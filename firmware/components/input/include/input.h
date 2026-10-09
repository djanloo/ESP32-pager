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
