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
