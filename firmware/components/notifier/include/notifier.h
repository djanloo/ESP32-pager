#pragma once

#include "esp_err.h"
#include "pager_app.h"

/* Inizializza il LED WS2812 sul GPIO indicato e avvia il task che lo anima. */
esp_err_t notifier_start(int gpio);

/* Cambia il pattern luminoso. PAGER_LED_NO_CHANGE viene ignorato. */
void notifier_set(pager_led_t pattern);
