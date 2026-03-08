# NutriCalc — Hydroponic Nutrient Formula Solver

**Production URL:** https://thynk3rbot.github.io/antigravity/static/

**Local Dev:** `python server.py` → http://localhost:8100

---

## Quick Start (5 minutes)

### 1. Clone & Navigate

```bash
git clone https://github.com/thynk3rbot/antigravity.git
cd antigravity/tools/nutribuddy
```

### 2. Start Local Dev Server

```bash
# Option A: Python (recommended)
python server.py
# Opens: http://localhost:8100

# Option B: Node http-server
npm install -g http-server
http-server -p 8100
# Opens: http://localhost:8100

# Option C: VS Code Live Server
# Right-click static/index.html → Open with Live Server
```

### 3. Open App

- **App:** http://localhost:8100/static/
- **Docs:** http://localhost:8100/docs/
- **Diagrams:** http://localhost:8100/docs/diagrams.html

### 4. Test Core Features

```
1. Select chemicals (tap checkboxes)
2. Load preset (click "Bugbee Standard")
3. Set EC target (drag slider: 1.2-2.5)
4. Click "Solve Formula"
5. View results: Nutrient Profile, Cost, Compounds
6. Click "Production" → See mixing guide
7. Click "📖 Docs" → View documentation
```

---

## Project Structure

```
tools/nutribuddy/
├── static/                      # App (served at /)
│   ├── index.html              # Main app (160KB)
│   │   ├─ Formula Builder       # Step 1: Select chemicals
│   │   ├─ Nutrient Profile      # Step 2: View ratios & EC
│   │   ├─ Chemical Library      # Manage compound database
│   │   ├─ Saved Formulas        # Browser storage (IndexedDB)
│   │   ├─ Dosing Output         # MQTT publish, pump mapping
│   │   └─ Production            # Mixing guide, BOM, checklist
│   ├── favicon.svg              # 64×64px animated logo (tab icon)
│   └── logo.svg                 # Full-size animated logo
│
├── docs/                        # Documentation site
│   ├── index.html              # 6-page doc hub (navigation)
│   │   ├─ Home                 # Overview, features
│   │   ├─ Concepts             # EC, NPK, A/B split, chemistry
│   │   ├─ Quickstart           # 5-step tutorial
│   │   ├─ User Guide           # Feature documentation
│   │   ├─ Reference            # Data tables, presets, units
│   │   └─ Troubleshooting      # FAQs, error solutions
│   └── diagrams.html           # 5 animated diagrams
│       ├─ Mixing Equipment     # 5-step cascade animation
│       ├─ Precipitation        # Ca²⁺ + SO₄²⁻ → CaSO₄
│       ├─ EC Meter Scale       # Gradient fill animation
│       ├─ A/B Split Reference  # Color-coded chemicals
│       └─ Stock Concentration  # 50×, 100×, 200× scaling
│
├── chemicals.json              # 80+ compounds (molar mass, formula, source)
├── mqtt_config.json            # MQTT defaults (host, port, topic)
├── price_config.json           # Chemical pricing sources
│
├── server.py                   # Local dev server (Python)
│   ├─ Auto-reload on file change
│   ├─ CORS enabled
│   └─ Serves on port 8100
│
├── netlify.toml                # Netlify config (routing, headers, security)
├── .gitignore                  # Git ignore rules (Python, IDE, cache)
├── DEPLOY.md                   # Deployment & maintenance guide
└── README.md                   # This file

Parent: .github/workflows/
└── deploy-nutribuddy.yml       # GitHub Actions workflow
    ├─ Validates files
    ├─ Deploys to GitHub Pages
    └─ Deploys to Netlify
```

---

## Deployment

### Automatic (Recommended)

```bash
# 1. Make changes
edit static/index.html  # or docs/index.html, chemicals.json, etc.

# 2. Commit & push
git add tools/nutribuddy/
git commit -m "Update: Add new preset formula"
git push origin main

# 3. Workflow runs automatically
# GitHub Actions → deploy-nutribuddy → logs
# After ✅: https://thynk3rbot.github.io/antigravity/static/ updates
```

**Automatic triggers:**
- ✅ Push to `main` touching `tools/nutribuddy/**`
- ✅ Pull request to `main` (validation only)
- ✅ Manual trigger: Actions tab → Run workflow

### Manual Deployment (If needed)

```bash
# GitHub Pages already live, so usually no manual push needed
# But if testing: https://github.com/thynk3rbot/antigravity/actions

# For Netlify (if secrets configured):
# See DEPLOY.md → Part B: Netlify Deployment
```

---

## Making Changes

### 1. Update App (index.html)

```bash
# 1. Edit static/index.html
# Examples:
#   - Add new preset in 'presets' object
#   - Add help tooltip: <span class="help-icon">❓</span>
#   - Modify UI styling in <style> section
#   - Fix bugs in JavaScript (bottom of file)

# 2. Test locally
python server.py
# Open http://localhost:8100/static/
# Edit → Save → Browser auto-reloads

# 3. Commit & push
git add tools/nutribuddy/static/index.html
git commit -m "Add: Help topic for X feature"
git push origin main
```

### 2. Update Docs (docs/index.html)

```bash
# 1. Edit docs/index.html
# Structure: Find <div id="page-PAGENAME" class="page hidden">
# Examples:
#   - Add paragraph to Concepts page
#   - Add FAQ to Troubleshooting
#   - Add table to Reference page
#   - Update code examples in Quickstart

# 2. Test locally
python server.py
# Open http://localhost:8100/docs/
# Edit → Save → Auto-reload

# 3. Commit & push
git add tools/nutribuddy/docs/index.html
git commit -m "Docs: Add FAQ for X question"
git push origin main
```

### 3. Update Diagrams (docs/diagrams.html)

```bash
# 1. Edit docs/diagrams.html
# Change SVG shapes, colors, or animation timing
# Examples:
#   - Adjust animation-delay: 0s, 1.5s, 3s, 4.5s, 6s
#   - Change colors: var(--accent) = #4ade80 (green)
#   - Edit text labels and arrow positions

# 2. Test locally
python server.py
# Open http://localhost:8100/docs/diagrams.html
# Watch animations, verify timing looks good

# 3. Commit & push
git add tools/nutribuddy/docs/diagrams.html
git commit -m "Diagrams: Adjust animation timing for clarity"
git push origin main
```

### 4. Update Chemical Database (chemicals.json)

```bash
# 1. Edit chemicals.json
# Add new compound:
{
  "name": "Potassium Sulfate",
  "formula": "K₂SO₄",
  "elements": {
    "K": 44.9,
    "S": 18.4,
    "O": 76.7
  },
  "molar_mass": 174.26,
  "source": "https://example.com",
  "price": {
    "value": 3.75,
    "currency": "USD",
    "unit": "kg",
    "date": "2026-03-08"
  }
}

# 2. Validate JSON
python3 -m json.tool tools/nutribuddy/chemicals.json > /dev/null && echo "✓ Valid" || echo "✗ Invalid"

# 3. Test app loads compounds
python server.py
# Open http://localhost:8100/static/
# Check chemical library includes new compound

# 4. Commit & push
git add tools/nutribuddy/chemicals.json
git commit -m "Compounds: Add Potassium Sulfate"
git push origin main
```

### 5. Update MQTT Config (mqtt_config.json)

```bash
# 1. Edit mqtt_config.json
# Set default broker, port, topic prefix
{
  "broker": "mqtt.home.local",
  "port": 1883,
  "username": "",
  "password": "",
  "topic_prefix": "nutribuddy/"
}

# 2. Validate JSON
python3 -m json.tool tools/nutribuddy/mqtt_config.json > /dev/null

# 3. Test app loads defaults
python server.py
# Open Dosing Output page → check defaults populated

# 4. Commit & push
git commit -m "Config: Update MQTT defaults for new broker"
```

---

## Branching Workflow

### For Maintenance Tasks

```bash
# 1. Create feature branch
git checkout -b feature/update-preset-formulas

# 2. Make changes
# Edit static/index.html, chemicals.json, etc.

# 3. Commit locally
git add .
git commit -m "Add: Tomato-specific nutrient preset"

# 4. Open Pull Request
git push origin feature/update-preset-formulas
# GitHub UI: Create PR from branch to main

# 5. Review & merge
# GitHub: Request review, approve, merge to main

# 6. Auto-deploy triggers
# Workflow runs → GitHub Pages updates
```

### Quick Hotfix (Urgent Bug)

```bash
# If immediate fix needed:
git checkout main
git pull origin main

# Make minimal changes
edit static/index.html

# Commit & push directly to main (for hotfixes only)
git add tools/nutribuddy/static/index.html
git commit -m "Hotfix: Fix broken formula solver"
git push origin main

# Workflow runs immediately
# Site updates within 1-2 minutes
```

---

## Common Tasks

### Add a New Nutrient Preset

1. Open `static/index.html`
2. Find `presets = { ... }` (around line 500)
3. Add new preset:
   ```javascript
   "Lettuce Optimized": {
     "compounds": ["Calcium Nitrate", "Potassium Nitrate", "KH2PO4", ...],
     "ec_target": 1.2,
     "compounds_data": {
       "Calcium Nitrate": 2.5,
       "Potassium Nitrate": 3.0,
       // ... molar amounts
     }
   }
   ```
4. Test locally → Save → Commit → Push

### Add Help Text for a Feature

1. Open `static/index.html`
2. Find the relevant UI element
3. Add help icon & tooltip:
   ```html
   <span class="help-icon" data-help-id="ec-target">❓</span>
   <div id="ec-target" class="tooltip">
     <p><strong>EC Target</strong></p>
     <p>Electrical conductivity measure (mS/cm)...</p>
   </div>
   ```
4. Commit & Push

### Fix a Bug

1. Identify issue (test in local browser)
2. Find & fix code in `static/index.html` or `docs/index.html`
3. Test fix locally
4. Commit with clear message: `git commit -m "Fix: X not working when Y"`
5. Push to main (or create PR for review)
6. Workflow deploys automatically

### Update Documentation

1. Edit `docs/index.html`
2. Find relevant `<div id="page-PAGENAME">`
3. Update content
4. Test: Open docs locally, read through all pages
5. Commit & Push

---

## Troubleshooting

### Server won't start

```bash
# Check Python version
python --version  # Should be 3.6+

# Try with python3
python3 server.py

# Check port 8100 is available
lsof -i :8100  # macOS/Linux
netstat -ano | findstr :8100  # Windows

# Try different port
python server.py --port 8101
```

### Changes not showing up locally

```bash
# 1. Hard refresh browser
Ctrl+Shift+R (Windows/Linux)
Cmd+Shift+R (Mac)

# 2. Check server is running
Terminal should show: "Serving on http://localhost:8100"

# 3. Check file was saved
ls tools/nutribuddy/static/index.html  # Should be recent timestamp
```

### JSON validation failed

```bash
# Validate file
python3 -m json.tool tools/nutribuddy/chemicals.json

# Error message shows line number with syntax error
# Look for: missing comma, extra quotes, missing braces
```

### Deployment failed on GitHub Actions

```bash
# 1. Check Actions tab: https://github.com/thynk3rbot/antigravity/actions
# 2. Click latest run → View error logs
# 3. Common errors:
#    - HTML validation: missing </html> tag
#    - JSON validation: syntax error (comma, quote, brace)
#    - Missing file: favicon.svg, logo.svg not found

# Fix & push again (workflow re-runs automatically)
```

---

## Performance Notes

| Metric | Value | Notes |
|--------|-------|-------|
| **App size** | ~160 KB | index.html (inline CSS/JS) |
| **Docs size** | ~48 KB | index.html + diagrams.html |
| **Load time** | <500ms | No external dependencies |
| **Browser support** | Chrome 90+, Firefox 88+, Safari 14+ | ES6 standard |
| **Mobile responsive** | Yes | Tested on 375px width |
| **Accessibility** | WCAG 2.1 AA | Keyboard nav, screen reader friendly |

---

## Tech Stack

| Layer | Technology | Notes |
|-------|-----------|-------|
| **Frontend** | HTML5 + CSS3 + JavaScript (ES6) | No frameworks, pure vanilla |
| **UI** | Inline CSS (no external sheets) | Single file for portability |
| **Graphics** | SVG animations (CSS keyframes) | Logo, favicon, diagrams |
| **Data** | JSON (local browser storage) | IndexedDB for saved formulas |
| **Dev Server** | Python 3.6+ | `http.server` with auto-reload |
| **Deployment** | GitHub Actions + GitHub Pages | CI/CD pipeline |
| **Backup** | Netlify (optional) | Automatic deployment |
| **Version Control** | Git + GitHub | Standard workflow |

---

## Support & Resources

- **Main Repo:** https://github.com/thynk3rbot/antigravity
- **Live App:** https://thynk3rbot.github.io/antigravity/static/
- **Deployment Guide:** `DEPLOY.md` (in this directory)
- **GitHub Actions Setup:** `GITHUB_ACTIONS_SETUP.md` (in repo root)
- **Issues:** GitHub → Issues tab

---

## Version Info

- **Current Version:** v1.0.0
- **Release Date:** 2026-03-08
- **Status:** ✅ Production-ready
- **Last Updated:** 2026-03-08

For updates, see: `DEPLOY.md` → Part H: Version History

---

**Happy fermenting! 🌱**
