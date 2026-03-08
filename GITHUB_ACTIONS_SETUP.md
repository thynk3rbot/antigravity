# GitHub Actions Setup Guide

## Overview

The NutriCalc deployment pipeline uses GitHub Actions for automated testing and deployment to:
- **GitHub Pages** (automatic, uses built-in GITHUB_TOKEN)
- **Netlify** (requires manual secret setup)

---

## Quick Setup (5 minutes)

### Step 1: GitHub Pages (No Setup Needed ✓)

GitHub Pages deployment uses the built-in `GITHUB_TOKEN`, so **no secrets to configure**.

**Verify it works:**
1. Navigate to repo: https://github.com/thynk3rbot/antigravity
2. Go to **Settings** → **Pages**
3. Confirm **Source** is set to "GitHub Actions"
4. URL will be: `https://thynk3rbot.github.io/antigravity/`

### Step 2: Netlify (Optional but Recommended)

If you want to use Netlify as a backup deployment, configure these secrets:

#### 2a. Create Netlify Auth Token

1. Log in to https://netlify.com
2. Click your **profile icon** (top-right) → **Account settings**
3. Navigate to **Applications**
4. Click **Personal access tokens** → **New access token**
5. Name it: `github-actions-nutribuddy`
6. Copy the token (you won't see it again)

#### 2b. Get Netlify Site ID

1. Log in to https://netlify.com
2. Select the **NutriCalc site** from your dashboard (create new site if needed)
3. Go to **Site settings** → **General**
4. Copy the **Site ID** (looks like: `abc123def456`)

#### 2c. Store in GitHub Secrets

1. Navigate to repo: https://github.com/thynk3rbot/antigravity
2. Go to **Settings** → **Secrets and variables** → **Actions**
3. Click **New repository secret**
4. Add two secrets:

| Name | Value |
|------|-------|
| `NETLIFY_AUTH_TOKEN` | (Paste token from step 2a) |
| `NETLIFY_SITE_ID` | (Paste site ID from step 2b) |

5. Click **Add secret** for each

**Verify secrets are stored:**
```bash
# In terminal, navigate to repo
cd antigravity

# Secrets are encrypted and stored in GitHub
# They'll be used automatically by the workflow
```

---

## How the Workflow Works

### File

`.github/workflows/deploy-nutribuddy.yml`

### Triggers

Deployment runs automatically when:
- ✅ **Push to `main` branch** AND changes touch `tools/nutribuddy/**`
- ✅ **Pull request to `main`** (validation only, no deploy)
- ✅ **Manual trigger** (Actions tab → Run workflow)

### Stages

```
1. Validate
   ├─ Check HTML syntax
   ├─ Validate JSON files
   └─ Verify favicon/logo exist

2. Deploy to GitHub Pages (Automatic)
   ├─ Copy files to build/
   └─ Push to gh-pages branch

3. Deploy to Netlify (If secrets configured)
   ├─ Authenticate with NETLIFY_AUTH_TOKEN
   ├─ Deploy site using NETLIFY_SITE_ID
   └─ Post deployment URL

4. Test Live Deployment
   ├─ Wait for propagation
   ├─ Verify GitHub Pages is live
   └─ Check file integrity
```

---

## Monitoring Deployments

### GitHub Actions Tab

1. Go to repo → **Actions** tab
2. Select **"Deploy NutriCalc to GitHub Pages & Netlify"**
3. View latest run → Click to see detailed logs

**Status indicators:**
- ✅ Green = Passed
- ❌ Red = Failed
- ⏳ Yellow = Running
- ⏭️ Skipped = Didn't trigger

### GitHub Pages

1. Go to repo → **Settings** → **Pages**
2. Scroll to **Deployment history**
3. See latest deployment URL and status

### Netlify (If configured)

1. Log in to https://app.netlify.com
2. Select **NutriCalc site**
3. View **Deploys** tab
4. See latest deploy with status (✓ Published, ✗ Failed, ⏳ Building)

---

## Troubleshooting

### ❌ Workflow shows red (Failed)

**Check the error:**
1. Actions tab → Latest run → View logs
2. Look for specific error message

**Common fixes:**

| Error | Solution |
|-------|----------|
| `HTML validation failed` | Fix unclosed tags in `.html` files |
| `Invalid JSON` | Run `python3 -m json.tool file.json` to find syntax error |
| `favicon.svg missing` | Ensure file exists at `tools/nutribuddy/static/favicon.svg` |
| `NETLIFY_AUTH_TOKEN not found` | Configure secret in Settings → Secrets |

### ❌ GitHub Pages shows 404

**Check:**
1. Verify workflow succeeded (green ✅)
2. Go to Settings → Pages → Check source is "GitHub Actions"
3. Wait 1-2 minutes for propagation
4. Hard refresh browser (Ctrl+Shift+R)

### ❌ Netlify deploy failed

**Check:**
1. Verify `NETLIFY_AUTH_TOKEN` and `NETLIFY_SITE_ID` are set
2. Confirm Netlify site exists at https://app.netlify.com
3. Check Netlify deploy logs for specific error
4. Test token: `curl -H "Authorization: Bearer $TOKEN" https://api.netlify.com/api/v1/user`

### ⏭️ Workflow didn't trigger

**Possible reasons:**
1. Push was to branch other than `main` (workflow only runs on main)
2. Changes didn't touch `tools/nutribuddy/` (workflow is filtered)
3. Workflow file has syntax error (check `.github/workflows/deploy-nutribuddy.yml`)

**Force trigger:**
1. Actions tab → **Deploy NutriCalc...** → **Run workflow** → Choose `main` → **Run**

---

## Environment Variables

The workflow uses these GitHub variables (read-only context):

```yaml
github.repository       # thynk3rbot/antigravity
github.repository_owner # thynk3rbot
github.event.repository.name # antigravity
github.ref              # refs/heads/main
github.event_name       # push, pull_request, workflow_dispatch
github.actor            # (pushed by username)
```

These are automatically available and don't need to be configured.

---

## Testing Locally Before Pushing

To simulate the deployment locally:

```bash
# 1. Validate HTML (simulates workflow validation)
for file in tools/nutribuddy/static/*.html tools/nutribuddy/docs/*.html; do
  echo "Checking: $file"
  grep -q "</html>" "$file" && echo "  ✓ Valid" || echo "  ✗ Missing closing tag"
done

# 2. Validate JSON
python3 -m json.tool tools/nutribuddy/chemicals.json > /dev/null && echo "✓ JSON valid" || echo "✗ JSON invalid"

# 3. Check favicon/logo
ls tools/nutribuddy/static/favicon.svg && echo "✓ Favicon found"
ls tools/nutribuddy/static/logo.svg && echo "✓ Logo found"

# 4. If all pass, safe to push:
git push origin main
```

---

## Secrets Rotation (Security Best Practice)

**Every 6 months:**

1. **Regenerate Netlify Auth Token:**
   - Netlify → Account settings → Personal access tokens → Delete old → Create new
   - Update GitHub secret with new token

2. **Verify GitHub Pages token** (auto-managed by GitHub, no action needed)

3. **Audit workflow logs** for any failed authentication attempts

---

## What's Deployed

### GitHub Pages (`/static/` folder)

- `static/index.html` — Main NutriCalc app (Formula Builder, Solver, Production)
- `static/favicon.svg` — Browser tab icon
- `static/logo.svg` — App header logo
- `docs/index.html` — 6-page documentation
- `docs/diagrams.html` — Animated chemical diagrams
- `*.json` files — Chemical database, MQTT config, pricing

### Netlify

Same files as GitHub Pages, served from `https://<site>.netlify.app/`

---

## Disabling Deployments

If you need to temporarily disable automatic deployments:

**Option 1: Disable workflow**
1. Actions tab → Select workflow → ⋯ menu → **Disable**
2. To re-enable: ⋯ menu → **Enable**

**Option 2: Modify trigger condition**
Edit `.github/workflows/deploy-nutribuddy.yml`:
```yaml
on:
  # push:  # Comment this out to disable
  #   branches: [ main ]
  workflow_dispatch:  # Keep manual trigger enabled
```

Then push changes to `main` and workflow won't auto-trigger.

---

## Advanced: Custom Deployment Targets

To deploy to other hosting platforms (GitHub.io subdomain, Vercel, etc.):

1. **Fork the workflow** to `.github/workflows/deploy-custom.yml`
2. **Add new job** following the same pattern:
   ```yaml
   deploy-custom:
     name: Deploy to Custom Host
     needs: validate
     runs-on: ubuntu-latest
     if: github.event_name == 'push' && github.ref == 'refs/heads/main'
     steps:
       - uses: actions/checkout@v4
       - name: Deploy
         run: |
           # Add custom deployment command here
           # Examples: curl upload, SFTP, FTP, rsync, etc.
   ```
3. **Configure secrets** for custom host (API keys, credentials)
4. **Push** and workflow will run both deployments

---

## Support

- **Workflow file:** `.github/workflows/deploy-nutribuddy.yml`
- **Deployment guide:** `tools/nutribuddy/DEPLOY.md`
- **GitHub Actions docs:** https://docs.github.com/en/actions
- **Netlify docs:** https://docs.netlify.com/

For issues, check GitHub Actions logs or Netlify dashboard.
