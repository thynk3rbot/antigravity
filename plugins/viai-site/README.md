# viai.club Website Plugin

A clean, static-first customer website for the viai.club platform. Integrated with the Magic V3 Daemon as a managed plugin.

## Features
- **Landing Page**: Modern hero section with core value props.
- **Product Showcase**: Real-time fleet status via Daemon API.
- **Customer Portal**: Success stories and testimonials.
- **Documentation**: AI-powered search via Dify RAG.

## Operational Flow
### 1. Local Development
```bash
pip install -r requirements.txt
python server.py
# View at http://localhost:8010
```

### 2. V3 Orchestration
The plugin is automatically discovered by the **Magic V3 Daemon**.
To start it via the daemon API:
```bash
curl -X POST http://localhost:8001/api/plugins/viai-site/start
```

### 3. Docker Deployment
```bash
docker build -t viai-site .
docker run -p 8010:8010 viai-site
```

## Security & Performance
- **Zero Frameworks**: Vanilla JS/CSS for ultra-fast load times.
- **Safe DOM**: No `innerHTML` usage with untrusted data.
- **Lightweight**: Alpine image under 100MB.
