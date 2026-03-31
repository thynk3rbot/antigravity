@echo off
TITLE NutriCalc - Deployment Manager
color 0E
echo =======================================================
echo      NutriCalc - Deployment Manager
echo =======================================================
echo.
echo This script helps you deploy NutriCalc to production.
echo.
pause
cls

:menu
color 0E
echo =======================================================
echo      NutriCalc Deployment Options
echo =======================================================
echo.
echo 1. Verify local setup
echo 2. Test local development (start all services)
echo 3. View deployment status
echo 4. Deploy to production (GitHub Pages)
echo 5. View help & documentation
echo 6. Exit
echo.
set /p choice="Enter your choice (1-6): "

if "%choice%"=="1" goto verify
if "%choice%"=="2" goto test
if "%choice%"=="3" goto status
if "%choice%"=="4" goto deploy
if "%choice%"=="5" goto help
if "%choice%"=="6" goto end
echo Invalid choice. Please try again.
timeout /t 2 >nul
cls
goto menu

:verify
cls
color 0A
echo =======================================================
echo      Verifying Local Setup
echo =======================================================
echo.
cd /d "%~dp0\..\tools\nutribuddy"
python -m json.tool chemicals.json >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] chemicals.json is valid
) else (
    echo [ERROR] chemicals.json is invalid
)
if exist "static\index.html" (
    echo [OK] App file found (static/index.html)
) else (
    echo [ERROR] App file missing
)
if exist "docs\index.html" (
    echo [OK] Docs file found (docs/index.html)
) else (
    echo [ERROR] Docs file missing
)
if exist "server.py" (
    echo [OK] Server script found (server.py)
) else (
    echo [ERROR] Server script missing
)
echo.
echo Setup verification complete.
pause
cls
goto menu

:test
cls
color 0B
echo =======================================================
echo      Starting Local Development Environment
echo =======================================================
echo.
cd /d "%~dp0"
call Start_NutriCalc_All.bat
goto menu

:status
cls
color 0B
echo =======================================================
echo      NutriCalc Deployment Status
echo =======================================================
echo.
echo GitHub Pages Deployment:
echo   Status: ✓ READY
echo   Branch: main
echo   URL: https://thynk3rbot.github.io/antigravity/static/
echo.
echo Netlify Deployment (Optional):
echo   Status: ○ REQUIRES SETUP
echo   Setup: See GITHUB_ACTIONS_SETUP.md
echo.
echo Local Development:
echo   Status: ✓ READY
echo   Command: python server.py (from tools/nutribuddy/)
echo   URL: http://localhost:8100/
echo.
echo To deploy to GitHub Pages:
echo   1. Make changes locally and test
echo   2. Commit: git commit -m "Your message"
echo   3. Push: git push origin main
echo   4. GitHub Actions will auto-deploy (2-3 minutes)
echo.
echo To set up Netlify backup:
echo   1. Read: GITHUB_ACTIONS_SETUP.md
echo   2. Create Netlify account (free)
echo   3. Generate auth token & site ID
echo   4. Store in GitHub secrets
echo.
pause
cls
goto menu

:deploy
cls
color 0C
echo =======================================================
echo      Deploy to Production (GitHub Pages)
echo =======================================================
echo.
echo GitHub Actions will handle deployment when you push to main.
echo.
echo To deploy:
echo   1. Verify changes locally: Start_NutriCalc_All.bat
echo   2. Test formula solver, docs, etc.
echo   3. Open Git Bash or command prompt
echo   4. Run:
echo      git add tools/nutribuddy/
echo      git commit -m "Your changes"
echo      git push origin main
echo.
echo   5. Check deployment progress:
echo      GitHub → Actions tab → "Deploy NutriCalc..." workflow
echo.
echo   6. Verify live deployment:
echo      https://thynk3rbot.github.io/antigravity/static/
echo.
echo Need help? See DEPLOY.md or GITHUB_ACTIONS_SETUP.md
echo.
pause
cls
goto menu

:help
cls
color 0B
echo =======================================================
echo      NutriCalc Documentation & Help
echo =======================================================
echo.
echo Quick Start Files:
echo   - README.md
echo     5-minute overview and quick start guide
echo.
echo   - DEPLOY.md
echo     Comprehensive deployment and maintenance guide
echo     * GitHub Pages setup
echo     * Netlify configuration
echo     * Local development
echo     * Troubleshooting
echo.
echo   - GITHUB_ACTIONS_SETUP.md
echo     GitHub Actions CI/CD pipeline guide
echo     * How automatic deployment works
echo     * How to set up Netlify backup
echo     * GitHub secrets configuration
echo.
echo   - DEPLOYMENT_CHECKLIST.md
echo     Production readiness checklist
echo     * Verify all components are ready
echo     * Post-deployment validation
echo     * Maintenance schedule
echo.
echo   - DEPLOYMENT_SUMMARY.md
echo     Complete project summary
echo     * What's been delivered
echo     * Live URLs
echo     * Next steps and activation
echo.
echo Common Tasks:
echo   Setup environment: Setup_NutriCalc.bat
echo   Start dev servers: Start_NutriCalc_All.bat
echo   Deploy changes: Push to GitHub (automatic)
echo   Check status: This menu → Option 3
echo.
echo Useful URLs:
echo   Local App: http://localhost:8100/static/
echo   Local Docs: http://localhost:8101/
echo   Live App: https://thynk3rbot.github.io/antigravity/static/
echo   Repository: https://github.com/thynk3rbot/antigravity
echo.
pause
cls
goto menu

:end
color 07
echo.
echo Thank you for using NutriCalc!
echo.
exit /b 0
