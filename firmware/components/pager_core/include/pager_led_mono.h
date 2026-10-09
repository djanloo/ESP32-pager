#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pager_app.h"

/* Resa dei pattern su un LED a un solo colore (es. LED blu su GPIO2 degli ESP32). */
#define PAGER_MONO_SLOW_HALF_MS 1000u  /* in connessione: 1 s acceso, 1 s spento */
#define PAGER_MONO_FAST_HALF_MS 150u   /* avviso: lampeggio veloce */
#define PAGER_MONO_SENT_MS 300u        /* chiamata inviata: un lampo */

/* true se il LED deve essere acceso, t_ms millisecondi dopo l'inizio del pattern. */
bool pager_led_mono_on(pager_led_t pattern, uint32_t t_ms);
