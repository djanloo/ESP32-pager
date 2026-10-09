#pragma once

#include "esp_err.h"

/* Connette la scheda al Wi-Fi (WPA2/WPA3-Personal) e la tiene connessa,
 * riprovando ogni 2 s se la connessione cade.
 * Richiede nvs_flash_init() ed esp_event_loop_create_default() già eseguiti. */
esp_err_t wifi_conn_start(const char *ssid, const char *password);
