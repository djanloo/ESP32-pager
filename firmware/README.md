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
| Lampo verde | La tua chiamata è partita (doppia pressione: chiamata dimostrativa, non arriva a nessuno; dopo ~1 s il server ti risponde con una chiamata finta da "demo") |
| Arancione lampeggiante (10 s) | Qualcuno ha chiamato la pausa — premi il pulsante per spegnerlo |
| Rosso fisso | Server irraggiungibile da più di 60 s, o nome non valido (vedi log) |
