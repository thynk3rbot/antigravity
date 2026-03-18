"""
NutriCalc Price Scraper
Searches multiple sources for commodity fertilizer/chemical prices.

Strategy:
  - DuckDuckGo HTML search (no API key, respectful rate limits) as primary discovery
  - Per-source pattern matching for known bulk supplier sites
  - Stores: source_name, url, price_per_kg, unit_size_kg, currency, date_scraped
  - Amazon links are surfaced for manual verification (direct scraping violates ToS)
"""

import re
import time
import logging
import sqlite3
import urllib.parse
from datetime import datetime
from dataclasses import dataclass, field
from typing import Optional

import httpx
from bs4 import BeautifulSoup

log = logging.getLogger("nutribuddy.scraper")

# ── User-agent that behaves like a real browser ───────────────────────────────
HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/124.0.0.0 Safari/537.36"
    ),
    "Accept-Language": "en-US,en;q=0.9",
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
}

# ── Known bulk supplier domain patterns ──────────────────────────────────────
BULK_SUPPLIERS = [
    "greenwaybiotech.com",
    "bulkrea gents.com",
    "hydrofarm.com",
    "amazonbulk.com",
    "generalhydroponics.com",
    "chemicalplanet.com",
    "trifectanutients.com",
    "amazon.com",
    "ebay.com",
]

# ── Search query templates per source type ────────────────────────────────────
SEARCH_TEMPLATES = {
    "bulk_retail":  '"{name}" fertilizer buy "per kg" OR "per lb" hydroponic',
    "amazon":       '"{name}" fertilizer site:amazon.com "per kg" OR "lb"',
    "lab_supplier": '"{name}" reagent grade buy bulk "per kg"',
    "agri":         '"{name}" fertilizer agricultural grade buy price',
}


@dataclass
class PriceResult:
    compound_id: int
    compound_name: str
    source_name: str
    source_url: str
    price_raw: str          # e.g. "$12.99 / 5 lb"
    price_per_kg: float     # normalised to per-kg USD
    unit_size_kg: float     # package size in kg (0 = unknown)
    currency: str = "USD"
    date_scraped: str = ""
    notes: str = ""

    def __post_init__(self):
        if not self.date_scraped:
            self.date_scraped = datetime.utcnow().isoformat()


# ── Price extraction helpers ──────────────────────────────────────────────────

# Regex patterns to find prices in text snippets
_PRICE_RE = re.compile(
    r'\$\s*([\d,]+\.?\d*)'                        # $12.99
    r'(?:\s*/\s*(\d+\.?\d*)\s*(lb|lbs|kg|g|oz))?',  # optional / 5 lb
    re.IGNORECASE
)
_PER_KG_RE = re.compile(r'([\d,]+\.?\d*)\s*/\s*kg', re.IGNORECASE)
_PER_LB_RE = re.compile(r'\$\s*([\d,]+\.?\d*)\s*/\s*(?:lb|lbs?)', re.IGNORECASE)


def _parse_price_text(text: str) -> tuple[float, float, str]:
    """
    Extract (price_per_kg, unit_size_kg, price_raw) from a text snippet.
    Returns (0, 0, '') if nothing found.
    """
    # Direct per-kg price
    m = _PER_KG_RE.search(text)
    if m:
        price = float(m.group(1).replace(',', ''))
        return price, 1.0, m.group(0)

    # Dollar + optional unit
    m = _PRICE_RE.search(text)
    if m:
        raw_price = float(m.group(1).replace(',', ''))
        qty_str = m.group(2)
        unit_str = (m.group(3) or '').lower()
        price_raw = m.group(0)

        if not qty_str:
            # Assume 1 lb if no unit given (common in US retail)
            return raw_price * 2.205, 0.454, price_raw

        qty = float(qty_str)
        if unit_str in ('lb', 'lbs'):
            kg = qty * 0.453592
            return round(raw_price / kg, 2), round(kg, 3), price_raw
        elif unit_str == 'kg':
            return round(raw_price / qty, 2), qty, price_raw
        elif unit_str == 'g':
            kg = qty / 1000
            return round(raw_price / kg, 2), round(kg, 3), price_raw
        elif unit_str == 'oz':
            kg = qty * 0.0283495
            return round(raw_price / kg, 2), round(kg, 3), price_raw

    return 0.0, 0.0, ''


# ── DuckDuckGo HTML search ────────────────────────────────────────────────────

def _ddg_search(query: str, max_results: int = 8) -> list[dict]:
    """
    Fetch DuckDuckGo HTML results for a query.
    Returns list of {title, url, snippet}.
    Respects DDG by adding a delay and not hammering it.
    """
    encoded = urllib.parse.quote_plus(query)
    url = f"https://html.duckduckgo.com/html/?q={encoded}"
    try:
        resp = httpx.get(url, headers=HEADERS, timeout=12, follow_redirects=True)
        resp.raise_for_status()
    except Exception as e:
        log.warning(f"DDG search failed for '{query}': {e}")
        return []

    soup = BeautifulSoup(resp.text, "html.parser")
    results = []
    for result in soup.select(".result")[:max_results]:
        title_el = result.select_one(".result__title")
        link_el  = result.select_one(".result__url")
        snip_el  = result.select_one(".result__snippet")
        if not title_el:
            continue
        title   = title_el.get_text(strip=True)
        raw_url = link_el.get_text(strip=True) if link_el else ""
        snippet = snip_el.get_text(strip=True) if snip_el else ""
        # DDG encodes the actual URL in the href
        href = title_el.find("a", href=True)
        actual_url = href["href"] if href else raw_url
        if actual_url.startswith("//duckduckgo.com/l/?"):
            parsed = urllib.parse.urlparse(actual_url)
            qs = urllib.parse.parse_qs(parsed.query)
            actual_url = qs.get("uddg", [raw_url])[0]
        results.append({"title": title, "url": actual_url, "snippet": snippet})

    return results


# ── Per-source scrape logic ───────────────────────────────────────────────────

def _classify_source(url: str) -> str:
    """Return a friendly source name from a URL."""
    host = urllib.parse.urlparse(url).netloc.lower().replace("www.", "")
    known = {
        "amazon.com": "Amazon",
        "ebay.com": "eBay",
        "greenwaybiotech.com": "Greenway Biotech",
        "hydrofarm.com": "Hydrofarm",
        "bulkreagents.com": "BulkReagents",
        "generalhydroponics.com": "General Hydroponics",
    }
    for domain, name in known.items():
        if domain in host:
            return name
    return host.split(".")[0].title()


def _scrape_compound(compound: dict, source_types: list[str],
                     delay: float = 1.5) -> list[PriceResult]:
    """
    Search for pricing for a single compound across requested source types.
    Returns list of PriceResult (may be empty).
    """
    name = compound["name"]
    cid  = compound["id"]
    results: list[PriceResult] = []
    seen_urls: set[str] = set()

    for src_type in source_types:
        template = SEARCH_TEMPLATES.get(src_type, SEARCH_TEMPLATES["bulk_retail"])
        query = template.format(name=name)
        log.info(f"[{name}] Searching ({src_type}): {query}")

        hits = _ddg_search(query, max_results=6)
        time.sleep(delay)  # polite rate limit

        for hit in hits:
            url = hit["url"]
            if url in seen_urls or not url.startswith("http"):
                continue
            seen_urls.add(url)

            # Combine title + snippet for price extraction
            text = f"{hit['title']} {hit['snippet']}"
            price_kg, unit_kg, raw = _parse_price_text(text)

            if price_kg <= 0:
                continue
            if price_kg > 500:          # sanity filter — no fertilizer costs >$500/kg
                continue
            if price_kg < 0.05:         # sanity filter — nothing is cheaper than $0.05/kg
                continue

            src_name = _classify_source(url)
            note = f"Extracted from search snippet: {hit['snippet'][:120]}"

            results.append(PriceResult(
                compound_id=cid,
                compound_name=name,
                source_name=src_name,
                source_url=url,
                price_raw=raw,
                price_per_kg=round(price_kg, 2),
                unit_size_kg=round(unit_kg, 3),
                notes=note
            ))

            log.info(f"[{name}] Found: ${price_kg:.2f}/kg at {src_name} ({url[:60]})")

        if len(results) >= 5:
            break   # enough sources found for this compound

    return results


# ── Main public API ───────────────────────────────────────────────────────────

class PriceScraper:
    def __init__(self, db_path: str, source_types: list[str] = None):
        self.db_path = db_path
        self.source_types = source_types or ["bulk_retail", "lab_supplier"]
        self._init_db()

    def _init_db(self):
        with sqlite3.connect(self.db_path) as db:
            db.execute("""
                CREATE TABLE IF NOT EXISTS prices (
                    id            INTEGER PRIMARY KEY AUTOINCREMENT,
                    compound_id   INTEGER NOT NULL,
                    compound_name TEXT NOT NULL,
                    source_name   TEXT,
                    source_url    TEXT,
                    price_raw     TEXT,
                    price_per_kg  REAL,
                    unit_size_kg  REAL,
                    currency      TEXT DEFAULT 'USD',
                    date_scraped  TEXT,
                    notes         TEXT
                )
            """)
            db.commit()

    def get_prices(self, compound_id: int) -> list[dict]:
        with sqlite3.connect(self.db_path) as db:
            db.row_factory = sqlite3.Row
            rows = db.execute(
                "SELECT * FROM prices WHERE compound_id=? ORDER BY date_scraped DESC",
                (compound_id,)
            ).fetchall()
        return [dict(r) for r in rows]

    def get_best_price(self, compound_id: int) -> Optional[dict]:
        """Return the most recent lowest price_per_kg for a compound."""
        prices = self.get_prices(compound_id)
        if not prices:
            return None
        # Filter to prices from last 30 days, then pick cheapest
        cutoff = datetime.utcnow().isoformat()[:10]
        recent = [p for p in prices if p["date_scraped"][:10] >= cutoff[:10]]
        pool = recent if recent else prices
        return min(pool, key=lambda p: p["price_per_kg"])

    def get_all_best_prices(self) -> dict[int, dict]:
        """Return {compound_id: best_price_dict} for all compounds that have prices."""
        with sqlite3.connect(self.db_path) as db:
            db.row_factory = sqlite3.Row
            rows = db.execute(
                """
                SELECT p.* FROM prices p
                INNER JOIN (
                    SELECT compound_id, MIN(price_per_kg) as min_price
                    FROM prices GROUP BY compound_id
                ) best ON p.compound_id = best.compound_id
                         AND p.price_per_kg = best.min_price
                """
            ).fetchall()
        result = {}
        for r in rows:
            d = dict(r)
            result[d["compound_id"]] = d
        return result

    def scrape_compound(self, compound: dict) -> list[dict]:
        """Scrape prices for a single compound. Returns list of stored price dicts."""
        results = _scrape_compound(compound, self.source_types)
        stored = []
        with sqlite3.connect(self.db_path) as db:
            for r in results:
                db.execute("""
                    INSERT INTO prices
                      (compound_id, compound_name, source_name, source_url,
                       price_raw, price_per_kg, unit_size_kg, currency, date_scraped, notes)
                    VALUES (?,?,?,?,?,?,?,?,?,?)
                """, (
                    r.compound_id, r.compound_name, r.source_name, r.source_url,
                    r.price_raw, r.price_per_kg, r.unit_size_kg, r.currency,
                    r.date_scraped, r.notes
                ))
            db.commit()
        stored = self.get_prices(compound["id"])
        return stored

    def scrape_all(self, compounds: list[dict],
                   progress_cb=None) -> dict[int, list[dict]]:
        """
        Scrape prices for all compounds.
        progress_cb(compound_name, index, total) called after each compound.
        Returns {compound_id: [price_dicts]}
        """
        results = {}
        total = len(compounds)
        for i, c in enumerate(compounds):
            log.info(f"Scraping {i+1}/{total}: {c['name']}")
            prices = self.scrape_compound(c)
            results[c["id"]] = prices
            if progress_cb:
                progress_cb(c["name"], i + 1, total)
            time.sleep(2.0)   # inter-compound delay
        return results

    def clear_prices(self, compound_id: int = None):
        with sqlite3.connect(self.db_path) as db:
            if compound_id:
                db.execute("DELETE FROM prices WHERE compound_id=?", (compound_id,))
            else:
                db.execute("DELETE FROM prices")
            db.commit()
