# RAG Router -- Universal IoT-to-RAG Routing Microservice

**Bridge Magic MQTT telemetry to Dify knowledge bases with domain-aware routing.**

Pipeline: MQTT Ingest → Domain Classification → Completeness Guard → Dify RAG → Response

## Quick Start

### 1. Setup Environment

```bash
cd tools/rag_router
cp .env.example .env
# Edit .env with your Dify API key and knowledge base IDs
```

### 2. Start the Full Stack

```bash
# Start Dify + RAG Router + MQTT + Dependencies
docker compose up -d

# Wait for services to initialize (30-60 seconds)
docker compose logs -f dify-api  # Watch Dify startup
```

### 3. Access the Query Console

Open http://localhost:8200 in your browser. You'll see:
- **Left panel**: Registered sensor nodes (auto-updated)
- **Center panel**: Query input + real-time RAG responses
- **Test presets**: Inject synthetic sensor data to test the pipeline

### 4. Upload Knowledge to Dify

1. Open http://localhost (Dify admin UI)
2. Login (default: `admin@dify.ai` / `dify-ai`)
3. Create a new **Knowledge Base** for each domain:
   - **Nutrient KB**: Upload hydroponic protocol PDFs (Bugbee, USU docs)
   - **Botanical KB**: Upload crop growth guides, VPD references
   - **Hardware KB**: Upload Magic firmware manual, schematic PDFs
4. Copy each knowledge base ID to `.env`:
   ```env
   KNOWLEDGE_ID_NUTRIENT=dataset-xxxx-yyyy-zzzz
   ```

### 5. Test End-to-End

**Via HTTP** (simplest):
```bash
curl -X POST http://localhost:8200/api/ingest-and-query \
  -H "Content-Type: application/json" \
  -d '{
    "id": "PH-01",
    "hwType": "ph-ec-sensor",
    "caps": "ph,ec,temp",
    "data": {"ph": 6.1, "ec": 1.8, "temp": 22.5}
  }'
```

**Via WebSocket** (real-time):
Open the query console at http://localhost:8200 and click "pH+EC OK" under "Test Data Injection". The RAG response will appear in real-time.

**Via MQTT** (production):
Publish sensor readings to `magic/<nodeId>/sensor/<key>`:
```bash
mosquitto_pub -h localhost -t "magic/PH-01/sensor/ph" -m "6.1"
mosquitto_pub -h localhost -t "magic/PH-01/sensor/ec" -m "1.8"
mosquitto_pub -h localhost -t "magic/PH-01/sensor/temp" -m "22.5"

# Then query the node
curl -X POST http://localhost:8200/api/query \
  -H "Content-Type: application/json" \
  -d '{"node_id": "PH-01"}'
```

---

## Architecture

### Services

| Service | Port | Purpose |
|---------|------|---------|
| **rag-router** | 8200 | FastAPI routing engine + query console |
| **dify-api** | 5001 (internal) | Dify RAG + LLM integration |
| **dify-web** | 80 | Dify admin UI (login, KB upload) |
| **emqx** | 1883 | MQTT broker |
| **postgres** | 5432 | Dify database |
| **redis** | 6379 | Dify cache + Celery broker |
| **weaviate** | 8080 | Vector embeddings database |

### Data Flow

```
MQTT Sensor Data
    ↓
RAG Router (MQTT Client)
    ↓ (aggregate by node)
SensorAggregator (in-memory state)
    ↓ (on /api/query request)
classify_domain() → hwType→{NUTRIENT,BOTANICAL,HARDWARE}
    ↓
check_completeness() → Relentless Honesty Guard
    ↓ (if failed, return "INSUFFICIENT DATA FOR X ANALYSIS")
query_dify() → Dify API with expert profile
    ↓
Dify RAG (semantic search + LLM)
    ↓ (retrieves from knowledge base)
answer + sources → broadcast to WebSocket clients
    ↓
Query Console displays real-time result
```

### Domain Classification (Fuzzy Matching)

Maps `hwType` substrings to expert domains:

| Domain | Keywords |
|--------|----------|
| NUTRIENT | ph, ec, tds, ppm, nutrient, dose, pump, reservoir |
| BOTANICAL | dht, humidity, soil, moisture, light, par, temp, co2, vpd |
| HARDWARE | relay, valve, fan, motor, flow, level, pressure, mcp |

New domains can be registered via `POST /api/products/register`.

### Relentless Honesty Guard

Before querying RAG, the router validates that all required sensor keys are present:

| Domain | Required Keys |
|--------|---|
| NUTRIENT | `ph`, `ec` |
| BOTANICAL | `temp` |
| HARDWARE | *(none)* |

If missing → returns `"INSUFFICIENT DATA FOR {DOMAIN} ANALYSIS"` with the missing keys explicitly listed.

### Expert Profiles (System Prompts)

Each domain has a hardcoded system prompt that's prepended to the Dify query:

- **NUTRIENT**: "You are a hydroponic nutrient analyst trained on the Bruce Bugbee/USU protocols..."
- **BOTANICAL**: "You are an environmental botanist. Analyze temperature, humidity, and light readings..."
- **HARDWARE**: "You are a Magic hardware diagnostics specialist..."

Custom domains can override these via `POST /api/products/register`.

---

## API Reference

### Ingest Sensor Data

**`POST /api/ingest`** — Ingest PeripheralInfo JSON (doesn't run RAG)
```json
{
  "id": "PH-01",
  "hwType": "ph-ec-sensor",
  "caps": "ph,ec,temp",
  "data": {"ph": 6.1, "ec": 1.8, "temp": 22.5}
}
```

**`POST /api/ingest-and-query`** — Ingest + immediately run RAG pipeline
Same payload, returns RAG response.

### Query RAG

**`POST /api/query`** — Query RAG for a node with optional custom question
```json
{
  "node_id": "PH-01",
  "query": "What nutrient lockouts are happening?"
}
```

**`GET /api/nodes`** — List all tracked sensor nodes (with current readings)

**`GET /api/history?limit=20`** — Fetch recent RAG query/response history

### Product Registration (Runtime Extensibility)

**`POST /api/products/register`** — Add a new product line
```json
{
  "domain": "CUSTOM_PRODUCT",
  "knowledge_id": "dataset-xxxx-yyyy-zzzz",
  "keywords": ["custom", "domain", "keywords"],
  "required_keys": ["required_sensor_1", "required_sensor_2"],
  "expert_prompt": "You are a specialist in... [custom prompt]"
}
```

**`GET /api/domains`** — List all registered expert domains and their configuration

### WebSocket (Real-time)

**`WS /ws`** — Connect for live updates. Send:
```json
{
  "action": "query",
  "node_id": "PH-01",
  "query": "optional custom question"
}
```

Receive events:
- `rag_response` — Full RAG answer with sources
- `guard_rejection` — Data validation failed
- `error` — Query error

---

## Troubleshooting

### MQTT Connection Fails
```bash
docker compose logs emqx
# Check MQTT_BROKER, MQTT_PORT in .env
```

### Dify Connection Fails
```bash
curl -s http://localhost/v1 | jq .
# Should return Dify API info
# Check DIFY_BASE_URL, DIFY_API_KEY in .env
```

### Query Returns "RAG ENGINE NOT CONFIGURED"
```bash
# Verify DIFY_API_KEY is set in .env
# Verify the API key is from a Dify **App** (not a Dataset key)
grep DIFY_API_KEY .env
```

### "INSUFFICIENT DATA FOR NUTRIENT ANALYSIS"
```bash
# pH and EC are required for nutrient domain.
# The sensor is reporting pH but not EC.
# Either: (a) add EC sensor, (b) query BOTANICAL/HARDWARE domain instead
```

### Knowledge Base Not Returning Results
```bash
# 1. Verify the knowledge base was uploaded in Dify UI
# 2. Verify the dataset ID is correct in .env
# 3. Check indexing status in Dify UI (should be "Completed")
# 4. Try a simple query manually in Dify > "Hit Testing"
```

---

## Configuration

### Environment Variables

| Variable | Default | Notes |
|----------|---------|-------|
| `DIFY_BASE_URL` | `http://localhost:5001/v1` | Dify API endpoint (change if not localhost) |
| `DIFY_API_KEY` | *(required)* | Dify App API key (get from Dify UI) |
| `MQTT_BROKER` | `localhost` | MQTT broker hostname |
| `MQTT_PORT` | `1883` | MQTT broker port |
| `RAG_ROUTER_PORT` | `8200` | RAG Router HTTP/WS port |
| `LOG_LEVEL` | `INFO` | `DEBUG`, `INFO`, `WARNING`, `ERROR` |
| `KNOWLEDGE_ID_*` | *(optional)* | Per-domain knowledge base IDs |

### Domain Extensibility

Add new expert domains **at runtime** (no code changes):

```bash
curl -X POST http://localhost:8200/api/products/register \
  -H "Content-Type: application/json" \
  -d '{
    "domain": "NUTRIENT_ADVANCED",
    "knowledge_id": "dataset-advanced-xxxx",
    "keywords": ["advanced", "nutrient", "custom"],
    "required_keys": ["ph", "ec", "ca", "k", "mg"],
    "expert_prompt": "You are a master hydroponic chemist..."
  }'
```

From then on, sensors with `hwType` containing "advanced" will route to that domain.

---

## Development

### Run Locally (Without Docker)

```bash
# 1. Install dependencies
pip install -r requirements.txt

# 2. Start Dify stack (only)
docker compose up dify-api dify-worker dify-web db redis weaviate plugin_daemon sandbox ssrf_proxy nginx emqx

# 3. Run RAG Router locally
python server.py

# 4. Access console at http://localhost:8200
```

### Testing

**Test Presets** (in query console UI):
- `pH+EC OK` — Normal nutrient readings
- `pH Low (lockout)` — pH 4.8 → should explain nutrient lockout
- `pH only (guard)` → should trigger "INSUFFICIENT DATA" (missing EC)
- `DHT22 readings` → BOTANICAL domain
- `Relay status` → HARDWARE domain

**Raw HTTP** (curl):
```bash
curl -X POST http://localhost:8200/api/ingest-and-query \
  -H "Content-Type: application/json" \
  -d '{
    "id": "test-node",
    "hwType": "dht22",
    "caps": "temp,humidity",
    "data": {"temp": 28.5, "humidity": 72.3}
  }' | jq .
```

---

## Integration with Magic Firmware

The RAG Router listens on two MQTT patterns:

1. **Per-sensor**: `magic/<nodeId>/sensor/<key> = <value>`
   - Emitted by Magic firmware for each sensor reading
   - Aggregated in-memory per node

2. **Telemetry bundle**: `magic/telemetry/<nodeId> = {key:value, ...}`
   - Optional: batch updates in one message

**Firmware Side** (already implemented in Magic):
```cpp
// When a sensor reading arrives:
MQTTManager::getInstance().publish(
  "magic/" + nodeId + "/sensor/ph",
  phReading
);
```

**Integration Checklist**:
- ✅ Firmware publishes MQTT telemetry
- ✅ RAG Router subscribes and aggregates
- ✅ MQTT broker is running (emqx service)
- ✅ Firewall allows port 1883 (MQTT)

---

## Production Deployment Checklist

- [ ] Set secure passwords in `.env` (DB, Redis, MQTT)
- [ ] Upload all knowledge bases to Dify
- [ ] Copy knowledge base IDs to `.env`
- [ ] Test end-to-end with `curl` or query console
- [ ] Monitor logs: `docker compose logs -f`
- [ ] Set up log rotation (`docker compose` default: unlimited)
- [ ] Optional: Enable TLS for Dify (`nginx.conf` SSL cert)
- [ ] Optional: Configure MQTT authentication (emqx credentials)
- [ ] Back up Dify database: `docker compose exec db pg_dump ...`

---

## FAQ

**Q: Can I use a different vector store instead of Weaviate?**
A: Yes. In `docker-compose.yml`, replace the `weaviate` service with `qdrant`, `milvus`, `chroma`, or `elasticsearch`. Update `VECTOR_STORE` in `.env`.

**Q: How do I add a new knowledge base without restarting?**
A: Use `POST /api/products/register` at runtime. No restart needed.

**Q: What if Dify is slow to respond?**
A: Check Dify logs (`docker compose logs dify-worker`). May need to scale workers or increase timeouts.

**Q: Can I run this on a Raspberry Pi?**
A: Not recommended. Weaviate + Dify require ~4GB RAM + 4 CPU cores minimum. A small Linux server (2-4GB, dual-core) is the lower bound.

**Q: Does the router cache queries?**
A: No, every query hits Dify. For caching, add Redis middleware to `server.py` (future enhancement).

---

## License & Attribution

Magic + RAG Router — Part of the [thynk3rbot/nutricalc](https://github.com/thynk3rbot/nutricalc) unified repository.

Dify integration uses the [Dify](https://dify.ai) open-source platform.

MQTT broker: [EMQX](https://www.emqx.io)

Vector store: [Weaviate](https://weaviate.io) (or your choice)

---

## Next Steps

1. **Upload Knowledge Bases**: Use Dify UI to ingest your hydroponic, botanical, and hardware PDFs
2. **Test Integration**: Send MQTT data from your Magic device or use test presets
3. **Customize Expert Prompts**: Fine-tune domain prompts in `server.py` if needed
4. **Monitor Production**: Set up alerts on query failures, slow responses

---

**Last Updated**: 2026-03-10
**Maintained By**: Claude (Magic RAG Integration)
