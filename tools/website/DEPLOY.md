# Magic Website — VPS Deployment Guide

## Prerequisites

- Ubuntu 22.04+ VPS with a public IP
- Domain pointed at the VPS (A record)
- Root or sudo access

---

## 1. System packages

```bash
sudo apt update
sudo apt install -y nginx certbot python3-certbot-nginx python3-pip
```

---

## 2. Install EMQX Community

Follow the official guide at https://www.emqx.io/downloads (choose Ubuntu).
Quick install via package repo:

```bash
curl -s https://assets.emqx.com/scripts/install-emqx-deb.sh | sudo bash
sudo systemctl enable emqx && sudo systemctl start emqx
```

EMQX management dashboard is at `http://YOUR_VPS_IP:18083` (default: admin / public).
**Change the password immediately.**

Add device credentials via EMQX dashboard: Authentication → Add user.

---

## 3. Python dependencies

```bash
cd /opt/magic-website   # or wherever you deploy the repo
pip install -r tools/website/requirements.txt
```

---

## 4. Nginx config

```bash
sudo cp tools/website/nginx/magic.conf /etc/nginx/sites-available/magic
# Edit the file — replace YOUR_DOMAIN with your domain
sudo nano /etc/nginx/sites-available/magic

sudo ln -s /etc/nginx/sites-available/magic /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

---

## 5. TLS with Certbot

```bash
sudo certbot --nginx -d YOUR_DOMAIN
# Follow prompts — Certbot auto-patches your Nginx config with cert paths.
```

---

## 6. Start FastAPI

Set the MQTT broker URL to the WebSocket path proxied by Nginx:

```bash
export MQTT_BROKER_URL="wss://YOUR_DOMAIN/mqtt"
cd tools/website
uvicorn server:app --host 0.0.0.0 --port 8001
```

---

## 7. Systemd service (recommended)

Create `/etc/systemd/system/magic-web.service`:

```ini
[Unit]
Description=Magic Corporate Website
After=network.target

[Service]
User=www-data
WorkingDirectory=/opt/magic-website/tools/website
Environment="MQTT_BROKER_URL=wss://YOUR_DOMAIN/mqtt"
ExecStart=/usr/local/bin/uvicorn server:app --host 0.0.0.0 --port 8001
Restart=always

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable magic-web
sudo systemctl start magic-web
```

---

## 8. Verify

```bash
curl https://YOUR_DOMAIN/health
# Expected: {"status":"ok"}
```

Open `https://YOUR_DOMAIN/dashboard` — you should see the MQTT dashboard
connecting to `wss://YOUR_DOMAIN/mqtt`.

---

## Local dev with public EMQX test broker

```bash
MQTT_BROKER_URL=wss://broker.emqx.io:8084/mqtt python tools/website/server.py
# open http://localhost:8001
```

## Local dev with Docker EMQX

```bash
docker run -d --name emqx -p 1883:1883 -p 8083:8083 -p 18083:18083 emqx/emqx
MQTT_BROKER_URL=ws://localhost:8083/mqtt python tools/website/server.py
```
