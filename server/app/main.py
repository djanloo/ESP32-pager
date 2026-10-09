"""Servizio dei cercapersone: WebSocket per i dispositivi, HTTP per test e PC."""

import json
import logging
import os
import re

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from pydantic import BaseModel, Field

from app.hub import Hub

NAME_PATTERN = r"^[a-z0-9_-]{1,32}$"
_NAME_RE = re.compile(NAME_PATTERN)

log = logging.getLogger("pager.api")


class CallRequest(BaseModel):
    from_: str = Field(alias="from", pattern=NAME_PATTERN)


def create_app(hub: Hub | None = None) -> FastAPI:
    if hub is None:
        hub = Hub(cooldown_s=float(os.environ.get("CALL_COOLDOWN_S", "10")))
    app = FastAPI(title="Cercapersone")
    app.state.hub = hub

    @app.get("/health")
    async def health() -> dict:
        return {"status": "ok"}

    @app.get("/devices")
    async def devices() -> dict:
        return {"devices": hub.names()}

    @app.post("/call")
    async def call(req: CallRequest) -> dict:
        delivered = await hub.call(req.from_)
        if delivered is None:
            raise HTTPException(status_code=429, detail="troppe chiamate, riprova tra poco")
        return {"delivered": delivered}

    @app.websocket("/ws")
    async def ws(websocket: WebSocket, name: str = "") -> None:
        await websocket.accept()
        if not _NAME_RE.fullmatch(name):
            log.warning("connessione rifiutata: nome non valido %r", name)
            await websocket.close(code=1008)
            return

        await hub.register(name, websocket)
        try:
            while True:
                message = await websocket.receive()
                if message["type"] == "websocket.disconnect":
                    break
                text = message.get("text")
                if text is None:
                    log.warning("frame binario ignorato da %s", name)
                    continue
                try:
                    payload = json.loads(text)
                except json.JSONDecodeError:
                    log.warning("messaggio non JSON da %s: %r", name, text[:100])
                    continue
                if isinstance(payload, dict) and payload.get("type") == "call":
                    await hub.call(name)
                else:
                    log.warning("messaggio ignorato da %s: %r", name, text[:100])
        except (WebSocketDisconnect, RuntimeError):
            pass
        finally:
            hub.unregister(name, websocket)

    return app


logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
app = create_app()
