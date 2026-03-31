import pytest
import sys
from pathlib import Path
from httpx import AsyncClient, ASGITransport

sys.path.insert(0, str(Path(__file__).parent.parent))
from server import app, _init_db


@pytest.fixture(autouse=True, scope="module")
async def setup_db():
    await _init_db()


@pytest.mark.asyncio
async def test_health():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as c:
        r = await c.get("/health")
    assert r.status_code == 200
    assert r.json() == {"status": "ok"}


@pytest.mark.asyncio
async def test_config_endpoint():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as c:
        r = await c.get("/api/config")
    assert r.status_code == 200
    assert "mqttBrokerUrl" in r.json()


@pytest.mark.asyncio
async def test_contact_submit():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as c:
        r = await c.post("/api/contact", json={
            "name": "Ada Lovelace",
            "email": "ada@example.com",
            "company": "Acme",
            "message": "Interested in Magic",
        })
    assert r.status_code == 200
    assert r.json()["ok"] is True


@pytest.mark.asyncio
async def test_contact_missing_email():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as c:
        r = await c.post("/api/contact", json={"name": "No email"})
    assert r.status_code == 422


@pytest.mark.asyncio
async def test_contacts_list():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as c:
        r = await c.get("/api/contacts")
    assert r.status_code == 200
    assert isinstance(r.json(), list)
