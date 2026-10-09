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
    PAGER_EV_SEND_FAILED,  /* l'invio della chiamata chiesto da PAGER_EV_BUTTON è fallito */
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
