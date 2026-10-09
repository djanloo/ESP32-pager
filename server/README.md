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
