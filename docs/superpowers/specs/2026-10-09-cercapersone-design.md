# Cercapersone per la pausa — Design

Data: 2026-10-09

## 1. Obiettivo

Un piccolo cercapersone per un gruppo di colleghi (fino a ~20 persone) per
chiamare la pausa. Chi preme il pulsante sul proprio dispositivo fa partire un
avviso su tutti gli altri dispositivi. La comunicazione passa da un servizio
centrale che gira in un pod podman sulla rete interna aziendale.

**Criterio di successo:** premendo il pulsante su un cercapersone, tutti gli
altri cercapersone connessi mostrano l'avviso entro ~1 secondo; chi ha chiamato
non riceve il proprio avviso.

### Nell'ambito (v1)

- Chiamata di gruppo: un pulsante → avviso a tutti gli altri.
- Ogni dispositivo ha un nome (es. `giulia`), incluso nell'avviso.
- Hardware: solo la scheda ESP32-S3 DevKit (pulsante BOOT + LED RGB integrato),
  con firmware strutturato per aggiungere buzzer/display in seguito.
- Wi-Fi WPA2/WPA3-Personal.
- Server FastAPI in container, eseguito nel pod podman.
- Endpoint HTTP `POST /call` per chiamare la pausa da PC/script.

### Fuori ambito (da fare dopo)

- Sicurezza/autenticazione (token condiviso o per dispositivo, TLS). Il
  protocollo è pensato per aggiungerla con un parametro in più (`token` nella
  query del WebSocket, header `Authorization` su `/call`) senza altri cambi.
- Risposte alla chiamata ("vengo"/"non posso"), chiamate mirate, presenza.
- Recupero delle chiamate perse da dispositivi offline.
- Buzzer, display, pulsanti esterni.
- Configurazione del nome via seriale/NVS (in v1 si usa `menuconfig`).
- Wi-Fi WPA2-Enterprise.

## 2. Architettura

```
 [Cercapersone ESP32] ──WebSocket──┐
 [Cercapersone ESP32] ──WebSocket──┼──> [FastAPI nel pod podman]
 [Cercapersone ESP32] ──WebSocket──┘          ▲
 [PC / curl] ───────── POST /call ────────────┘
```

Ogni cercapersone mantiene una connessione WebSocket permanente con il server.
Il server inoltra ogni chiamata a tutti i dispositivi connessi tranne il
mittente. Tutta la comunicazione è HTTP/WS in chiaro sulla rete interna.

## 3. Protocollo

### Endpoint HTTP

| Metodo | Percorso | Descrizione | Risposta |
|---|---|---|---|
| GET (upgrade WS) | `/ws?name=<nome>` | Connessione permanente di un cercapersone | — |
| POST | `/call` | Body `{"from": "<nome>"}`. Inoltra la chiamata a tutti i dispositivi connessi tranne quello di nome `<nome>` (se connesso) | `200 {"delivered": <n>}`; `429` se bloccata dall'anti-spam; `422` se il body non è valido |
| GET | `/health` | Healthcheck | `200 {"status": "ok"}` |
| GET | `/devices` | Dispositivi connessi | `200 {"devices": ["giulia", "marco"]}` |

Il parametro `name` è obbligatorio: stringa di 1–32 caratteri `[a-z0-9_-]`.
Se manca o non è valido, il server chiude il WebSocket con codice `1008`.

### Messaggi WebSocket (JSON, testo)

Dispositivo → server:

```json
{"type": "call"}
```

Server → dispositivi (a tutti tranne il mittente):

```json
{"type": "call", "from": "giulia", "ts": 1760000000}
```

`ts` è il timestamp Unix (secondi) del server al momento della chiamata.

I campi sconosciuti vengono ignorati da entrambe le parti, per permettere di
estendere il protocollo.

### Keepalive

Il client invia ping WebSocket ogni 20 s. Una connessione senza traffico (né
ping né messaggi) per 60 s viene considerata morta e rimossa dal server. Il
client si riconnette autonomamente (vedi §5).

### Regole del server

- **Stato solo in memoria:** registro `nome → connessione`. Nessun database. Al
  riavvio del pod i dispositivi si riconnettono da soli.
- **Nome duplicato:** una nuova connessione con un nome già presente sostituisce
  la precedente, che viene chiusa con codice `1000`.
- **Anti-spam:** al massimo una chiamata ogni `CALL_COOLDOWN_S` secondi (default
  10) per nome mittente, sia via WebSocket sia via `/call`. Le chiamate in
  eccesso sono ignorate (WS) o rifiutate con `429` (HTTP) e loggate.
- **Chiamate perse:** non vengono recuperate. Un dispositivo offline al momento
  della chiamata non riceve l'avviso.

## 4. Server (FastAPI)

### Struttura

| File | Compito |
|---|---|
| `server/app/hub.py` | Classe `Hub`: registro dei dispositivi connessi, `broadcast(from_name)` con esclusione del mittente, anti-spam. Logica indipendente da FastAPI, testabile con connessioni finte |
| `server/app/main.py` | App FastAPI: endpoint `/ws`, `/call`, `/health`, `/devices`, che delegano a `Hub` |
| `server/tests/` | Test `pytest` |
| `server/requirements.txt` | `fastapi`, `uvicorn[standard]` (+ `pytest`, `httpx` per i test) |
| `server/Containerfile` | Immagine `python:3.12-slim`, avvio con `uvicorn` sulla porta 8000 |

### Configurazione (variabili d'ambiente)

| Variabile | Default | Significato |
|---|---|---|
| `PORT` | `8000` | Porta di ascolto |
| `CALL_COOLDOWN_S` | `10` | Intervallo minimo tra due chiamate dello stesso mittente |

### Gestione errori

- Messaggio WS non JSON o con `type` sconosciuto: ignorato e loggato, la
  connessione resta aperta.
- Errore di invio a un dispositivo durante il broadcast: il dispositivo viene
  rimosso dal registro, l'invio agli altri prosegue.
- Log su stdout (connessioni, disconnessioni, chiamate, errori), leggibili con
  `podman logs`.

### Deploy

Il gestore del pod costruisce l'immagine con `podman build -f Containerfile`,
aggiunge il container al pod e pubblica la porta 8000 sulla rete interna. Il
container espone `GET /health` per l'healthcheck.

## 5. Firmware (ESP-IDF 5.x)

Target: ESP32-S3 DevKit (WROOM). Flash e monitor seriale dalla porta UART.

### Componenti

| Componente | Compito | Dipende da |
|---|---|---|
| `config` | Espone SSID, password, URL del server, nome del dispositivo da Kconfig (`menuconfig`) | — |
| `wifi_conn` | Connessione WPA2-Personal, riconnessione automatica, eventi connesso/disconnesso | `config` |
| `pager_link` | Client WebSocket (`esp_websocket_client`) verso `ws://<server>/ws?name=<nome>`; riconnessione con backoff esponenziale 1 s → 30 s; `send_call()`; parsing dei messaggi in arrivo e pubblicazione come eventi | `config`, `wifi_conn` |
| `input` | Pulsante BOOT (GPIO0) con debounce (~50 ms); evento "pulsante premuto" | — |
| `notifier` | LED WS2812 su GPIO48 tramite componente `led_strip`; esegue i pattern luminosi | — |
| `main` | Macchina a stati dell'applicazione: collega eventi di input, link e notifier | tutti |

I componenti comunicano tramite il loop eventi di ESP-IDF (`esp_event`). Una
futura uscita (buzzer, display) sarà un nuovo componente che si iscrive
all'evento "chiamata ricevuta", senza modificare gli altri.

La logica pura (parsing dei messaggi JSON, macchina a stati di `main`) è
separata dal codice che tocca l'hardware, per poterla testare.

### Configurazione (Kconfig)

| Opzione | Esempio |
|---|---|
| `PAGER_WIFI_SSID` | `UfficioWiFi` |
| `PAGER_WIFI_PASSWORD` | — |
| `PAGER_SERVER_URL` | `ws://10.0.0.50:8000/ws` |
| `PAGER_DEVICE_NAME` | `giulia` |

Le credenziali reali stanno in `sdkconfig` locale, escluso da git; nel repo c'è
solo `sdkconfig.defaults` con valori segnaposto.

### Comportamento

| Stato / evento | LED | Pulsante |
|---|---|---|
| In connessione (Wi-Fi o server) | Blu, pulsazione lenta | Ignorato |
| Connesso, a riposo | Spento | Invia chiamata |
| Chiamata inviata | Verde, lampo breve (~300 ms) | — |
| Chiamata ricevuta | Arancione lampeggiante per 10 s, poi spento | Spegne l'avviso (presa visione), **non** invia una chiamata |
| Server irraggiungibile da > 60 s | Rosso fisso (continua a tentare la riconnessione) | Ignorato |

Una nuova chiamata ricevuta durante un avviso già attivo fa ripartire i 10 s.

## 6. Test

### Server

- Unitari su `Hub`: broadcast esclude il mittente; anti-spam blocca la seconda
  chiamata entro il cooldown e accetta quella successiva; nome duplicato
  sostituisce la connessione precedente; errore di invio rimuove il dispositivo.
- Integrazione con `TestClient` di FastAPI: due WebSocket, una chiamata arriva
  solo all'altro; `POST /call` consegna ai WebSocket connessi e risponde con il
  numero di consegne; `429` sul secondo `POST /call` ravvicinato; `/devices`
  e `/health`.

### Firmware

- Unitari (Unity, ESP-IDF) sulla logica pura: parsing dei messaggi (valido,
  JSON malformato, `type` sconosciuto, campi extra), transizioni della macchina
  a stati (pulsante a riposo → chiamata; pulsante durante avviso → spegnimento;
  nuova chiamata durante avviso → riavvio timer).
- Verifica manuale sulla scheda per Wi-Fi, LED e pulsante.

### End-to-end (una sola scheda, senza il pod)

1. Server in locale sul PC (`uvicorn`), scheda configurata con l'IP del PC.
2. `curl -X POST http://<pc>:8000/call -d '{"from":"test"}'` → la scheda
   lampeggia arancione.
3. Un secondo client (`websocat ws://<pc>:8000/ws?name=finto`) riceve la
   chiamata quando si preme il pulsante della scheda.
4. Arresto e riavvio del server → la scheda passa a blu, poi torna a riposo
   dopo la riconnessione.

## 7. Struttura del repository

```
test_cercapersone/
├── docs/superpowers/specs/   specifiche
├── server/                   FastAPI + Containerfile + test
└── firmware/                 progetto ESP-IDF
```
