#include "pager_led_mono.h"

bool pager_led_mono_on(pager_led_t pattern, uint32_t t_ms)
{
    switch (pattern) {
    case PAGER_LED_CONNECTING:
        return (t_ms / PAGER_MONO_SLOW_HALF_MS) % 2 == 0;
    case PAGER_LED_SENT:
        return t_ms < PAGER_MONO_SENT_MS;
    case PAGER_LED_ALERT:
        return (t_ms / PAGER_MONO_FAST_HALF_MS) % 2 == 0;
    case PAGER_LED_ERROR:
        return true;
    case PAGER_LED_OFF:
    case PAGER_LED_NO_CHANGE:
    default:
        return false;
    }
}
