# Cercapersone per la pausa — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cercapersone ESP32-S3 che, alla pressione del pulsante, fa lampeggiare tutti gli altri cercapersone tramite un servizio FastAPI nel pod podman aziendale.

**Architecture:** Server FastAPI con un `Hub` in memoria (registro nome → WebSocket, broadcast escluso il mittente, anti-spam) ed endpoint `/ws`, `/call`, `/health`, `/devices`. Firmware ESP-IDF diviso in componenti: logica pura (`pager_core`: protocollo JSON e macchina a stati, testata sul PC) e componenti hardware/rete (`notifier`, `input`, `wifi_conn`, `pager_link`) collegati da `main` tramite eventi e una coda.

**Tech Stack:** Python 3.12, FastAPI, uvicorn, pytest; ESP-IDF v5.5.2 (C), `espressif/esp_websocket_client` ^1.8.0, `espressif/led_strip` ^3.0.0, cJSON, Unity.

**Spec:** `docs/superpowers/specs/2026-10-09-cercapersone-design.md`

## Global Constraints

- Nome dispositivo: regex `^[a-z0-9_-]{1,32}$`, uguale su server e firmware.
- Protocollo WS: dispositivo → server `{"type":"call"}`; server → dispositivi `{"type":"call","from":"<nome>","ts":<unix s>}`; campi/tipi sconosciuti ignorati.
- Nome non valido su `/ws`: il server chiude con codice `1008`. Nome duplicato: la vecchia connessione viene chiusa con `1000`.
- Anti-spam: `CALL_COOLDOWN_S` (default 10) per mittente, condiviso fra WS e `POST /call`; `POST /call` bloccata → `429`.
- Keepalive: ping WS ogni 20 s (client e server); connessione morta rilevata entro 60 s.
- Riconnessione firmware: backoff esponenziale da 1 s a 30 s.
- Avviso: arancione lampeggiante 10 s (`PAGER_ALERT_MS`). Rosso fisso dopo 60 s senza server (`PAGER_OFFLINE_ERROR_MS`).
- LED WS2812 su GPIO48, pulsante BOOT su GPIO0 (attivo basso), entrambi configurabili in Kconfig.
- Nessuna autenticazione e nessun TLS in v1.
- ESP-IDF v5.5.2 in `~/.espressif/v5.5.2/esp-idf`; si usa **sempre** tramite `firmware/idf.sh` (il PATH di sistema non contiene `idf.py`).
- I test host del firmware (`firmware/test_host`) si eseguono in una shell **senza** ambiente ESP-IDF attivato (con l'ambiente attivo il `gcc` di sistema usa l'assembler sbagliato).
- Porta seriale: `/dev/ttyACM0` (porta UART della scheda, chip CH343).
- Commenti e log in italiano, identificatori in inglese.
- Ogni commit termina con la riga `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

## Review Focus

1. **Nome dispositivo non valido** (maiuscole, spazi, >32 caratteri, vuoto): il server chiude con 1008 e il firmware deve accorgersene subito all'avvio con un errore chiaro e LED rosso, non riprovare all'infinito in blu. Test: Task 2 (`test_invalid_name_is_rejected`), Task 5 (`test_valid_name`), Task 9 step manuale.
2. **Messaggio di chiamata incompleto o sporco** (manca `from`, `from` non stringa o troppo lungo, buffer non terminato da `\0`, JSON malformato): il dispositivo deve comunque mostrare l'avviso se `type` è `call` e non deve mai andare in crash. Test: Task 5.
3. **Anti-spam condiviso fra WS e HTTP**: chi chiama dal dispositivo e subito dopo con `curl` con lo stesso nome deve ricevere 429. Test: Task 2 (`test_cooldown_is_shared_between_ws_and_http`).
4. **Overflow del contatore millisecondi** (`uint32_t` dopo ~49 giorni di accensione): l'avviso deve comunque spegnersi dopo 10 s. Test: Task 6 (`test_alert_timeout_across_wraparound`).
5. **Riavvio del server con dispositivi connessi**: i dispositivi tornano blu e poi si riconnettono da soli entro 30 s. Test: Task 9 step end-to-end.

---

## File Structure

```
.gitignore                                   (modifica: voci firmware)
server/
  requirements.txt                           dipendenze runtime
  requirements-dev.txt                       + pytest, httpx
  pytest.ini
  app/__init__.py
  app/hub.py                                 Hub: registro, broadcast, anti-spam
  app/main.py                                app FastAPI ed endpoint
  tests/test_hub.py
  tests/test_api.py
  tools/fake_pager.py                        cercapersone finto per test manuali
  Containerfile
  README.md                                  istruzioni per il gestore del pod
firmware/
  CMakeLists.txt
  idf.sh                                     wrapper di idf.py con ambiente attivato
  sdkconfig.defaults
  sdkconfig.local.example                    modello per credenziali/nome (copia non versionata)
  tools/serial_log.py                        legge il log seriale dopo un reset
  README.md
  main/CMakeLists.txt
  main/Kconfig.projbuild                     opzioni "Cercapersone"
  main/main.c
  components/pager_core/                     logica pura (nessuna dipendenza hardware)
    CMakeLists.txt
    include/pager_proto.h, pager_proto.c     parsing/creazione messaggi
    include/pager_app.h,   pager_app.c       macchina a stati
  components/notifier/                       LED WS2812
    CMakeLists.txt, idf_component.yml, include/notifier.h, notifier.c
  components/input/                          pulsante con debounce
    CMakeLists.txt, include/input.h, input.c
  components/wifi_conn/                      Wi-Fi station con riconnessione
    CMakeLists.txt, include/wifi_conn.h, wifi_conn.c
  components/pager_link/                     client WebSocket verso il server
    CMakeLists.txt, idf_component.yml, include/pager_link.h, pager_link.c
  test_host/                                 test Unity sul PC
    Makefile, test_pager_proto.c, test_pager_app.c
```

---

### Task 1: Server — Hub (registro, broadcast, anti-spam)

**Files:**
- Create: `server/requirements.txt`, `server/requirements-dev.txt`, `server/pytest.ini`, `server/app/__init__.py`, `server/app/hub.py`
- Test: `server/tests/test_hub.py`

**Interfaces:**
- Consumes: niente.
- Produces:
  - `class Connection(Protocol)`: `async send_json(data) -> None`, `async close(code: int = 1000) -> None` (compatibile con `starlette.websockets.WebSocket`).
  - `class Hub(cooldown_s: float = 10.0, clock: Callable[[], float] = time.monotonic, wall_clock: Callable[[], float] = time.time)`
  - `Hub.names() -> list[str]` (ordinati)
  - `async Hub.register(name: str, conn: Connection) -> None`
  - `Hub.unregister(name: str, conn: Connection) -> None`
  - `async Hub.call(from_name: str) -> int | None` (numero di consegne, `None` se bloccata dall'anti-spam)

- [ ] **Step 1: Crea l'ambiente Python**

`server/requirements.txt`:
```
fastapi>=0.115
uvicorn[standard]>=0.30
```

`server/requirements-dev.txt`:
```
-r requirements.txt
pytest>=8
httpx>=0.27
```

`server/pytest.ini`:
```ini
[pytest]
pythonpath = .
testpaths = tests
```

`server/app/__init__.py`: file vuoto.

Run:
```bash
cd server && python3 -m venv .venv && .venv/bin/pip install -q -r requirements-dev.txt
```
Expected: termina senza errori.

- [ ] **Step 2: Scrivi i test che falliscono**

`server/tests/test_hub.py`:
```python
import asyncio

from app.hub import Hub


class FakeConn:
    def __init__(self, fail: bool = False) -> None:
        self.sent: list = []
        self.closed_code: int | None = None
        self.fail = fail

    async def send_json(self, data) -> None:
        if self.fail:
            raise RuntimeError("connessione persa")
        self.sent.append(data)

    async def close(self, code: int = 1000) -> None:
        self.closed_code = code


class FakeClock:
    def __init__(self, t: float = 1000.0) -> None:
        self.t = t

    def __call__(self) -> float:
        return self.t


def make_hub(clock=None, cooldown_s: float = 10.0) -> Hub:
    return Hub(cooldown_s=cooldown_s, clock=clock or FakeClock(), wall_clock=lambda: 1760000000.7)


def test_call_reaches_everyone_except_sender():
    hub = make_hub()
    a, b, c = FakeConn(), FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a)
        await hub.register("b", b)
        await hub.register("c", c)
        return await hub.call("a")

    assert asyncio.run(scenario()) == 2
    assert a.sent == []
    assert b.sent == [{"type": "call", "from": "a", "ts": 1760000000}]
    assert c.sent == b.sent


def test_call_from_unconnected_name_reaches_everyone():
    hub = make_hub()
    a, b = FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a)
        await hub.register("b", b)
        return await hub.call("pc")

    assert asyncio.run(scenario()) == 2
    assert a.sent[0]["from"] == "pc"


def test_cooldown_blocks_repeated_calls_from_same_sender():
    clock = FakeClock()
    hub = make_hub(clock=clock)
    b = FakeConn()

    async def scenario():
        await hub.register("b", b)
        first = await hub.call("a")
        clock.t += 9.9
        second = await hub.call("a")
        clock.t += 0.1
        third = await hub.call("a")
        return first, second, third

    assert asyncio.run(scenario()) == (1, None, 1)
    assert len(b.sent) == 2


def test_cooldown_is_per_sender():
    hub = make_hub()
    c = FakeConn()

    async def scenario():
        await hub.register("c", c)
        return await hub.call("a"), await hub.call("b")

    assert asyncio.run(scenario()) == (1, 1)


def test_duplicate_name_replaces_previous_connection():
    hub = make_hub()
    a1, a2, b = FakeConn(), FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a1)
        await hub.register("a", a2)
        await hub.register("b", b)
        await hub.call("b")

    asyncio.run(scenario())
    assert a1.closed_code == 1000
    assert a1.sent == []
    assert len(a2.sent) == 1
    assert hub.names() == ["a", "b"]


def test_unregister_of_replaced_connection_keeps_new_one():
    hub = make_hub()
    a1, a2 = FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a1)
        await hub.register("a", a2)

    asyncio.run(scenario())
    hub.unregister("a", a1)
    assert hub.names() == ["a"]
    hub.unregister("a", a2)
    assert hub.names() == []


def test_failed_send_removes_device_and_continues():
    hub = make_hub()
    broken, ok = FakeConn(fail=True), FakeConn()

    async def scenario():
        await hub.register("broken", broken)
        await hub.register("ok", ok)
        return await hub.call("pc")

    assert asyncio.run(scenario()) == 1
    assert len(ok.sent) == 1
    assert hub.names() == ["ok"]
```

- [ ] **Step 3: Verifica che falliscano**

Run: `cd server && .venv/bin/pytest tests/test_hub.py -v`
Expected: FAIL / ERROR con `ModuleNotFoundError: No module named 'app.hub'`

- [ ] **Step 4: Implementa `Hub`**

`server/app/hub.py`:
```python
"""Registro dei cercapersone connessi e inoltro delle chiamate."""

import logging
import time
from typing import Any, Callable, Protocol

log = logging.getLogger("pager.hub")


class Connection(Protocol):
    async def send_json(self, data: Any) -> None: ...

    async def close(self, code: int = 1000) -> None: ...


class Hub:
    def __init__(
        self,
        cooldown_s: float = 10.0,
        clock: Callable[[], float] = time.monotonic,
        wall_clock: Callable[[], float] = time.time,
    ) -> None:
        self._cooldown_s = cooldown_s
        self._clock = clock
        self._wall_clock = wall_clock
        self._conns: dict[str, Connection] = {}
        self._last_call: dict[str, float] = {}

    def names(self) -> list[str]:
        return sorted(self._conns)

    async def register(self, name: str, conn: Connection) -> None:
        old = self._conns.get(name)
        self._conns[name] = conn
        log.info("connesso: %s", name)
        if old is not None and old is not conn:
            log.info("sostituita la connessione precedente di %s", name)
            try:
                await old.close(code=1000)
            except Exception:
                pass

    def unregister(self, name: str, conn: Connection) -> None:
        if self._conns.get(name) is conn:
            del self._conns[name]
            log.info("disconnesso: %s", name)

    async def call(self, from_name: str) -> int | None:
        now = self._clock()
        last = self._last_call.get(from_name)
        if last is not None and now - last < self._cooldown_s:
            log.info("chiamata di %s ignorata (anti-spam)", from_name)
            return None
        self._last_call[from_name] = now

        message = {"type": "call", "from": from_name, "ts": int(self._wall_clock())}
        delivered = 0
        for name, conn in list(self._conns.items()):
            if name == from_name:
                continue
            try:
                await conn.send_json(message)
                delivered += 1
            except Exception as exc:
                log.warning("invio a %s fallito (%s): rimosso", name, exc)
                self.unregister(name, conn)
        log.info("chiamata di %s consegnata a %d dispositivi", from_name, delivered)
        return delivered
```

- [ ] **Step 5: Verifica che passino**

Run: `cd server && .venv/bin/pytest tests/test_hub.py -v`
Expected: 7 passed

- [ ] **Step 6: Commit**

```bash
git add server/requirements.txt server/requirements-dev.txt server/pytest.ini server/app/__init__.py server/app/hub.py server/tests/test_hub.py
git commit -m "server: Hub con broadcast, anti-spam e sostituzione nomi duplicati

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Server — API FastAPI

**Files:**
- Create: `server/app/main.py`
- Test: `server/tests/test_api.py`

**Interfaces:**
- Consumes: `Hub` da Task 1 (`names`, `register`, `unregister`, `call`).
- Produces:
  - `create_app(hub: Hub | None = None) -> FastAPI` (se `hub` è `None` legge `CALL_COOLDOWN_S` dall'ambiente)
  - `app` a livello di modulo (usato da `uvicorn app.main:app`)
  - `NAME_PATTERN = r"^[a-z0-9_-]{1,32}$"`
  - Endpoint: `GET /health` → `{"status":"ok"}`; `GET /devices` → `{"devices":[...]}`; `POST /call` body `{"from": str}` → `{"delivered": int}` / 429 / 422; WS `/ws?name=`.

- [ ] **Step 1: Scrivi i test che falliscono**

`server/tests/test_api.py`:
```python
import time

import pytest
from fastapi.testclient import TestClient
from starlette.websockets import WebSocketDisconnect

from app.hub import Hub
from app.main import create_app


@pytest.fixture
def client():
    with TestClient(create_app(Hub(cooldown_s=10))) as c:
        yield c


def wait_devices(client: TestClient, expected: list[str]) -> None:
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        if client.get("/devices").json()["devices"] == expected:
            return
        time.sleep(0.01)
    raise AssertionError(f"dispositivi attesi {expected}, trovati {client.get('/devices').json()}")


def test_health(client):
    assert client.get("/health").json() == {"status": "ok"}


def test_devices_lists_connected_and_forgets_disconnected(client):
    assert client.get("/devices").json() == {"devices": []}
    with client.websocket_connect("/ws?name=b"), client.websocket_connect("/ws?name=a"):
        wait_devices(client, ["a", "b"])
    wait_devices(client, [])


def test_ws_call_reaches_others_but_not_sender(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        a.send_json({"type": "call"})
        msg = b.receive_json()
        assert msg["type"] == "call" and msg["from"] == "a" and isinstance(msg["ts"], int)
        # Se "a" avesse ricevuto la propria chiamata, il primo messaggio sarebbe quello.
        b.send_json({"type": "call"})
        assert a.receive_json()["from"] == "b"


def test_http_call_delivers_to_connected_devices(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        resp = client.post("/call", json={"from": "pc"})
        assert resp.status_code == 200
        assert resp.json() == {"delivered": 2}
        assert a.receive_json()["from"] == "pc"
        assert b.receive_json()["from"] == "pc"


def test_http_call_excludes_device_with_same_name(client):
    with client.websocket_connect("/ws?name=a"), client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        assert client.post("/call", json={"from": "a"}).json() == {"delivered": 1}
        assert b.receive_json()["from"] == "a"


def test_cooldown_is_shared_between_ws_and_http(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        a.send_json({"type": "call"})
        assert b.receive_json()["from"] == "a"
        assert client.post("/call", json={"from": "a"}).status_code == 429


@pytest.mark.parametrize("query", ["", "?name=", "?name=Giulia", "?name=a%20b", "?name=" + "x" * 33])
def test_invalid_name_is_rejected(client, query):
    with client.websocket_connect("/ws" + query) as ws:
        with pytest.raises(WebSocketDisconnect) as exc:
            ws.receive_json()
    assert exc.value.code == 1008
    assert client.get("/devices").json() == {"devices": []}


@pytest.mark.parametrize("body", [{}, {"from": ""}, {"from": "Giulia"}, {"from": "x" * 33}, {"from": 3}])
def test_http_call_with_invalid_from_is_422(client, body):
    assert client.post("/call", json=body).status_code == 422


def test_duplicate_name_closes_previous_connection(client):
    with client.websocket_connect("/ws?name=a") as a1:
        wait_devices(client, ["a"])
        with client.websocket_connect("/ws?name=a") as a2:
            with pytest.raises(WebSocketDisconnect) as exc:
                a1.receive_json()
            assert exc.value.code == 1000
            wait_devices(client, ["a"])
            assert client.post("/call", json={"from": "pc"}).json() == {"delivered": 1}
            assert a2.receive_json()["from"] == "pc"


def test_malformed_messages_are_ignored_and_connection_stays_open(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        a.send_text("non è json")
        a.send_text('{"type": "boh"}')
        a.send_text("[1, 2]")
        a.send_bytes(b"\x00\x01")
        a.send_json({"type": "call", "extra": 1})
        assert b.receive_json()["from"] == "a"
```

- [ ] **Step 2: Verifica che falliscano**

Run: `cd server && .venv/bin/pytest tests/test_api.py -v`
Expected: ERROR con `ModuleNotFoundError: No module named 'app.main'`

- [ ] **Step 3: Implementa l'app**

`server/app/main.py`:
```python
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
```

- [ ] **Step 4: Verifica che passino tutti i test del server**

Run: `cd server && .venv/bin/pytest -v`
Expected: tutti passed (7 di `test_hub.py` + 18 di `test_api.py`)

- [ ] **Step 5: Commit**

```bash
git add server/app/main.py server/tests/test_api.py
git commit -m "server: endpoint /ws, /call, /health, /devices

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Server — container, istruzioni e cercapersone finto

**Files:**
- Create: `server/Containerfile`, `server/README.md`, `server/tools/fake_pager.py`

**Interfaces:**
- Consumes: `app.main:app` da Task 2.
- Produces:
  - Comando di avvio del server (anche in locale): `uvicorn app.main:app --host 0.0.0.0 --port 8000 --ws-ping-interval 20 --ws-ping-timeout 20`
  - `server/tools/fake_pager.py [--url ws://HOST:8000/ws] [--name finto] [--call] [--seconds 30]`: si connette, con `--call` invia una chiamata, stampa `ricevuto: <json>` per ogni messaggio.

- [ ] **Step 1: Scrivi il Containerfile**

`server/Containerfile`:
```dockerfile
FROM docker.io/library/python:3.12-slim

WORKDIR /srv
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY app ./app

ENV PORT=8000 \
    CALL_COOLDOWN_S=10
EXPOSE 8000

# Rispettato da podman solo con --format docker (vedi README).
HEALTHCHECK --interval=30s --timeout=3s \
    CMD python -c "import os, urllib.request; urllib.request.urlopen('http://127.0.0.1:%s/health' % os.environ['PORT'])"

CMD ["sh", "-c", "exec uvicorn app.main:app --host 0.0.0.0 --port ${PORT} --ws-ping-interval 20 --ws-ping-timeout 20"]
```

- [ ] **Step 2: Scrivi il cercapersone finto**

`server/tools/fake_pager.py`:
```python
"""Cercapersone finto per test manuali: si connette, stampa le chiamate ricevute,
con --call ne invia una appena connesso."""

import argparse
import asyncio
import json

import websockets


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="ws://127.0.0.1:8000/ws")
    parser.add_argument("--name", default="finto")
    parser.add_argument("--call", action="store_true", help="invia una chiamata appena connesso")
    parser.add_argument("--seconds", type=float, default=30, help="per quanto restare in ascolto")
    args = parser.parse_args()

    async with websockets.connect(f"{args.url}?name={args.name}") as ws:
        print(f"connesso come {args.name}", flush=True)
        if args.call:
            await ws.send(json.dumps({"type": "call"}))
            print("chiamata inviata", flush=True)
        try:
            async with asyncio.timeout(args.seconds):
                async for raw in ws:
                    print(f"ricevuto: {raw}", flush=True)
        except TimeoutError:
            pass


asyncio.run(main())
```

- [ ] **Step 3: Scrivi il README per il gestore del pod**

`server/README.md`:
````markdown
# Server dei cercapersone

Servizio FastAPI che inoltra le chiamate "pausa" tra i cercapersone ESP32.
Stato solo in memoria: al riavvio i dispositivi si riconnettono da soli.

## Build e avvio con podman

```bash
podman build --format docker -t cercapersone-server -f Containerfile .
podman run -d --name cercapersone --pod <nome-pod> cercapersone-server
```

Il pod deve pubblicare la porta 8000 (es. `podman pod create --name <nome-pod> -p 8000:8000`).
`--format docker` serve perché podman rispetti l'`HEALTHCHECK`.

Log: `podman logs -f cercapersone` (connessioni, disconnessioni, chiamate).

## Configurazione

| Variabile | Default | Significato |
|---|---|---|
| `PORT` | `8000` | Porta di ascolto nel container |
| `CALL_COOLDOWN_S` | `10` | Secondi minimi tra due chiamate dello stesso mittente |

## Endpoint

| Metodo | Percorso | Descrizione |
|---|---|---|
| WS | `/ws?name=<nome>` | Connessione dei cercapersone. Nome: `[a-z0-9_-]`, 1–32 caratteri |
| POST | `/call` | Body `{"from":"<nome>"}` → `{"delivered": n}`; `429` se troppo ravvicinata |
| GET | `/devices` | Cercapersone connessi |
| GET | `/health` | `{"status":"ok"}` |

Esempi:

```bash
curl http://HOST:8000/devices
curl -X POST http://HOST:8000/call -H 'Content-Type: application/json' -d '{"from":"pc"}'
```

## Sviluppo locale

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt
.venv/bin/pytest
.venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000 --ws-ping-interval 20 --ws-ping-timeout 20
.venv/bin/python tools/fake_pager.py --name finto       # in un altro terminale
```
````

- [ ] **Step 4: Verifica il server reale in locale**

Run (avvia il server in background, poi prova gli endpoint e due cercapersone finti):
```bash
cd server
.venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 8000 --ws-ping-interval 20 --ws-ping-timeout 20 > /tmp/claude-server.log 2>&1 &
SERVER_PID=$!
sleep 2
curl -s http://127.0.0.1:8000/health
.venv/bin/python tools/fake_pager.py --name ascolta --seconds 4 > /tmp/claude-ascolta.log &
sleep 1
.venv/bin/python tools/fake_pager.py --name chiama --call --seconds 1
curl -s -X POST http://127.0.0.1:8000/call -H 'Content-Type: application/json' -d '{"from":"pc"}'
sleep 3
cat /tmp/claude-ascolta.log
kill $SERVER_PID
```
(Se il piano è eseguito in Claude Code, usa la scratchpad della sessione al posto di `/tmp` per i log.)

Expected:
- `{"status":"ok"}`
- `{"delivered":1}` dalla `POST /call` (solo `ascolta` è connesso in quel momento)
- in `ascolta.log`: `connesso come ascolta`, poi `ricevuto: {"type":"call","from":"chiama",...}` e `ricevuto: {"type":"call","from":"pc",...}`

- [ ] **Step 5: Verifica la build del container (solo se podman è installato)**

Run: `command -v podman && cd server && podman build --format docker -t cercapersone-server -f Containerfile .`
Expected: se podman non è installato il comando non stampa nulla: segnalalo nel report (la build verrà verificata dal gestore del pod). Altrimenti la build termina con `Successfully tagged`.

- [ ] **Step 6: Commit**

```bash
git add server/Containerfile server/README.md server/tools/fake_pager.py
git commit -m "server: Containerfile, README e cercapersone finto per i test

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Firmware — progetto ESP-IDF di base

**Files:**
- Create: `firmware/CMakeLists.txt`, `firmware/idf.sh`, `firmware/sdkconfig.defaults`, `firmware/sdkconfig.local.example`, `firmware/main/CMakeLists.txt`, `firmware/main/Kconfig.projbuild`, `firmware/main/main.c`, `firmware/tools/serial_log.py`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: niente.
- Produces:
  - `firmware/idf.sh <argomenti idf.py>`: esegue `idf.py` con l'ambiente ESP-IDF v5.5.2.
  - Macro Kconfig: `CONFIG_PAGER_WIFI_SSID`, `CONFIG_PAGER_WIFI_PASSWORD`, `CONFIG_PAGER_SERVER_URL`, `CONFIG_PAGER_DEVICE_NAME` (stringhe), `CONFIG_PAGER_LED_GPIO` (int, 48), `CONFIG_PAGER_BUTTON_GPIO` (int, 0).
  - `firmware/sdkconfig.local` (non versionato) applicato come default aggiuntivo.
  - `~/.espressif/tools/python/v5.5.2/venv/bin/python firmware/tools/serial_log.py [porta] [secondi]`: resetta la scheda e stampa il log seriale.

- [ ] **Step 1: Crea i file di progetto**

`firmware/idf.sh`:
```bash
#!/usr/bin/env bash
# Esegue idf.py con l'ambiente ESP-IDF v5.5.2 attivato.
set -e
. "$HOME/.espressif/tools/activate_idf_v5.5.2.sh" > /dev/null
exec python "$IDF_PATH/tools/idf.py" "$@"
```
Run: `chmod +x firmware/idf.sh`

`firmware/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)

# Credenziali e nome del dispositivo stanno in sdkconfig.local (non versionato).
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.local")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cercapersone)
```

`firmware/sdkconfig.defaults`:
```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y
```

`firmware/sdkconfig.local.example`:
```
# Copia questo file in sdkconfig.local e personalizzalo.
# Dopo ogni modifica: rm sdkconfig && ./idf.sh build
CONFIG_PAGER_WIFI_SSID="NomeRete"
CONFIG_PAGER_WIFI_PASSWORD="password"
CONFIG_PAGER_SERVER_URL="ws://192.168.1.10:8000/ws"
CONFIG_PAGER_DEVICE_NAME="giulia"
```

`firmware/main/Kconfig.projbuild`:
```
menu "Cercapersone"

    config PAGER_WIFI_SSID
        string "SSID Wi-Fi"
        default "cambiami"

    config PAGER_WIFI_PASSWORD
        string "Password Wi-Fi"
        default "cambiami"

    config PAGER_SERVER_URL
        string "URL WebSocket del server (senza ?name=)"
        default "ws://192.168.1.10:8000/ws"

    config PAGER_DEVICE_NAME
        string "Nome del dispositivo (a-z 0-9 _ -, max 32)"
        default "cercapersone"

    config PAGER_LED_GPIO
        int "GPIO del LED WS2812"
        default 48

    config PAGER_BUTTON_GPIO
        int "GPIO del pulsante (attivo basso)"
        default 0

endmenu
```

`firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ".")
```

`firmware/main/main.c`:
```c
#include "esp_log.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "cercapersone \"%s\" avviato", CONFIG_PAGER_DEVICE_NAME);
}
```

`firmware/tools/serial_log.py`:
```python
"""Resetta la scheda (linee DTR/RTS) e stampa il log seriale per alcuni secondi.

Uso: serial_log.py [porta] [secondi]
"""

import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

with serial.Serial(port, 115200, timeout=0.2) as s:
    s.dtr = False  # IO0 alto: avvio normale, non download mode
    s.rts = True   # EN basso: reset
    time.sleep(0.1)
    s.rts = False
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        data = s.read(1024)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
```

Aggiungi a `.gitignore`:
```
firmware/sdkconfig.local
firmware/test_host/build/
```

- [ ] **Step 2: Compila**

Run: `cd firmware && ./idf.sh build`
Expected: termina con `Project build complete`. La prima build impiega qualche minuto.

- [ ] **Step 3: Flash e controllo del log**

Run:
```bash
cd firmware && ./idf.sh -p /dev/ttyACM0 flash
~/.espressif/tools/python/v5.5.2/venv/bin/python tools/serial_log.py /dev/ttyACM0 5
```
Expected: il flash termina con `Hard resetting via RTS pin...`; il log contiene `cercapersone "cercapersone" avviato`.

- [ ] **Step 4: Commit**

```bash
git add .gitignore firmware/CMakeLists.txt firmware/idf.sh firmware/sdkconfig.defaults firmware/sdkconfig.local.example firmware/main firmware/tools/serial_log.py
git commit -m "firmware: progetto ESP-IDF di base con configurazione Kconfig

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

Nota: non aggiungere `firmware/sdkconfig` né `firmware/build/` (già in `.gitignore`).

---

### Task 5: Firmware — protocollo (`pager_core/pager_proto`) con test sul PC

**Files:**
- Create: `firmware/components/pager_core/CMakeLists.txt`, `firmware/components/pager_core/include/pager_proto.h`, `firmware/components/pager_core/pager_proto.c`, `firmware/test_host/Makefile`
- Test: `firmware/test_host/test_pager_proto.c`

**Interfaces:**
- Consumes: cJSON (componente `json` di ESP-IDF; sul PC compilato dai sorgenti di ESP-IDF).
- Produces (`pager_proto.h`):
  - `#define PAGER_NAME_MAX 32`
  - `typedef enum { PAGER_MSG_CALL, PAGER_MSG_IGNORED } pager_msg_kind_t;`
  - `typedef struct { pager_msg_kind_t kind; char from[PAGER_NAME_MAX + 1]; int64_t ts; } pager_msg_t;`
  - `bool pager_proto_parse(const char *data, size_t len, pager_msg_t *out);` — `false` se non è un oggetto JSON; `data` può non essere terminato da `'\0'`.
  - `const char *pager_proto_call_message(void);` — `{"type":"call"}`
  - `bool pager_proto_valid_name(const char *name);`

- [ ] **Step 1: Scrivi i test che falliscono**

`firmware/test_host/test_pager_proto.c`:
```c
#include <string.h>

#include "pager_proto.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static bool parse_str(const char *s, pager_msg_t *out)
{
    return pager_proto_parse(s, strlen(s), out);
}

static void test_parse_full_call(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"call\",\"from\":\"giulia\",\"ts\":1760000000}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("giulia", m.from);
    TEST_ASSERT_TRUE(m.ts == 1760000000);
}

static void test_parse_uses_only_len_bytes(void)
{
    const char buf[] = "{\"type\":\"call\",\"from\":\"marco\"}SPAZZATURA";
    size_t len = strlen("{\"type\":\"call\",\"from\":\"marco\"}");
    pager_msg_t m;
    TEST_ASSERT_TRUE(pager_proto_parse(buf, len, &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("marco", m.from);
}

static void test_parse_call_without_from_or_ts(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"call\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("", m.from);
    TEST_ASSERT_TRUE(m.ts == 0);
}

static void test_parse_call_with_non_string_from(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"call\",\"from\":42,\"ts\":\"ieri\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("", m.from);
    TEST_ASSERT_TRUE(m.ts == 0);
}

static void test_parse_truncates_long_from(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"call\",\"from\":\"abcdefghijklmnopqrstuvwxyz0123456789\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_NAME_MAX, strlen(m.from));
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuvwxyz012345", m.from);
}

static void test_parse_ignores_extra_fields(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"nuovo\":[1,2],\"type\":\"call\",\"from\":\"a\",\"x\":{}}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("a", m.from);
}

static void test_parse_unknown_type_is_ignored(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"presence\",\"from\":\"a\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_IGNORED, m.kind);
    TEST_ASSERT_TRUE(parse_str("{\"from\":\"a\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_IGNORED, m.kind);
    TEST_ASSERT_TRUE(parse_str("{\"type\":1}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_IGNORED, m.kind);
}

static void test_parse_rejects_non_objects(void)
{
    pager_msg_t m;
    TEST_ASSERT_FALSE(parse_str("non json", &m));
    TEST_ASSERT_FALSE(parse_str("{\"type\":\"call\"", &m));
    TEST_ASSERT_FALSE(parse_str("[1,2]", &m));
    TEST_ASSERT_FALSE(parse_str("\"call\"", &m));
    TEST_ASSERT_FALSE(pager_proto_parse("{}", 0, &m));
}

static void test_call_message(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"call\"}", pager_proto_call_message());
}

static void test_valid_name(void)
{
    TEST_ASSERT_TRUE(pager_proto_valid_name("giulia"));
    TEST_ASSERT_TRUE(pager_proto_valid_name("a"));
    TEST_ASSERT_TRUE(pager_proto_valid_name("piano_2-nord"));
    TEST_ASSERT_TRUE(pager_proto_valid_name("abcdefghijklmnopqrstuvwxyz012345"));
    TEST_ASSERT_FALSE(pager_proto_valid_name(""));
    TEST_ASSERT_FALSE(pager_proto_valid_name("Giulia"));
    TEST_ASSERT_FALSE(pager_proto_valid_name("a b"));
    TEST_ASSERT_FALSE(pager_proto_valid_name("è"));
    TEST_ASSERT_FALSE(pager_proto_valid_name("abcdefghijklmnopqrstuvwxyz0123456"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_full_call);
    RUN_TEST(test_parse_uses_only_len_bytes);
    RUN_TEST(test_parse_call_without_from_or_ts);
    RUN_TEST(test_parse_call_with_non_string_from);
    RUN_TEST(test_parse_truncates_long_from);
    RUN_TEST(test_parse_ignores_extra_fields);
    RUN_TEST(test_parse_unknown_type_is_ignored);
    RUN_TEST(test_parse_rejects_non_objects);
    RUN_TEST(test_call_message);
    RUN_TEST(test_valid_name);
    return UNITY_END();
}
```

`firmware/test_host/Makefile`:
```make
# Test della logica pura del firmware, compilati con il gcc del PC.
# Eseguire in una shell SENZA l'ambiente ESP-IDF attivato: make -C firmware/test_host

IDF_PATH ?= $(HOME)/.espressif/v5.5.2/esp-idf
UNITY := $(IDF_PATH)/components/unity/unity/src
CJSON := $(IDF_PATH)/components/json/cJSON
CORE  := ../components/pager_core

CC := /usr/bin/gcc
CFLAGS := -std=gnu11 -Wall -Wextra -g -I$(CORE)/include -I$(UNITY) -I$(CJSON)

TESTS := test_pager_proto

all: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./build/$$t || exit 1; done

build:
	mkdir -p build

test_pager_proto: | build
	$(CC) $(CFLAGS) -o build/$@ $@.c $(CORE)/pager_proto.c $(UNITY)/unity.c $(CJSON)/cJSON.c

clean:
	rm -rf build

.PHONY: all clean $(TESTS)
```

- [ ] **Step 2: Verifica che falliscano**

Run: `make -C firmware/test_host`
Expected: errore di compilazione `pager_proto.h: No such file or directory`

- [ ] **Step 3: Implementa il protocollo**

`firmware/components/pager_core/include/pager_proto.h`:
```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAGER_NAME_MAX 32

typedef enum {
    PAGER_MSG_CALL,     /* qualcuno ha chiamato la pausa */
    PAGER_MSG_IGNORED,  /* JSON valido ma tipo sconosciuto o assente */
} pager_msg_kind_t;

typedef struct {
    pager_msg_kind_t kind;
    char from[PAGER_NAME_MAX + 1];  /* "" se assente o non stringa */
    int64_t ts;                     /* 0 se assente o non numerico */
} pager_msg_t;

/* Interpreta un messaggio dal server. data può non essere terminato da '\0'.
 * Ritorna false se data non contiene un oggetto JSON. */
bool pager_proto_parse(const char *data, size_t len, pager_msg_t *out);

/* Testo da inviare al server per chiamare la pausa. */
const char *pager_proto_call_message(void);

/* true se name rispetta [a-z0-9_-]{1,32}, come richiesto dal server. */
bool pager_proto_valid_name(const char *name);
```

`firmware/components/pager_core/pager_proto.c`:
```c
#include "pager_proto.h"

#include <string.h>

#include "cJSON.h"

bool pager_proto_parse(const char *data, size_t len, pager_msg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = PAGER_MSG_IGNORED;

    if (len == 0) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        return false;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "call") == 0) {
        out->kind = PAGER_MSG_CALL;

        const cJSON *from = cJSON_GetObjectItemCaseSensitive(root, "from");
        if (cJSON_IsString(from)) {
            size_t n = strlen(from->valuestring);
            if (n > PAGER_NAME_MAX) {
                n = PAGER_NAME_MAX;
            }
            memcpy(out->from, from->valuestring, n);
            out->from[n] = '\0';
        }

        const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
        if (cJSON_IsNumber(ts)) {
            out->ts = (int64_t)ts->valuedouble;
        }
    }

    cJSON_Delete(root);
    return true;
}

const char *pager_proto_call_message(void)
{
    return "{\"type\":\"call\"}";
}

bool pager_proto_valid_name(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > PAGER_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}
```

`firmware/components/pager_core/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "pager_proto.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json)
```

- [ ] **Step 4: Verifica che passino**

Run: `make -C firmware/test_host`
Expected: `10 Tests 0 Failures 0 Ignored` e `OK`

- [ ] **Step 5: Verifica che il componente compili per l'ESP32-S3**

Run: `cd firmware && ./idf.sh build`
Expected: `Project build complete`

- [ ] **Step 6: Commit**

```bash
git add firmware/components/pager_core firmware/test_host/Makefile firmware/test_host/test_pager_proto.c
git commit -m "firmware: protocollo dei messaggi con test sul PC

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Firmware — macchina a stati (`pager_core/pager_app`) con test sul PC

**Files:**
- Create: `firmware/components/pager_core/include/pager_app.h`, `firmware/components/pager_core/pager_app.c`
- Modify: `firmware/components/pager_core/CMakeLists.txt`, `firmware/test_host/Makefile`
- Test: `firmware/test_host/test_pager_app.c`

**Interfaces:**
- Consumes: niente.
- Produces (`pager_app.h`):
  - `#define PAGER_ALERT_MS 10000u`, `#define PAGER_OFFLINE_ERROR_MS 60000u`
  - `typedef enum { PAGER_LED_NO_CHANGE = 0, PAGER_LED_OFF, PAGER_LED_CONNECTING, PAGER_LED_SENT, PAGER_LED_ALERT, PAGER_LED_ERROR } pager_led_t;`
  - `typedef enum { PAGER_STATE_CONNECTING, PAGER_STATE_IDLE, PAGER_STATE_ALERTING, PAGER_STATE_OFFLINE_ERROR } pager_state_t;`
  - `typedef enum { PAGER_EV_LINK_UP, PAGER_EV_LINK_DOWN, PAGER_EV_BUTTON, PAGER_EV_CALL_RECEIVED, PAGER_EV_TICK } pager_event_t;`
  - `typedef struct { bool send_call; pager_led_t led; } pager_output_t;`
  - `typedef struct { pager_state_t state; uint32_t alert_until_ms; uint32_t down_since_ms; } pager_app_t;`
  - `void pager_app_init(pager_app_t *app, uint32_t now_ms);`
  - `pager_output_t pager_app_handle(pager_app_t *app, pager_event_t ev, uint32_t now_ms);`

- [ ] **Step 1: Scrivi i test che falliscono**

`firmware/test_host/test_pager_app.c`:
```c
#include "pager_app.h"
#include "unity.h"

static pager_app_t app;

void setUp(void) {}
void tearDown(void) {}

/* Porta l'app in IDLE all'istante t. */
static void start_idle(uint32_t t)
{
    pager_app_init(&app, t);
    pager_app_handle(&app, PAGER_EV_LINK_UP, t);
}

static void test_init_is_connecting(void)
{
    pager_app_init(&app, 0);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
}

static void test_link_up_goes_idle_with_led_off(void)
{
    pager_app_init(&app, 0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_LINK_UP, 100);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
    TEST_ASSERT_FALSE(out.send_call);
}

static void test_button_when_idle_sends_call(void)
{
    start_idle(0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_BUTTON, 200);
    TEST_ASSERT_TRUE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_SENT, out.led);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_button_when_connecting_does_nothing(void)
{
    pager_app_init(&app, 0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_BUTTON, 200);
    TEST_ASSERT_FALSE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_NO_CHANGE, out.led);
}

static void test_call_received_when_idle_alerts(void)
{
    start_idle(0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_ALERT, out.led);
}

static void test_call_received_when_connecting_is_ignored(void)
{
    pager_app_init(&app, 0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_NO_CHANGE, out.led);
}

static void test_button_during_alert_dismisses_without_calling(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_BUTTON, 2000);
    TEST_ASSERT_FALSE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_alert_ends_after_timeout(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_ALERT_MS - 1);
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_NO_CHANGE, out.led);
    out = pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_ALERT_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
}

static void test_new_call_during_alert_restarts_timer(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 6000);
    TEST_ASSERT_EQUAL(PAGER_LED_ALERT, out.led);
    pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_ALERT_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    pager_app_handle(&app, PAGER_EV_TICK, 6000 + PAGER_ALERT_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_alert_timeout_across_wraparound(void)
{
    uint32_t t = 0xFFFFF000u;  /* 4096 ms prima dell'overflow */
    start_idle(t);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, t);
    pager_app_handle(&app, PAGER_EV_TICK, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    pager_app_handle(&app, PAGER_EV_TICK, t + PAGER_ALERT_MS - 1);  /* dopo l'overflow */
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_TICK, t + PAGER_ALERT_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
}

static void test_link_down_goes_connecting(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_LINK_DOWN, 2000);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_CONNECTING, out.led);
}

static void test_offline_too_long_shows_error(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_LINK_DOWN, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_OFFLINE_ERROR_MS - 1);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
    out = pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_OFFLINE_ERROR_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_OFFLINE_ERROR, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_ERROR, out.led);
}

static void test_never_connected_shows_error_after_timeout(void)
{
    pager_app_init(&app, 500);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_TICK, 500 + PAGER_OFFLINE_ERROR_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_OFFLINE_ERROR, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_ERROR, out.led);
}

static void test_repeated_link_down_does_not_reset_offline_timer(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_LINK_DOWN, 1000);
    pager_app_handle(&app, PAGER_EV_LINK_DOWN, 30000);
    pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_OFFLINE_ERROR_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_OFFLINE_ERROR, app.state);
}

static void test_link_up_after_error_goes_idle(void)
{
    pager_app_init(&app, 0);
    pager_app_handle(&app, PAGER_EV_TICK, PAGER_OFFLINE_ERROR_MS);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_LINK_UP, PAGER_OFFLINE_ERROR_MS + 5000);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_is_connecting);
    RUN_TEST(test_link_up_goes_idle_with_led_off);
    RUN_TEST(test_button_when_idle_sends_call);
    RUN_TEST(test_button_when_connecting_does_nothing);
    RUN_TEST(test_call_received_when_idle_alerts);
    RUN_TEST(test_call_received_when_connecting_is_ignored);
    RUN_TEST(test_button_during_alert_dismisses_without_calling);
    RUN_TEST(test_alert_ends_after_timeout);
    RUN_TEST(test_new_call_during_alert_restarts_timer);
    RUN_TEST(test_alert_timeout_across_wraparound);
    RUN_TEST(test_link_down_goes_connecting);
    RUN_TEST(test_offline_too_long_shows_error);
    RUN_TEST(test_never_connected_shows_error_after_timeout);
    RUN_TEST(test_repeated_link_down_does_not_reset_offline_timer);
    RUN_TEST(test_link_up_after_error_goes_idle);
    return UNITY_END();
}
```

In `firmware/test_host/Makefile` sostituisci la riga `TESTS := test_pager_proto` con:
```make
TESTS := test_pager_proto test_pager_app
```
e aggiungi, dopo la regola `test_pager_proto`:
```make
test_pager_app: | build
	$(CC) $(CFLAGS) -o build/$@ $@.c $(CORE)/pager_app.c $(UNITY)/unity.c
```

- [ ] **Step 2: Verifica che falliscano**

Run: `make -C firmware/test_host`
Expected: `test_pager_proto` passa; `test_pager_app` fallisce in compilazione con `pager_app.h: No such file or directory`

- [ ] **Step 3: Implementa la macchina a stati**

`firmware/components/pager_core/include/pager_app.h`:
```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PAGER_ALERT_MS 10000u          /* durata dell'avviso */
#define PAGER_OFFLINE_ERROR_MS 60000u  /* senza server per più di così → errore */

typedef enum {
    PAGER_LED_NO_CHANGE = 0,
    PAGER_LED_OFF,
    PAGER_LED_CONNECTING,  /* blu, pulsazione lenta */
    PAGER_LED_SENT,        /* verde, lampo breve, poi spento */
    PAGER_LED_ALERT,       /* arancione lampeggiante */
    PAGER_LED_ERROR,       /* rosso fisso */
} pager_led_t;

typedef enum {
    PAGER_STATE_CONNECTING,
    PAGER_STATE_IDLE,
    PAGER_STATE_ALERTING,
    PAGER_STATE_OFFLINE_ERROR,
} pager_state_t;

typedef enum {
    PAGER_EV_LINK_UP,
    PAGER_EV_LINK_DOWN,
    PAGER_EV_BUTTON,
    PAGER_EV_CALL_RECEIVED,
    PAGER_EV_TICK,
} pager_event_t;

typedef struct {
    bool send_call;
    pager_led_t led;
} pager_output_t;

typedef struct {
    pager_state_t state;
    uint32_t alert_until_ms;
    uint32_t down_since_ms;
} pager_app_t;

/* Stato iniziale: in connessione. Il chiamante accende PAGER_LED_CONNECTING. */
void pager_app_init(pager_app_t *app, uint32_t now_ms);

/* Applica un evento all'istante now_ms (contatore che può andare in overflow). */
pager_output_t pager_app_handle(pager_app_t *app, pager_event_t ev, uint32_t now_ms);
```

`firmware/components/pager_core/pager_app.c`:
```c
#include "pager_app.h"

/* true se now ha raggiunto deadline, anche a cavallo dell'overflow di uint32_t */
static bool reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

void pager_app_init(pager_app_t *app, uint32_t now_ms)
{
    app->state = PAGER_STATE_CONNECTING;
    app->alert_until_ms = 0;
    app->down_since_ms = now_ms;
}

pager_output_t pager_app_handle(pager_app_t *app, pager_event_t ev, uint32_t now_ms)
{
    pager_output_t out = { .send_call = false, .led = PAGER_LED_NO_CHANGE };

    switch (ev) {
    case PAGER_EV_LINK_UP:
        if (app->state == PAGER_STATE_CONNECTING || app->state == PAGER_STATE_OFFLINE_ERROR) {
            app->state = PAGER_STATE_IDLE;
            out.led = PAGER_LED_OFF;
        }
        break;

    case PAGER_EV_LINK_DOWN:
        if (app->state == PAGER_STATE_IDLE || app->state == PAGER_STATE_ALERTING) {
            app->state = PAGER_STATE_CONNECTING;
            app->down_since_ms = now_ms;
            out.led = PAGER_LED_CONNECTING;
        }
        break;

    case PAGER_EV_BUTTON:
        if (app->state == PAGER_STATE_IDLE) {
            out.send_call = true;
            out.led = PAGER_LED_SENT;
        } else if (app->state == PAGER_STATE_ALERTING) {
            app->state = PAGER_STATE_IDLE;
            out.led = PAGER_LED_OFF;
        }
        break;

    case PAGER_EV_CALL_RECEIVED:
        if (app->state == PAGER_STATE_IDLE || app->state == PAGER_STATE_ALERTING) {
            app->state = PAGER_STATE_ALERTING;
            app->alert_until_ms = now_ms + PAGER_ALERT_MS;
            out.led = PAGER_LED_ALERT;
        }
        break;

    case PAGER_EV_TICK:
        if (app->state == PAGER_STATE_ALERTING && reached(now_ms, app->alert_until_ms)) {
            app->state = PAGER_STATE_IDLE;
            out.led = PAGER_LED_OFF;
        } else if (app->state == PAGER_STATE_CONNECTING &&
                   reached(now_ms, app->down_since_ms + PAGER_OFFLINE_ERROR_MS)) {
            app->state = PAGER_STATE_OFFLINE_ERROR;
            out.led = PAGER_LED_ERROR;
        }
        break;
    }
    return out;
}
```

In `firmware/components/pager_core/CMakeLists.txt` sostituisci `SRCS "pager_proto.c"` con:
```cmake
idf_component_register(SRCS "pager_proto.c" "pager_app.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json)
```

- [ ] **Step 4: Verifica che passino**

Run: `make -C firmware/test_host`
Expected: `== test_pager_proto` → `10 Tests 0 Failures`; `== test_pager_app` → `15 Tests 0 Failures`

- [ ] **Step 5: Verifica la build per ESP32-S3**

Run: `cd firmware && ./idf.sh build`
Expected: `Project build complete`

- [ ] **Step 6: Commit**

```bash
git add firmware/components/pager_core firmware/test_host/Makefile firmware/test_host/test_pager_app.c
git commit -m "firmware: macchina a stati del cercapersone con test sul PC

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: Firmware — LED (`notifier`) e pulsante (`input`)

**Files:**
- Create: `firmware/components/notifier/CMakeLists.txt`, `firmware/components/notifier/idf_component.yml`, `firmware/components/notifier/include/notifier.h`, `firmware/components/notifier/notifier.c`, `firmware/components/input/CMakeLists.txt`, `firmware/components/input/include/input.h`, `firmware/components/input/input.c`
- Modify: `firmware/main/main.c`, `firmware/main/CMakeLists.txt` (demo temporanea, sostituita nel Task 9)

**Interfaces:**
- Consumes: `pager_led_t` da `pager_app.h` (Task 6).
- Produces:
  - `esp_err_t notifier_start(int gpio);`
  - `void notifier_set(pager_led_t pattern);` — `PAGER_LED_NO_CHANGE` viene ignorato; sicura da qualunque task.
  - `ESP_EVENT_DECLARE_BASE(INPUT_EVENT);` `enum { INPUT_EVENT_PRESSED };`
  - `esp_err_t input_start(int gpio);` — pubblica `INPUT_EVENT_PRESSED` sul loop eventi di default (che deve già esistere) a ogni pressione (fronte di discesa, debounce 50 ms).

- [ ] **Step 1: Scrivi il componente `notifier`**

`firmware/components/notifier/idf_component.yml`:
```yaml
dependencies:
  espressif/led_strip: "^3.0.0"
```

`firmware/components/notifier/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "notifier.c"
                       INCLUDE_DIRS "include"
                       REQUIRES pager_core
                       PRIV_REQUIRES esp_timer)
```

`firmware/components/notifier/include/notifier.h`:
```c
#pragma once

#include "esp_err.h"
#include "pager_app.h"

/* Inizializza il LED WS2812 sul GPIO indicato e avvia il task che lo anima. */
esp_err_t notifier_start(int gpio);

/* Cambia il pattern luminoso. PAGER_LED_NO_CHANGE viene ignorato. */
void notifier_set(pager_led_t pattern);
```

`firmware/components/notifier/notifier.c`:
```c
#include "notifier.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_strip.h"

#define FRAME_MS 20
#define SENT_FLASH_MS 300
#define BLINK_HALF_PERIOD_MS 250
#define PULSE_PERIOD_MS 2000
#define BRIGHT 32  /* luminosità massima (0-255): il LED è molto forte */

static const char *TAG = "notifier";
static led_strip_handle_t s_strip;
static QueueHandle_t s_queue;

static void show(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void notifier_task(void *arg)
{
    pager_led_t pattern = PAGER_LED_OFF;
    int64_t started_ms = now_ms();

    for (;;) {
        pager_led_t requested;
        if (xQueueReceive(s_queue, &requested, pdMS_TO_TICKS(FRAME_MS)) == pdTRUE) {
            pattern = requested;
            started_ms = now_ms();
        }
        uint32_t t = (uint32_t)(now_ms() - started_ms);

        switch (pattern) {
        case PAGER_LED_CONNECTING: {
            uint32_t phase = t % PULSE_PERIOD_MS;
            uint32_t half = PULSE_PERIOD_MS / 2;
            uint32_t level = phase < half ? phase : PULSE_PERIOD_MS - phase;  /* 0..half */
            show(0, 0, (uint8_t)(level * BRIGHT / half));
            break;
        }
        case PAGER_LED_SENT:
            if (t < SENT_FLASH_MS) {
                show(0, BRIGHT, 0);
            } else {
                pattern = PAGER_LED_OFF;
                show(0, 0, 0);
            }
            break;
        case PAGER_LED_ALERT:
            if ((t / BLINK_HALF_PERIOD_MS) % 2 == 0) {
                show(BRIGHT, BRIGHT / 4, 0);
            } else {
                show(0, 0, 0);
            }
            break;
        case PAGER_LED_ERROR:
            show(BRIGHT, 0, 0);
            break;
        case PAGER_LED_OFF:
        case PAGER_LED_NO_CHANGE:
        default:
            show(0, 0, 0);
            break;
        }
    }
}

esp_err_t notifier_start(int gpio)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip), TAG, "init LED");

    s_queue = xQueueCreate(1, sizeof(pager_led_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "coda");
    ESP_RETURN_ON_FALSE(xTaskCreate(notifier_task, "notifier", 3072, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

void notifier_set(pager_led_t pattern)
{
    if (pattern == PAGER_LED_NO_CHANGE || s_queue == NULL) {
        return;
    }
    xQueueOverwrite(s_queue, &pattern);
}
```

- [ ] **Step 2: Scrivi il componente `input`**

`firmware/components/input/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "input.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES esp_driver_gpio)
```

`firmware/components/input/include/input.h`:
```c
#pragma once

#include "esp_err.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(INPUT_EVENT);

enum {
    INPUT_EVENT_PRESSED,  /* pulsante premuto (una volta per pressione) */
};

/* Avvia la lettura del pulsante (attivo basso, pull-up interno).
 * Richiede che il loop eventi di default sia già stato creato. */
esp_err_t input_start(int gpio);
```

`firmware/components/input/input.c`:
```c
#include "input.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POLL_MS 10
#define STABLE_SAMPLES 5  /* 5 letture uguali = 50 ms di debounce */

ESP_EVENT_DEFINE_BASE(INPUT_EVENT);

static const char *TAG = "input";

static void input_task(void *arg)
{
    int gpio = (int)(intptr_t)arg;
    bool last_raw = false;
    bool stable_pressed = false;
    int same_count = 0;

    for (;;) {
        bool raw = gpio_get_level(gpio) == 0;
        if (raw == last_raw) {
            if (same_count < STABLE_SAMPLES) {
                same_count++;
            }
        } else {
            last_raw = raw;
            same_count = 0;
        }

        if (same_count == STABLE_SAMPLES && raw != stable_pressed) {
            stable_pressed = raw;
            if (stable_pressed) {
                ESP_LOGI(TAG, "pulsante premuto");
                esp_event_post(INPUT_EVENT, INPUT_EVENT_PRESSED, NULL, 0, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t input_start(int gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "config GPIO");
    ESP_RETURN_ON_FALSE(xTaskCreate(input_task, "input", 2560, (void *)(intptr_t)gpio, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}
```

- [ ] **Step 3: Demo temporanea in `main`: il pulsante fa scorrere i pattern**

`firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event pager_core notifier input)
```

`firmware/main/main.c`:
```c
/* Demo temporanea (Task 7): ogni pressione del pulsante passa al pattern successivo. */
#include "esp_event.h"
#include "esp_log.h"
#include "input.h"
#include "notifier.h"

static const char *TAG = "main";

static const pager_led_t DEMO[] = {
    PAGER_LED_CONNECTING, PAGER_LED_SENT, PAGER_LED_ALERT, PAGER_LED_ERROR, PAGER_LED_OFF,
};
static const char *DEMO_NAMES[] = { "connessione", "inviata", "avviso", "errore", "spento" };

static void on_press(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    static int i = 0;
    ESP_LOGI(TAG, "pattern: %s", DEMO_NAMES[i]);
    notifier_set(DEMO[i]);
    i = (i + 1) % (int)(sizeof(DEMO) / sizeof(DEMO[0]));
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(notifier_start(CONFIG_PAGER_LED_GPIO));
    ESP_ERROR_CHECK(esp_event_handler_register(INPUT_EVENT, INPUT_EVENT_PRESSED, on_press, NULL));
    ESP_ERROR_CHECK(input_start(CONFIG_PAGER_BUTTON_GPIO));
    ESP_LOGI(TAG, "demo LED pronta: premi BOOT");
}
```

- [ ] **Step 4: Compila e flasha**

Run: `cd firmware && ./idf.sh build && ./idf.sh -p /dev/ttyACM0 flash`
Expected: il component manager scarica `espressif/led_strip`; `Project build complete`; flash completato. Viene creato `firmware/dependencies.lock` (da versionare).

- [ ] **Step 5: Verifica manuale con l'utente**

Run: `~/.espressif/tools/python/v5.5.2/venv/bin/python firmware/tools/serial_log.py /dev/ttyACM0 40`
e, mentre gira, chiedi all'utente di premere BOOT 5 volte, una al secondo, e di descrivere il LED.

Expected:
- log `demo LED pronta: premi BOOT`, poi per ogni pressione **una sola** riga `pulsante premuto` e `pattern: ...`.
- LED: blu che pulsa → lampo verde e poi spento → arancione lampeggiante → rosso fisso → spento.
- Se il LED non si accende per niente: la scheda potrebbe avere il LED su GPIO38 (DevKitC-1 v1.1). Prova con `CONFIG_PAGER_LED_GPIO=38` in `sdkconfig.local`, `rm sdkconfig`, rifai build e flash e riporta il risultato all'utente.
- Se i colori sono scambiati (es. rosso al posto di verde): sostituisci `LED_STRIP_COLOR_COMPONENT_FMT_GRB` con `LED_STRIP_COLOR_COMPONENT_FMT_RGB` e riverifica.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/notifier firmware/components/input firmware/main firmware/dependencies.lock
git commit -m "firmware: componenti LED e pulsante con demo dei pattern

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: Firmware — Wi-Fi (`wifi_conn`) e collegamento al server (`pager_link`)

**Files:**
- Create: `firmware/components/wifi_conn/CMakeLists.txt`, `firmware/components/wifi_conn/include/wifi_conn.h`, `firmware/components/wifi_conn/wifi_conn.c`, `firmware/components/pager_link/CMakeLists.txt`, `firmware/components/pager_link/idf_component.yml`, `firmware/components/pager_link/include/pager_link.h`, `firmware/components/pager_link/pager_link.c`
- Modify: `firmware/main/main.c`, `firmware/main/CMakeLists.txt` (demo temporanea, sostituita nel Task 9)

**Interfaces:**
- Consumes: `pager_proto_parse`, `pager_proto_call_message`, `pager_proto_valid_name`, `pager_msg_t` (Task 5); server e `fake_pager.py` (Task 2-3) per la verifica.
- Produces:
  - `esp_err_t wifi_conn_start(const char *ssid, const char *password);` — richiede `nvs_flash_init()` ed `esp_event_loop_create_default()` già eseguiti.
  - `ESP_EVENT_DECLARE_BASE(PAGER_LINK_EVENT);` `enum { PAGER_LINK_EVENT_UP, PAGER_LINK_EVENT_DOWN, PAGER_LINK_EVENT_CALL };` (dati di `CALL`: `pager_msg_t`)
  - `esp_err_t pager_link_start(const char *server_url, const char *device_name);` — `ESP_ERR_INVALID_ARG` se il nome non è valido.
  - `esp_err_t pager_link_send_call(void);` — `ESP_ERR_INVALID_STATE` se non connesso.

- [ ] **Step 1: Scrivi il componente `wifi_conn`**

`firmware/components/wifi_conn/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "wifi_conn.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer)
```

`firmware/components/wifi_conn/include/wifi_conn.h`:
```c
#pragma once

#include "esp_err.h"

/* Connette la scheda al Wi-Fi (WPA2/WPA3-Personal) e la tiene connessa,
 * riprovando ogni 2 s se la connessione cade.
 * Richiede nvs_flash_init() ed esp_event_loop_create_default() già eseguiti. */
esp_err_t wifi_conn_start(const char *ssid, const char *password);
```

`firmware/components/wifi_conn/wifi_conn.c`:
```c
#include "wifi_conn.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#define RETRY_DELAY_US (2 * 1000 * 1000)

static const char *TAG = "wifi_conn";
static esp_timer_handle_t s_retry_timer;

static void retry_cb(void *arg)
{
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = data;
        ESP_LOGW(TAG, "Wi-Fi disconnesso (motivo %d), riprovo tra 2 s", ev->reason);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, RETRY_DELAY_US);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "connesso, IP " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

esp_err_t wifi_conn_start(const char *ssid, const char *password)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "init");

    const esp_timer_create_args_t timer_args = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_retry_timer), TAG, "timer");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL), TAG, "eventi Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL), TAG, "eventi IP");

    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "modo");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    ESP_LOGI(TAG, "connessione a \"%s\"...", ssid);
    return ESP_OK;
}
```

- [ ] **Step 2: Scrivi il componente `pager_link`**

`firmware/components/pager_link/idf_component.yml`:
```yaml
dependencies:
  espressif/esp_websocket_client: "^1.8.0"
```

`firmware/components/pager_link/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "pager_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_event
                       PRIV_REQUIRES pager_core)
```

`firmware/components/pager_link/include/pager_link.h`:
```c
#pragma once

#include "esp_err.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(PAGER_LINK_EVENT);

enum {
    PAGER_LINK_EVENT_UP,    /* connesso al server */
    PAGER_LINK_EVENT_DOWN,  /* connessione persa o tentativo fallito */
    PAGER_LINK_EVENT_CALL,  /* chiamata ricevuta; dati: pager_msg_t */
};

/* Avvia la connessione WebSocket a "<server_url>?name=<device_name>" e la mantiene
 * (backoff 1 s → 30 s). ESP_ERR_INVALID_ARG se il nome non è valido. */
esp_err_t pager_link_start(const char *server_url, const char *device_name);

/* Invia una chiamata. ESP_ERR_INVALID_STATE se non connesso. */
esp_err_t pager_link_send_call(void);
```

`firmware/components/pager_link/pager_link.c`:
```c
#include "pager_link.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "pager_proto.h"

#define BACKOFF_MIN_MS 1000
#define BACKOFF_MAX_MS 30000
#define WS_OPCODE_TEXT 0x01

ESP_EVENT_DEFINE_BASE(PAGER_LINK_EVENT);

static const char *TAG = "pager_link";
static esp_websocket_client_handle_t s_client;
static int s_backoff_ms = BACKOFF_MIN_MS;
static char s_url[256];

static void handle_data(const esp_websocket_event_data_t *data)
{
    if (data->op_code != WS_OPCODE_TEXT) {
        return;  /* ping/pong/close e frame binari */
    }
    if (data->payload_offset != 0 || data->data_len != data->payload_len) {
        ESP_LOGW(TAG, "messaggio frammentato ignorato (%d byte)", data->payload_len);
        return;
    }
    pager_msg_t msg;
    if (!pager_proto_parse(data->data_ptr, data->data_len, &msg)) {
        ESP_LOGW(TAG, "messaggio non valido: %.*s", data->data_len, data->data_ptr);
        return;
    }
    if (msg.kind == PAGER_MSG_CALL) {
        ESP_LOGI(TAG, "chiamata da %s", msg.from[0] ? msg.from : "?");
        esp_event_post(PAGER_LINK_EVENT, PAGER_LINK_EVENT_CALL, &msg, sizeof(msg), pdMS_TO_TICKS(100));
    }
}

static void on_ws_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connesso a %s", s_url);
        s_backoff_ms = BACKOFF_MIN_MS;
        esp_websocket_client_set_reconnect_timeout(s_client, s_backoff_ms);
        esp_event_post(PAGER_LINK_EVENT, PAGER_LINK_EVENT_UP, NULL, 0, pdMS_TO_TICKS(100));
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGW(TAG, "server non raggiungibile, nuovo tentativo tra %d ms", s_backoff_ms);
        esp_websocket_client_set_reconnect_timeout(s_client, s_backoff_ms);
        s_backoff_ms = s_backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : s_backoff_ms * 2;
        esp_event_post(PAGER_LINK_EVENT, PAGER_LINK_EVENT_DOWN, NULL, 0, pdMS_TO_TICKS(100));
        break;
    case WEBSOCKET_EVENT_DATA:
        handle_data(event_data);
        break;
    default:
        break;
    }
}

esp_err_t pager_link_start(const char *server_url, const char *device_name)
{
    if (!pager_proto_valid_name(device_name)) {
        ESP_LOGE(TAG, "nome dispositivo non valido: \"%s\" (ammessi a-z 0-9 _ -, max 32)", device_name);
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(s_url, sizeof(s_url), "%s?name=%s", server_url, device_name);
    ESP_RETURN_ON_FALSE(n > 0 && n < (int)sizeof(s_url), ESP_ERR_INVALID_SIZE, TAG, "URL troppo lungo");

    esp_websocket_client_config_t cfg = {
        .uri = s_url,
        .reconnect_timeout_ms = BACKOFF_MIN_MS,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 20,
        .pingpong_timeout_sec = 60,
        .enable_close_reconnect = true,  /* riconnetti anche dopo una chiusura pulita del server */
    };
    s_client = esp_websocket_client_init(&cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_FAIL, TAG, "init client");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws_event, NULL),
                        TAG, "eventi");
    return esp_websocket_client_start(s_client);
}

esp_err_t pager_link_send_call(void)
{
    if (s_client == NULL || !esp_websocket_client_is_connected(s_client)) {
        return ESP_ERR_INVALID_STATE;
    }
    const char *msg = pager_proto_call_message();
    int sent = esp_websocket_client_send_text(s_client, msg, strlen(msg), pdMS_TO_TICKS(1000));
    return sent < 0 ? ESP_FAIL : ESP_OK;
}
```

- [ ] **Step 3: Demo temporanea in `main`: solo rete, log degli eventi**

`firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event nvs_flash pager_core wifi_conn pager_link)
```

`firmware/main/main.c`:
```c
/* Demo temporanea (Task 8): connessione al server e log degli eventi. */
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "pager_link.h"
#include "pager_proto.h"
#include "wifi_conn.h"

static const char *TAG = "main";

static void on_link(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == PAGER_LINK_EVENT_UP) {
        ESP_LOGI(TAG, "evento: UP");
    } else if (id == PAGER_LINK_EVENT_DOWN) {
        ESP_LOGI(TAG, "evento: DOWN");
    } else if (id == PAGER_LINK_EVENT_CALL) {
        const pager_msg_t *msg = data;
        ESP_LOGI(TAG, "evento: CALL da %s", msg->from);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(PAGER_LINK_EVENT, ESP_EVENT_ANY_ID, on_link, NULL));
    ESP_ERROR_CHECK(wifi_conn_start(CONFIG_PAGER_WIFI_SSID, CONFIG_PAGER_WIFI_PASSWORD));
    ESP_ERROR_CHECK(pager_link_start(CONFIG_PAGER_SERVER_URL, CONFIG_PAGER_DEVICE_NAME));
}
```

- [ ] **Step 4: Compila**

Run: `cd firmware && ./idf.sh build`
Expected: il component manager scarica `espressif/esp_websocket_client`; `Project build complete`. Se il compilatore segnala che `enable_close_reconnect` o `esp_websocket_client_set_reconnect_timeout` non esistono, controlla in `firmware/managed_components/espressif__esp_websocket_client/include/esp_websocket_client.h` la versione scaricata (deve essere ≥ 1.8.0) e riporta il problema invece di rimuovere le chiamate.

- [ ] **Step 5: Configura la scheda per il server locale (serve l'utente)**

Chiedi all'utente SSID e password del Wi-Fi da usare per il test e conferma l'IP del PC (`hostname -I`, primo indirizzo nella stessa rete della scheda). Poi crea `firmware/sdkconfig.local` (non versionato) partendo da `sdkconfig.local.example`:
```
CONFIG_PAGER_WIFI_SSID="<ssid>"
CONFIG_PAGER_WIFI_PASSWORD="<password>"
CONFIG_PAGER_SERVER_URL="ws://<ip-del-pc>:8000/ws"
CONFIG_PAGER_DEVICE_NAME="scheda1"
```
Run: `cd firmware && rm -f sdkconfig && ./idf.sh build && ./idf.sh -p /dev/ttyACM0 flash`
Expected: flash completato.

- [ ] **Step 6: Verifica con il server locale**

Run:
```bash
cd server
.venv/bin/uvicorn app.main:app --host 0.0.0.0 --port 8000 --ws-ping-interval 20 --ws-ping-timeout 20 > <scratchpad>/server.log 2>&1 &
SERVER_PID=$!
~/.espressif/tools/python/v5.5.2/venv/bin/python ../firmware/tools/serial_log.py /dev/ttyACM0 40 > <scratchpad>/serial.log &
sleep 15
curl -s http://127.0.0.1:8000/devices
curl -s -X POST http://127.0.0.1:8000/call -H 'Content-Type: application/json' -d '{"from":"pc"}'
sleep 3
kill $SERVER_PID
sleep 20
cat <scratchpad>/serial.log | grep -E "wifi_conn|pager_link|main"
```
Expected:
- `/devices` → `{"devices":["scheda1"]}`; `/call` → `{"delivered":1}`
- nel log seriale: `connesso, IP ...`, `connesso a ws://...`, `evento: UP`, `chiamata da pc`, `evento: CALL da pc`; dopo l'arresto del server `evento: DOWN` e righe `nuovo tentativo tra 1000 ms`, `2000 ms`, `4000 ms`, ...
- Se la scheda non si connette ma il Wi-Fi sì: probabilmente il firewall del PC blocca la porta 8000 (`sudo ufw status`). Segnalalo all'utente, senza modificare il firewall di tua iniziativa.

- [ ] **Step 7: Commit**

```bash
git add firmware/components/wifi_conn firmware/components/pager_link firmware/main firmware/dependencies.lock
git commit -m "firmware: connessione Wi-Fi e collegamento WebSocket al server

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: Firmware — applicazione completa e prova end-to-end

**Files:**
- Modify: `firmware/main/main.c`, `firmware/main/CMakeLists.txt`
- Create: `firmware/README.md`

**Interfaces:**
- Consumes: `pager_app_init`, `pager_app_handle`, `pager_event_t`, `pager_output_t` (Task 6); `notifier_start`, `notifier_set`, `input_start`, `INPUT_EVENT` (Task 7); `wifi_conn_start`, `pager_link_start`, `pager_link_send_call`, `PAGER_LINK_EVENT` (Task 8).
- Produces: il firmware finale.

- [ ] **Step 1: Scrivi `main` definitivo**

`firmware/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event esp_timer nvs_flash pager_core notifier input wifi_conn pager_link)
```

`firmware/main/main.c`:
```c
/* Cercapersone: collega pulsante, LED e server tramite la macchina a stati di pager_app.
 * Tutti gli eventi finiscono in una coda letta da un solo task, che possiede lo stato. */
#include <stdint.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input.h"
#include "notifier.h"
#include "nvs_flash.h"
#include "pager_app.h"
#include "pager_link.h"
#include "wifi_conn.h"

#define TICK_MS 100

static const char *TAG = "main";
static QueueHandle_t s_events;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    pager_event_t ev;
    if (base == INPUT_EVENT && id == INPUT_EVENT_PRESSED) {
        ev = PAGER_EV_BUTTON;
    } else if (base == PAGER_LINK_EVENT && id == PAGER_LINK_EVENT_UP) {
        ev = PAGER_EV_LINK_UP;
    } else if (base == PAGER_LINK_EVENT && id == PAGER_LINK_EVENT_DOWN) {
        ev = PAGER_EV_LINK_DOWN;
    } else if (base == PAGER_LINK_EVENT && id == PAGER_LINK_EVENT_CALL) {
        ev = PAGER_EV_CALL_RECEIVED;
    } else {
        return;
    }
    if (xQueueSend(s_events, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "coda eventi piena, evento %d perso", (int)ev);
    }
}

static void app_task(void *arg)
{
    pager_app_t app;
    pager_app_init(&app, now_ms());
    notifier_set(PAGER_LED_CONNECTING);

    for (;;) {
        pager_event_t ev;
        if (xQueueReceive(s_events, &ev, pdMS_TO_TICKS(TICK_MS)) != pdTRUE) {
            ev = PAGER_EV_TICK;
        }
        pager_output_t out = pager_app_handle(&app, ev, now_ms());
        if (out.send_call) {
            esp_err_t err = pager_link_send_call();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "chiamata inviata");
            } else {
                ESP_LOGW(TAG, "invio chiamata fallito: %s", esp_err_to_name(err));
            }
        }
        notifier_set(out.led);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_events = xQueueCreate(16, sizeof(pager_event_t));
    ESP_ERROR_CHECK(s_events != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(notifier_start(CONFIG_PAGER_LED_GPIO));
    ESP_ERROR_CHECK(esp_event_handler_register(INPUT_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PAGER_LINK_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    /* Priorità 5 > app_main: parte subito e accende il LED blu prima di proseguire. */
    xTaskCreate(app_task, "app", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(input_start(CONFIG_PAGER_BUTTON_GPIO));
    ESP_ERROR_CHECK(wifi_conn_start(CONFIG_PAGER_WIFI_SSID, CONFIG_PAGER_WIFI_PASSWORD));

    ESP_LOGI(TAG, "cercapersone \"%s\" avviato", CONFIG_PAGER_DEVICE_NAME);
    err = pager_link_start(CONFIG_PAGER_SERVER_URL, CONFIG_PAGER_DEVICE_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "impossibile avviare il collegamento al server: %s", esp_err_to_name(err));
        notifier_set(PAGER_LED_ERROR);
    }
}
```

- [ ] **Step 2: Scrivi `firmware/README.md`**

````markdown
# Firmware del cercapersone (ESP32-S3, ESP-IDF 5.5.2)

## Configurazione di un dispositivo

```bash
cp sdkconfig.local.example sdkconfig.local   # poi modifica SSID, password, URL, nome
rm -f sdkconfig                              # necessario dopo ogni modifica di sdkconfig.local
```

Il nome (`CONFIG_PAGER_DEVICE_NAME`) deve essere unico e rispettare `[a-z0-9_-]`, max 32 caratteri.
In alternativa: `./idf.sh menuconfig` → menu "Cercapersone".

## Build, flash, log

```bash
./idf.sh build
./idf.sh -p /dev/ttyACM0 flash monitor     # Ctrl+] per uscire dal monitor
```

Usa la porta USB marcata **UART** della scheda.

## Test della logica sul PC

```bash
make -C test_host        # in una shell senza ambiente ESP-IDF attivo
```

## Significato del LED

| LED | Significato |
|---|---|
| Blu che pulsa | Connessione al Wi-Fi o al server in corso |
| Spento | Connesso, a riposo |
| Lampo verde | La tua chiamata è partita |
| Arancione lampeggiante (10 s) | Qualcuno ha chiamato la pausa — premi il pulsante per spegnerlo |
| Rosso fisso | Server irraggiungibile da più di 60 s, o nome non valido (vedi log) |
````

- [ ] **Step 3: Compila, verifica i test host e flasha**

Run:
```bash
make -C firmware/test_host
cd firmware && ./idf.sh build && ./idf.sh -p /dev/ttyACM0 flash
```
Expected: test host `10 Tests 0 Failures` e `15 Tests 0 Failures`; `Project build complete`; flash completato.

- [ ] **Step 4: Prova end-to-end con l'utente**

Avvia server e log seriale in background come nel Task 8 Step 6 (log seriale per 120 s). Poi, in ordine:

1. Attendi `connesso a ws://...` nel log. Chiedi all'utente: il LED è passato da blu pulsante a spento?
2. Run: `server/.venv/bin/python server/tools/fake_pager.py --name finto --call --seconds 1`
   Expected: log `chiamata da finto`; l'utente vede l'arancione lampeggiare e spegnersi da solo dopo ~10 s.
3. Run: `server/.venv/bin/python server/tools/fake_pager.py --name finto2 --seconds 20 > <scratchpad>/finto2.log &` e chiedi all'utente di premere BOOT.
   Expected: lampo verde sulla scheda; log `chiamata inviata`; `finto2.log` contiene `ricevuto: {"type":"call","from":"scheda1",...}`.
4. Run: `curl -s -X POST http://127.0.0.1:8000/call -H 'Content-Type: application/json' -d '{"from":"pc"}'` e chiedi all'utente di premere BOOT mentre lampeggia arancione.
   Expected: il LED si spegne subito; nel log **non** compare `chiamata inviata`.
5. Arresta il server (`kill $SERVER_PID`), attendi 5 s, riavvialo con lo stesso comando.
   Expected: il LED torna blu pulsante, poi si spegne entro ~30 s dal riavvio; nel log `evento`/`connesso a ws://...` di nuovo.

- [ ] **Step 5: Verifica del nome non valido**

Imposta temporaneamente `CONFIG_PAGER_DEVICE_NAME="Scheda 1"` in `sdkconfig.local`, poi `rm sdkconfig && ./idf.sh build && ./idf.sh -p /dev/ttyACM0 flash` e leggi il log seriale per 10 s.
Expected: log `nome dispositivo non valido: "Scheda 1"`; l'utente conferma LED rosso fisso. Poi ripristina `CONFIG_PAGER_DEVICE_NAME="scheda1"`, `rm sdkconfig`, ricompila e riflasha.

- [ ] **Step 6: Commit**

```bash
git add firmware/main firmware/README.md
git commit -m "firmware: applicazione completa del cercapersone

Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>"
```
