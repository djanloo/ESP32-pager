/* Cercapersone: collega pulsante, LED e server tramite la macchina a stati di pager_app.
 * Tutti gli eventi finiscono in una coda letta da un solo task, che possiede lo stato. */
#include <stdint.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input.h"
#include "notifier.h"
#include "nvs_flash.h"
#include "pager_app.h"
#include "pager_link.h"
#include "wifi_conn.h"

#define TICK_MS 100

static const char *TAG = "main";
static QueueHandle_t s_events;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    pager_event_t ev;
    if (base == INPUT_EVENT && id == INPUT_EVENT_PRESSED) {
        ev = PAGER_EV_BUTTON;
    } else if (base == INPUT_EVENT && id == INPUT_EVENT_DOUBLE_PRESSED) {
        ev = PAGER_EV_DOUBLE_BUTTON;
    } else if (base == PAGER_LINK_EVENT && id == PAGER_LINK_EVENT_UP) {
        ev = PAGER_EV_LINK_UP;
    } else if (base == PAGER_LINK_EVENT && id == PAGER_LINK_EVENT_DOWN) {
        ev = PAGER_EV_LINK_DOWN;
    } else if (base == PAGER_LINK_EVENT && id == PAGER_LINK_EVENT_CALL) {
        ev = PAGER_EV_CALL_RECEIVED;
    } else {
        return;
    }
    if (xQueueSend(s_events, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "coda eventi piena, evento %d perso", (int)ev);
    }
}

static void app_task(void *arg)
{
    pager_app_t app;
    pager_app_init(&app, now_ms());
    notifier_set(PAGER_LED_CONNECTING);

    for (;;) {
        pager_event_t ev;
        if (xQueueReceive(s_events, &ev, pdMS_TO_TICKS(TICK_MS)) != pdTRUE) {
            ev = PAGER_EV_TICK;
        }
        pager_output_t out = pager_app_handle(&app, ev, now_ms());
        if (out.send_call || out.send_demo) {
            const char *what = out.send_call ? "chiamata" : "chiamata dimostrativa";
            esp_err_t err = out.send_call ? pager_link_send_call() : pager_link_send_demo();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "%s inviata", what);
            } else {
                ESP_LOGW(TAG, "invio %s fallito: %s", what, esp_err_to_name(err));
                out = pager_app_handle(&app, PAGER_EV_SEND_FAILED, now_ms());
            }
        }
        notifier_set(out.led);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_events = xQueueCreate(16, sizeof(pager_event_t));
    ESP_ERROR_CHECK(s_events != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(notifier_start(CONFIG_PAGER_LED_GPIO));
    ESP_ERROR_CHECK(esp_event_handler_register(INPUT_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PAGER_LINK_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    /* Priorità 5 > app_main: parte subito e accende il LED blu prima di proseguire. */
    xTaskCreate(app_task, "app", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(input_start(CONFIG_PAGER_BUTTON_GPIO));
    ESP_ERROR_CHECK(wifi_conn_start(CONFIG_PAGER_WIFI_SSID, CONFIG_PAGER_WIFI_PASSWORD));

    ESP_LOGI(TAG, "cercapersone \"%s\" avviato", CONFIG_PAGER_DEVICE_NAME);
    err = pager_link_start(CONFIG_PAGER_SERVER_URL, CONFIG_PAGER_DEVICE_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "impossibile avviare il collegamento al server: %s", esp_err_to_name(err));
        notifier_set(PAGER_LED_ERROR);
    }
}
