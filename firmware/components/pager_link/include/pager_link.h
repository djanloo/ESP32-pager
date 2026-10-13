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

/* Invia una chiamata dimostrativa: il server non la inoltra e dopo ~1 s risponde
 * solo a questo dispositivo con una chiamata finta da "demo".
 * ESP_ERR_INVALID_STATE se non connesso. */
esp_err_t pager_link_send_demo(void);
