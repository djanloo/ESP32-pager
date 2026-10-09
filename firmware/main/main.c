/* Demo temporanea (Task 8): connessione al server e log degli eventi. */
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "pager_link.h"
#include "pager_proto.h"
#include "wifi_conn.h"

static const char *TAG = "main";

static void on_link(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == PAGER_LINK_EVENT_UP) {
        ESP_LOGI(TAG, "evento: UP");
    } else if (id == PAGER_LINK_EVENT_DOWN) {
        ESP_LOGI(TAG, "evento: DOWN");
    } else if (id == PAGER_LINK_EVENT_CALL) {
        const pager_msg_t *msg = data;
        ESP_LOGI(TAG, "evento: CALL da %s", msg->from);
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
    ESP_ERROR_CHECK(esp_event_handler_register(PAGER_LINK_EVENT, ESP_EVENT_ANY_ID, on_link, NULL));
    ESP_ERROR_CHECK(wifi_conn_start(CONFIG_PAGER_WIFI_SSID, CONFIG_PAGER_WIFI_PASSWORD));
    ESP_ERROR_CHECK(pager_link_start(CONFIG_PAGER_SERVER_URL, CONFIG_PAGER_DEVICE_NAME));
}
