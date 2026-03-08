# NutriCalc Deployment & Maintenance Guide

**Last Updated:** 2026-03-08
**NutriCalc Version:** v1.0.0
**Status:** Production-ready (GitHub Pages + Netlify dual deployment)

---

## Overview

NutriCalc is a fully static site (HTML/CSS/SVG/JSON) with no backend required. It deploys to:

- **GitHub Pages** (`https://<owner>.github.io/antigravity/static/`) — Primary
- **Netlify** (`https://<netlify-site>.netlify.app/`) — Backup / Preview
- **Local Dev** (`http://localhost:8100`) — Development

All deployments are **automatic on `main` branch push** via GitHub Actions.

---

## Deployment Architecture

### File Structure

```
tools/nutribuddy/
├── static/
│   ├── index.html          (Main app: Formula Builder, Solver, Production)
│   ├── favicon.svg         (64×64px animated logo)
│   └── logo.svg            (Full-size animated logo)
├── docs/
│   ├── index.html          (6-page documentation site)
│   ├── diagrams.html       (5 animated diagrams)
│   └── (content served from docs/)
├── netlify.toml            (Netlify routing & headers)
├── chemicals.json          (80+ compounds database)
├── mqtt_config.json        (MQTT defaults)
├── price_config.json       (Chemical pricing)
└── server.py               (Optional local dev server)
```

### GitHub Pages Structure

```
https://thynk3rbot.github.io/antigravity/
├── static/                 (NutriCalc app)
│   ├── index.html
│   ├── favicon.svg
│   └── logo.svg
├── docs/                   (Documentation)
│   ├── index.html
│   └── diagrams.html
└── (root redirects to /static/)
```

---

## Part A: GitHub Pages Deployment

### Automatic Deployment (Recommended)

**Trigger:** Any push to `main` branch touching `tools/nutribuddy/**`

**Workflow:** `.github/workflows/deploy-nutribuddy.yml`

1. **Validation** — HTML/JSON syntax, favicon/logo present
2. **Build** — Copy files to `build/` directory
3. **Deploy** — Push to GitHub Pages (requires `pages` write permission)
4. **Test** — Verify files deployed correctly

**No setup required** — GitHub Actions uses built-in `GITHUB_TOKEN`.

### Manual Deployment

If automatic deployment fails:

```bash
# 1. Ensure main branch is clean
git status

# 2. Push to main (triggers workflow)
git push origin main

# 3. Check workflow status
# GitHub UI → Actions tab → "Deploy NutriCalc to GitHub Pages & Netlify"

# 4. Verify deployment
curl https://thynk3rbot.github.io/antigravity/static/
```

### Testing Live Deployment

```bash
# Test app loads
curl https://thynk3rbot.github.io/antigravity/static/ | grep -q "NutriCalc" && echo "✓ App live"

# Test docs load
curl https://thynk3rbot.github.io/antigravity/docs/ | grep -q "Concepts" && echo "✓ Docs live"

# Test favicon
curl https://thynk3rbot.github.io/antigravity/static/favicon.svg | grep -q "svg" && echo "✓ Favicon live"
```

---

## Part B: Netlify Deployment

### Prerequisites

1. **Netlify account** — https://netlify.com (free tier sufficient)
2. **Connect GitHub repo** — Netlify app authorization
3. **Store secrets** — GitHub repo Settings → Secrets

### Setup (One-Time)

```bash
# 1. In GitHub repo: Settings → Secrets and variables → Actions
# Add secrets:
#   NETLIFY_AUTH_TOKEN  = (from Netlify: User Settings → Applications)
#   NETLIFY_SITE_ID     = (from Netlify: Site Settings → Site Details)

# 2. In Netlify: Create new site
#   - Connect to GitHub
#   - Select branch: main
#   - Build command: (leave blank, static site)
#   - Publish directory: tools/nutribuddy

# 3. Verify GitHub secrets are set
git ls-remote origin  # Confirm origin is reachable
```

### Finding Netlify Credentials

**Auth Token:**
1. Log in to https://netlify.com
2. User menu (top-right) → Account settings
3. Applications → Personal access tokens
4. Create token → Copy and store in GitHub secrets as `NETLIFY_AUTH_TOKEN`

**Site ID:**
1. Netlify dashboard → Select NutriCalc site
2. Site settings → General
3. Copy "API ID" → Store in GitHub secrets as `NETLIFY_SITE_ID`

### Automatic Deployment

Push to `main` → GitHub Actions → Netlify deployment (≈30 sec)

```bash
# Trigger workflow
git push origin main

# Monitor in Netlify dashboard
# https://app.netlify.com → NutriCalc site → Deploys
```

### Manual Deployment (If Secrets Unavailable)

```bash
# Option 1: Install Netlify CLI
npm install -g netlify-cli

# Option 2: Authenticate
netlify login

# Option 3: Deploy
cd tools/nutribuddy
netlify deploy --prod
```

### Rollback to Previous Deploy

```bash
# In Netlify dashboard:
# Site → Deploys → Find previous good deploy → Set as production

# Or via CLI:
netlify deploy --prod --message "Rollback to previous version"
```

---

## Part C: Local Development (Port 8100)

### Using Python Server

```bash
cd tools/nutribuddy

# Option 1: Python 3.10+
python server.py

# Opens: http://localhost:8100
# Auto-reload on file changes

# Ctrl+C to stop
```

### Using Browser Live Server (VS Code)

```bash
# 1. Install extension: Live Server (ritwickdey)
# 2. Right-click on index.html (in static/)
# 3. "Open with Live Server"
# 4. Auto-opens: http://localhost:5500
```

### Using Node http-server

```bash
# Install once:
npm install -g http-server

# Serve:
cd tools/nutribuddy
http-server -p 8100

# Opens: http://localhost:8100
```

### Local Testing Workflow

```bash
1. Start server: python server.py
2. Open app: http://localhost:8100/static/
3. Open docs: http://localhost:8100/docs/
4. Edit files (HTML/CSS/SVG in static/, docs/)
5. Browser auto-reloads
6. Test: Formula solve, MQTT publish, saved formulas
7. Ready to push: git add . && git commit -m "..."
```

---

## Part D: Updating Content

### Adding Help Topics (In-App)

**File:** `tools/nutribuddy/static/index.html`

```html
<!-- Example: Add help for a new feature -->
<div class="help-icon" title="Hover for help" data-help-id="new-feature-help">❓</div>
<div id="new-feature-help" class="tooltip hidden">
  <p><strong>Feature Name</strong></p>
  <p>Explanation here...</p>
</div>

<!-- Then add CSS for visibility on hover -->
<style>
  .tooltip {
    display: none;
    position: absolute;
    background: var(--surface);
    padding: 12px;
    border-radius: 6px;
    z-index: 1000;
  }
  .help-icon:hover + .tooltip {
    display: block;
  }
</style>
```

### Updating Documentation Pages

**File:** `tools/nutribuddy/docs/index.html`

The page is a single-file multi-page app (pages switch via JavaScript):

```javascript
// In the HTML, find the page structure:
<div id="page-concepts" class="page hidden">
  <!-- Update content here -->
</div>

// To add a new page:
1. Create new <div id="page-newpage" class="page hidden">
2. Add button to nav: <button onclick="showPage('newpage')">New Page</button>
3. Add page toggle in showPage() function
```

### Updating Diagrams

**File:** `tools/nutribuddy/docs/diagrams.html`

Diagrams use SVG + CSS animations:

```html
<!-- Animation timing adjustments -->
<style>
  @keyframes fillScale {
    0% { width: 0%; }
    100% { width: 100%; }
  }
  .step-1 { animation-delay: 0s; }
  .step-2 { animation-delay: 1.5s; }
  /* Adjust delays to match your narrative */
</style>

<!-- Color changes -->
<circle fill="var(--accent)" />  <!-- Green nutrient (NPK) -->
<circle fill="var(--accent2)" /> <!-- Blue/purple (A/B split) -->

<!-- Edit color variables in index.html <style> section -->
--accent: #4ade80;    /* Green for nutrients */
--accent2: #60a5fa;   /* Blue for chemistry */
```

### Updating Chemical Database

**File:** `tools/nutribuddy/chemicals.json`

```json
{
  "compounds": [
    {
      "name": "Calcium Nitrate",
      "formula": "Ca(NO₃)₂",
      "elements": {
        "Ca": 169.0,
        "N": 280.0
      },
      "molar_mass": 164.09,
      "source": "https://example.com",
      "price": {
        "value": 5.50,
        "currency": "USD",
        "unit": "kg",
        "date": "2026-03-08"
      }
    }
  ]
}
```

### Updating Preset Formulas

**File:** `tools/nutribuddy/static/index.html` → Search for `presets = {`

```javascript
presets = {
  "Bugbee Standard": {
    "compounds": ["Calcium Nitrate", "Potassium Nitrate", ...],
    "ec_target": 1.8,
    "compounds_data": { /* molar amounts */ }
  }
}
```

---

## Part E: Production Maintenance

### Health Checks

Run monthly:

```bash
# 1. Check file sizes (warn if >2MB each)
ls -lh tools/nutribuddy/static/*.html tools/nutribuddy/docs/*.html

# 2. Validate HTML/JSON
python3 -m py_compile tools/nutribuddy/server.py
python3 -m json.tool tools/nutribuddy/chemicals.json > /dev/null

# 3. Check GitHub Pages
curl -I https://thynk3rbot.github.io/antigravity/static/ | grep 200

# 4. Check Netlify
curl -I https://<netlify-site>.netlify.app/ | grep 200
```

### Monitoring Deployments

```bash
# GitHub Actions logs
# https://github.com/thynk3rbot/antigravity/actions/workflows/deploy-nutribuddy.yml

# Netlify deploy logs
# https://app.netlify.com → Site → Deploys → Click deploy → View logs

# GitHub Pages logs (if failed)
# GitHub UI → Actions tab → deploy-nutribuddy → Logs
```

### Rollback Procedure

**If deployment breaks:**

```bash
# 1. Identify last good commit
git log --oneline tools/nutribuddy/ | head -5

# 2. Revert problematic commit
git revert <commit-hash>

# 3. Push to main (triggers auto-deploy)
git push origin main

# 4. Verify rollback succeeded
# GitHub Actions → Latest run should succeed
# Check live URL: https://thynk3rbot.github.io/antigravity/static/
```

### Performance Optimization

**Image/SVG Compression:**
```bash
# Install imagemin
npm install -g imagemin imagemin-svgo

# Compress SVGs (optional, current SVGs are small)
imagemin tools/nutribuddy/static/*.svg --out-dir=tools/nutribuddy/static/
```

**Cache Busting** (if needed):
```bash
# Append version hash to resource URLs in HTML
<!-- Before: -->
<link rel="icon" href="favicon.svg" type="image/svg+xml">

<!-- After (if caching issues arise): -->
<link rel="icon" href="favicon.svg?v=1.0.0" type="image/svg+xml">

# Then update version in all links after significant changes
```

---

## Part F: Troubleshooting

### GitHub Pages Deploy Fails

**Issue:** Workflow shows ❌ in Actions tab

**Steps:**
1. Check workflow logs: `Actions → deploy-nutribuddy → Latest run`
2. Look for validation errors:
   - `Missing closing HTML tag` → Fix in `static/index.html` or `docs/index.html`
   - `Invalid JSON` → Fix in `.json` files (use `python3 -m json.tool file.json`)
   - `favicon.svg missing` → Ensure file exists in `static/`
3. Revert last commit if unsure: `git revert HEAD && git push`

### Netlify Deploy Fails

**Issue:** Netlify shows failed deploy

**Steps:**
1. Check GitHub secrets: Settings → Secrets → Verify `NETLIFY_AUTH_TOKEN` and `NETLIFY_SITE_ID` exist
2. Verify Site ID:
   ```bash
   # Should match Netlify Site settings
   grep -r "NETLIFY_SITE_ID" .github/workflows/
   ```
3. Re-authenticate (if expired):
   ```bash
   # In Netlify: Account settings → Personal access tokens → Regenerate
   # Update GitHub secret with new token
   ```

### Files Not Updating Live

**Issue:** Changes pushed but not visible on site

**Steps:**
1. Wait 2-3 minutes for deployment to complete
2. Hard refresh browser (Ctrl+Shift+R on Windows, Cmd+Shift+R on Mac)
3. Check GitHub Actions workflow succeeded
4. Verify file in GitHub repo: `tools/nutribuddy/` should show latest commit

### localhost:8100 Not Accessible

**Issue:** `Connection refused` error

**Steps:**
1. Ensure server is running: `python server.py`
2. Check port isn't in use: `lsof -i :8100` (macOS/Linux) or `netstat -ano | findstr :8100` (Windows)
3. Kill existing process and restart
4. Try alternate port: `python server.py --port 8101`

---

## Part G: Future Enhancements

### Planned Features

- [ ] **MQTT Live Dashboard** — Real-time pump dosing visualization
- [ ] **Formula Sharing** — QR codes or shareable links
- [ ] **Crop Library** — Pre-configured formulas for 20+ crops
- [ ] **Cost Analytics** — Historical price trends, bulk discounts
- [ ] **Precision Dosing** — Micro-adjustments for high-value crops

### Scaling Beyond Static Site

If future features require a backend:

1. **Deploy app server** (Python/Node) on separate service (Heroku/Railway)
2. **Keep static docs** on GitHub Pages/Netlify (no change to current setup)
3. **Update app HTML** to POST API calls to backend
4. **Update GitHub Actions** to deploy both app + server
5. **Use environment variables** for API endpoints (dev vs. prod)

---

## Part H: Version History

| Version | Date | Changes |
|---------|------|---------|
| **v1.0.0** | 2026-03-08 | Initial release: Formula builder, docs, dual deployment (GitHub Pages + Netlify) |
| v0.9.0 | 2026-03-07 | Documentation website with animated diagrams |
| v0.8.0 | 2026-03-06 | In-app help system + logo branding |
| v0.1.0 | 2026-01-01 | Initial NutriCalc solver prototype |

---

## Part I: Support & Contact

- **Repository:** https://github.com/thynk3rbot/antigravity
- **NutriCalc App:** https://thynk3rbot.github.io/antigravity/static/
- **Backup (Netlify):** https://nutriCalc-backup.netlify.app/ (configure NETLIFY_SITE_ID)
- **Issues:** GitHub → Issues tab → Report bugs or request features

---

**Last Maintenance Check:** 2026-03-08 ✓
