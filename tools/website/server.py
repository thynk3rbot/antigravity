#!/usr/bin/env python3
"""
server.py — Magic Corporate Website Backend

Usage:
    python tools/website/server.py
    # Then open: http://localhost:8001
"""
from __future__ import annotations

import os
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

import aiosqlite
import uvicorn
from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, EmailStr

BASE = Path(__file__).parent
DB_PATH = BASE / "db" / "contacts.db"
STATIC = BASE / "static"


async def _init_db():
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute("""
            CREATE TABLE IF NOT EXISTS contacts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                email TEXT NOT NULL,
                company TEXT,
                message TEXT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        await db.commit()


@asynccontextmanager
async def lifespan(app: FastAPI):
    await _init_db()
    yield


app = FastAPI(title="Magic Website", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=STATIC), name="static")


@app.get("/health")
async def health():
    return {"status": "ok"}


@app.get("/api/config")
async def client_config():
    return {"mqttBrokerUrl": os.environ.get("MQTT_BROKER_URL", "ws://localhost:8083/mqtt")}


class ContactForm(BaseModel):
    name: str
    email: EmailStr
    company: Optional[str] = None
    message: Optional[str] = None


@app.post("/api/contact")
async def submit_contact(form: ContactForm):
    async with aiosqlite.connect(DB_PATH) as db:
        await db.execute(
            "INSERT INTO contacts (name, email, company, message) VALUES (?, ?, ?, ?)",
            (form.name, form.email, form.company, form.message),
        )
        await db.commit()
    return {"ok": True}


@app.get("/api/contacts")
async def list_contacts():
    async with aiosqlite.connect(DB_PATH) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT id, name, email, company, message, created_at FROM contacts ORDER BY created_at DESC"
        ) as cur:
            rows = await cur.fetchall()
    return [dict(r) for r in rows]


@app.get("/")
async def index():
    return FileResponse(STATIC / "index.html")


@app.get("/features")
async def features():
    return FileResponse(STATIC / "features.html")


@app.get("/contact")
async def contact_page():
    return FileResponse(STATIC / "contact.html")


@app.get("/dashboard")
async def dashboard():
    return FileResponse(STATIC / "dashboard.html")


@app.get("/docs")
async def docs_redirect():
    return FileResponse(STATIC / "docs-redirect.html")


if __name__ == "__main__":
    uvicorn.run("server:app", host="0.0.0.0", port=8010, reload=True)
