# Marketing Plan Prompt — LoRaLink-AnyToAny

Paste the following prompt into Claude, ChatGPT, Gemini, or any capable AI to generate a comprehensive go-to-market plan.

---

## The Prompt

```
You are an expert B2B and maker-market product strategist with deep experience
in embedded systems, IoT, and developer tools. I need a comprehensive go-to-market
marketing plan for a technical firmware product.

## The Product

**LoRaLink-AnyToAny** is a proprietary ESP32-S3 firmware framework that enables
true any-to-any wireless protocol routing. Key facts:

- Runs on Heltec WiFi LoRa 32 V3 (~$20–30/node)
- Simultaneously bridges: LoRa (1–10km), ESP-NOW (~200m), BLE (~30m), WiFi/HTTP, Serial, MQTT
- Central CommandManager routes commands from ANY interface to ANY other
- AES-128 encrypted LoRa with TTL-based mesh (up to 3 hops), ACK/retry delivery
- Web dashboard, REST API, OTA updates, NVS persistence
- Dynamic GPIO/relay scheduling at runtime via JSON over any interface
- Relay control: 4 outputs (1× 110V, 3× 12V)
- DHT22 sensor support with safety-threshold automation
- Node tracking: RSSI, battery, uptime, GPS coords, hop count
- Firmware v1.5.0 — proprietary, © 2026 Steven P Williams (spw1.com)

## Infrastructure & Hosting Roadmap (Separate Project)

The system is currently demoed via temporary Cloudflare Quick Tunnels, but the roadmap includes:
- **Phase 1**: Migrating `spw1.com` and `viai.club` to Cloudflare DNS for branded access.
- **Phase 2**: Permanent Cloudflare Zero-Trust Tunnels (`app.spw1.com`, `docs.spw1.com`).
- **Phase 3**: Implementing SSO/MFA for secure remote fleet administration.
- **Phase 4**: Dockerized MQTT and WebApp deployment for high availability.

## The Creator

Solo indie developer / embedded engineer. Has a working product and a GitHub
repository. Limited marketing budget — needs high-ROI channels. Open to licensing,
commercial sales, and/or consulting engagements built around the framework.

## The Market Segments (prioritize these three)

1. **Makers & Hobbyists** — Arduino/ESP32 community, Hackaday readers, r/esp32,
   PlatformIO users. Want: easy setup, good docs, active community.

2. **Industrial IoT Integrators** — Small to mid-size firms building custom
   monitoring/control solutions. Want: proven reliability, AES security, REST API,
   MQTT compatibility, relay I/O.

3. **Smart Agriculture / Remote Monitoring** — Precision farming, environmental
   sensing, no-cellular deployments. Want: long-range LoRa mesh, solar + deep-sleep,
   sensor integration, easy dashboard.

## Deliverables I Need

Please create a detailed go-to-market marketing plan that includes:

### 1. Positioning & Messaging
- One clear positioning statement (under 25 words)
- Three distinct value propositions per target segment
- Tone and voice guidelines
- Key phrases / power words that resonate in each segment

### 2. Channel Strategy
- Ranked list of 8–12 specific channels (e.g., Hackster.io, r/esp32, PlatformIO registry,
  LinkedIn, YouTube, GitHub Sponsors, direct B2B outreach)
- For each: why it fits, content type, posting cadence, expected ROI
- Which channels to prioritize in first 90 days vs. months 4–12

### 3. Content Plan (90-day calendar)
- Week-by-week content topics
- Content formats: technical tutorials, demo videos, comparison posts, case studies
- At least 3 specific article/post titles with hook sentences

### 4. Community & Developer Relations
- Which forums/communities to join and how to add value before promoting
- Open-source vs. proprietary strategy recommendation with rationale
- GitHub repo optimization checklist

### 5. Monetization Strategy
- Recommended pricing tiers (e.g., personal/non-commercial free, commercial license,
  enterprise support, consulting day rate)
- Licensing model recommendation with rationale
- How to package a "starter kit" hardware + firmware bundle concept

### 6. Launch Plan
- A phased 3-month launch sequence with specific milestones
- "First 100 users" acquisition tactics
- Success metrics and KPIs for each phase

### 7. SEO & Discoverability
- 10 high-value long-tail keywords to target
- GitHub/README optimization tips
- Forum/community signature/profile recommendations

### 8. Risk & Mitigation
- Top 3 risks to market traction
- Mitigation strategy for each

Be specific and tactical. Avoid generic advice. Assume the creator has 5–10 hours/week
to spend on marketing. All tactics should be executable by a solo developer without
a marketing team or large budget.
```

---

## How to Use This Prompt

1. Copy everything inside the triple-backtick block above
2. Paste into your preferred AI assistant
3. Optionally append: *"Focus especially on [segment name]"* to narrow the output
4. Follow up with: *"Now give me the 5 highest-leverage actions for the first 30 days"*

## Suggested Follow-Up Prompts

After receiving the plan, extract maximum value with these follow-ups:

- *"Write the actual positioning statement and tagline options (give me 5 variations)"*
- *"Draft the GitHub README hero section using the positioning you recommended"*
- *"Write a Hackster.io project post introduction for the smart agriculture use case"*
- *"Create a comparison table: LoRaLink-AnyToAny vs. [competitor] for an industrial buyer"*
- *"Draft a cold outreach email to a small IoT integrator firm (under 150 words)"*
- *"Generate 10 tweet-length hooks for the any-to-any routing concept"*
- *"Write a PlatformIO library description (under 300 words)"*
