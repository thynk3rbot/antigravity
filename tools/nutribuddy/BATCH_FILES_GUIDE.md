# NutriCalc Batch Files Guide

**Windows Operational Scripts for Development & Deployment**

---

## Overview

NutriCalc includes 5 batch files (`.bat` scripts) in the `tools/` directory to automate common development and deployment operations.

### File Locations

All batch files are in: `C:\Users\spw1\OneDrive\Documents\Code\Antigravity Repository\antigravity\tools\`

```
tools/
├── Setup_NutriCalc.bat              (Initialize environment)
├── Start_NutriCalc_App.bat          (Start app server only)
├── Start_NutriCalc_Docs.bat         (Start docs server only)
├── Start_NutriCalc_All.bat          (Start both servers)
└── Deploy_NutriCalc.bat             (Deployment manager)
```

---

## Quick Start (Choose One)

### Option 1: Interactive Menu (Recommended)
```bash
# Double-click or run:
Deploy_NutriCalc.bat

# Then select from menu:
# 1. Verify local setup
# 2. Test local development
# 3. View deployment status
# 4. Deploy to production
# 5. View help
```

### Option 2: Start Development Directly
```bash
# Double-click:
Start_NutriCalc_All.bat

# Opens two windows:
# - App server:  http://localhost:8100/static/
# - Docs server: http://localhost:8101/
```

### Option 3: First-Time Setup
```bash
# Run once to initialize:
Setup_NutriCalc.bat

# Checks:
# - Python is installed
# - Required files present
# - JSON configuration valid
```

---

## Detailed Guide

### 1. Setup_NutriCalc.bat

**Purpose:** Initialize development environment and verify all prerequisites

**When to use:**
- First time setting up NutriCalc on Windows
- After pulling new changes from Git
- To verify everything is ready before starting development

**What it does:**
✓ Checks Python 3.6+ is installed and in PATH
✓ Verifies all required files are present:
  - `static/index.html` (app)
  - `docs/index.html` (documentation)
  - `server.py` (local dev server)
  - `chemicals.json` (database)
✓ Validates JSON configuration files
✓ Displays next steps and available commands

**Output:**
```
[OK] Python found: Python 3.10.1
[OK] App found: static/index.html
[OK] Docs found: docs/index.html
[OK] Server found: server.py
[OK] chemicals.json - Valid
```

**If something is wrong:**
- Displays [ERROR] for missing files or invalid JSON
- Provides instructions to fix the issue
- Exits with error code so you can correct the problem

**Run it:**
```bash
# Double-click Setup_NutriCalc.bat
# Or in command prompt:
cd tools
Setup_NutriCalc.bat
```

---

### 2. Start_NutriCalc_App.bat

**Purpose:** Start the NutriCalc application server

**When to use:**
- During development to test app changes
- When you only need the app (not docs)
- To serve the app on port 8100

**What it does:**
- Starts Python HTTP server on port 8100
- Serves app from `tools/nutribuddy/static/`
- Auto-restarts if the server crashes
- Keeps running until you close the window

**Features:**
- ✓ Auto-restart on crash
- ✓ Clear error messages
- ✓ Color-coded output (green for running, red for error)
- ✓ Uses ctrl+C to gracefully stop

**Run it:**
```bash
# Double-click: Start_NutriCalc_App.bat
# Or in command prompt:
cd tools
Start_NutriCalc_App.bat
```

**Then open:**
- App: http://localhost:8100/static/
- Combined: http://localhost:8100/ (redirects to app)

**Server logs will show:**
```
NutriCalc - Local Development Server
Starting NutriCalc app on port 8100...
App will be accessible at: http://localhost:8100/static/
```

---

### 3. Start_NutriCalc_Docs.bat

**Purpose:** Start the documentation server

**When to use:**
- When testing documentation changes
- When you only need the docs (not app)
- To serve docs on port 8101

**What it does:**
- Starts Python HTTP server on port 8101
- Serves docs from `tools/nutribuddy/docs/`
- Auto-restarts if the server crashes
- Keeps running until you close the window

**Features:**
- ✓ Auto-restart on crash
- ✓ Separate window from app server
- ✓ Uses ctrl+C to gracefully stop
- ✓ Can run alongside app server

**Run it:**
```bash
# Double-click: Start_NutriCalc_Docs.bat
# Or in command prompt:
cd tools
Start_NutriCalc_Docs.bat
```

**Then open:**
- Docs: http://localhost:8101/

---

### 4. Start_NutriCalc_All.bat (Most Useful)

**Purpose:** Start both app and docs servers simultaneously

**When to use:**
- Full development setup (most common)
- Testing app + documentation together
- Before committing changes
- When you want everything accessible locally

**What it does:**
- Opens first window: App server on port 8100
- Opens second window: Docs server on port 8101
- Displays summary of available URLs
- Allows both to run independently
- Can close either window without affecting the other

**Features:**
- ✓ Launches both in separate windows
- ✓ Auto-restart on crash (in each window)
- ✓ Clear display of available URLs
- ✓ Can stop servers independently

**Run it:**
```bash
# Double-click: Start_NutriCalc_All.bat
# Or in command prompt:
cd tools
Start_NutriCalc_All.bat
```

**Then open in browser:**
- App: http://localhost:8100/static/
- Docs: http://localhost:8101/
- Combined: http://localhost:8100/ (redirects to app)

**What you'll see:**
```
=======================================================
    NutriCalc - Complete Development Environment
=======================================================

Available URLs:
  App:       http://localhost:8100/static/
  Docs:      http://localhost:8101/
  Combined:  http://localhost:8100/

To test:
  1. Open http://localhost:8100/static/ in your browser
  2. Select chemicals and click "Solve Formula"
  3. View production workflow and mixing guide
  4. Open http://localhost:8101/ for documentation
```

**To stop:**
- Close the command prompt window(s)
- Or press Ctrl+C in each window

---

### 5. Deploy_NutriCalc.bat (Deployment Menu)

**Purpose:** Interactive deployment manager with help and status

**When to use:**
- First time deploying
- When unsure what to do next
- To check deployment status
- To access documentation

**What it does:**
- Shows interactive menu with 6 options
- Verifies local setup
- Displays deployment status
- Provides deployment instructions
- Links to documentation
- Shows available URLs

**Menu options:**

```
1. Verify local setup
   → Checks Python, required files, JSON validity
   → Same as Setup_NutriCalc.bat

2. Test local development (start all services)
   → Launches both app and docs servers
   → Same as Start_NutriCalc_All.bat

3. View deployment status
   → Shows current deployment readiness
   → GitHub Pages status
   → Netlify status
   → Next steps

4. Deploy to production (GitHub Pages)
   → Shows deployment steps:
   → 1. Test locally
   → 2. Commit changes
   → 3. Push to main
   → 4. Wait for GitHub Actions
   → 5. Verify live

5. View help & documentation
   → Lists all documentation files
   → Explains common tasks
   → Shows useful URLs

6. Exit
   → Closes the menu
```

**Run it:**
```bash
# Double-click: Deploy_NutriCalc.bat
# Or in command prompt:
cd tools
Deploy_NutriCalc.bat
```

**Then:**
- Choose an option (1-6)
- Follow the instructions
- Press any key when done to return to menu
- Choose "6" to exit

---

## Common Workflows

### Workflow 1: Start Development
```bash
1. Double-click: Setup_NutriCalc.bat
   (Verify everything is ready)

2. Double-click: Start_NutriCalc_All.bat
   (Start app and docs servers)

3. Open browser: http://localhost:8100/static/
   (Use the app)

4. Edit files in:
   - static/index.html (app changes)
   - docs/index.html (docs changes)

5. Browser auto-reloads (save file → see changes)

6. Close windows when done
   (Servers stop automatically)
```

### Workflow 2: Deploy Changes
```bash
1. Make and test changes locally
   (Use Start_NutriCalc_All.bat)

2. Verify everything works
   (Test formula solver, docs, etc.)

3. Open Git Bash or Command Prompt

4. Commit and push:
   git add tools/nutribuddy/
   git commit -m "Update: Your changes"
   git push origin main

5. GitHub Actions auto-deploys
   (Takes 2-3 minutes)

6. Verify live:
   https://thynk3rbot.github.io/antigravity/static/
```

### Workflow 3: Quick Status Check
```bash
1. Double-click: Deploy_NutriCalc.bat

2. Choose option "3. View deployment status"

3. See:
   - GitHub Pages status
   - Netlify status
   - Next steps

4. Exit menu (choose "6")
```

### Workflow 4: First-Time Setup + Development
```bash
1. Double-click: Setup_NutriCalc.bat
   (Initialize and verify)

2. Double-click: Start_NutriCalc_All.bat
   (Start servers)

3. Open: http://localhost:8100/static/
   (Use the app)

4. Make changes and test

5. When ready to deploy:
   - Close server windows
   - Commit changes in Git
   - Push to main
   - GitHub Actions deploys automatically
```

---

## Troubleshooting

### "Python is not installed or not in PATH"

**Problem:**
```
[ERROR] Python is not installed or not in PATH
```

**Solution:**
1. Install Python 3.6+ from https://python.org
2. During installation, **check "Add Python to PATH"**
3. Restart command prompt window
4. Try the batch file again

**Verify installation:**
```bash
python --version
```

### "Required files are missing"

**Problem:**
```
[ERROR] Missing: static/index.html
```

**Solution:**
1. Verify you're in the correct directory
2. Check files exist in `tools/nutribuddy/`
3. Pull latest from GitHub:
   ```bash
   git pull origin main
   ```
4. Try the batch file again

### Server won't start on port 8100/8101

**Problem:**
```
[ERROR] Server crashed or failed to start
```

**Solution:**
1. Port may be in use by another application
2. Kill existing process:
   ```bash
   netstat -ano | findstr :8100
   taskkill /PID <process_id> /F
   ```
3. Try a different port:
   - Edit the batch file
   - Change `8100` to `8102`
   - Save and run again

### "Invalid JSON" error

**Problem:**
```
[ERROR] chemicals.json - Invalid JSON
```

**Solution:**
1. Open `tools/nutribuddy/chemicals.json`
2. Look for syntax errors:
   - Missing commas
   - Extra quotes
   - Unclosed braces
3. Use Python to find the error:
   ```bash
   python -m json.tool tools/nutribuddy/chemicals.json
   ```
4. Fix the error and save
5. Try the batch file again

---

## Color Codes

The batch files use color codes for status:

| Color | Meaning |
|-------|---------|
| **Bright Cyan (0A)** | App server running (normal state) |
| **Bright Blue (0B)** | Docs server / Status info |
| **Bright Yellow (0E)** | Menu / Interactive prompt |
| **Red (0C)** | Error occurred |
| **Green (0B)** | Setup complete / Success |

---

## Advanced: Customizing Ports

If you need to use different ports (e.g., 8100 is already in use):

**Edit `Start_NutriCalc_App.bat`:**
```batch
REM Change this line:
python server.py
REM To:
python server.py --port 8102
```

**Edit `Start_NutriCalc_Docs.bat`:**
```batch
REM Change this line:
python -m http.server 8101 -d docs
REM To:
python -m http.server 8102 -d docs
```

Then update URLs accordingly:
- App: http://localhost:8102/static/
- Docs: http://localhost:8102/ (instead of 8101)

---

## File Descriptions

### Setup_NutriCalc.bat (90 lines)
- Initializes environment
- Verifies Python, files, JSON
- Shows next steps

### Start_NutriCalc_App.bat (25 lines)
- Minimal, focused script
- App server only
- Auto-restart on crash

### Start_NutriCalc_Docs.bat (25 lines)
- Minimal, focused script
- Docs server only
- Auto-restart on crash

### Start_NutriCalc_All.bat (50 lines)
- Launches both servers
- Opens separate windows
- Displays summary & URLs

### Deploy_NutriCalc.bat (200 lines)
- Interactive menu system
- 6 main options
- Context-sensitive help
- Deployment status display

**Total:** 390 lines of batch code, modeled after Magic

---

## Related Documentation

For more information, see:

1. **README.md** — Quick start & project overview
2. **DEPLOY.md** — Comprehensive deployment guide
3. **GITHUB_ACTIONS_SETUP.md** — CI/CD pipeline details
4. **DEPLOYMENT_CHECKLIST.md** — Production readiness
5. **DEPLOYMENT_SUMMARY.md** — Project summary

---

## Support

If you encounter issues:

1. Check this guide first (Troubleshooting section)
2. Run `Deploy_NutriCalc.bat` → Option 5 (Help)
3. Check README.md or DEPLOY.md
4. Report issues on GitHub

---

**Last Updated:** 2026-03-08
**Status:** ✅ Ready for use
