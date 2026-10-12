#include "pager_link.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "pager_netguard.h"
#include "pager_proto.h"

#define BACKOFF_MIN_MS 1000
#define BACKOFF_MAX_MS 30000
#define WS_OPCODE_TEXT 0x01

ESP_EVENT_DEFINE_BASE(PAGER_LINK_EVENT);

static const char *TAG = "pager_link";
static esp_websocket_client_handle_t s_client;
static int s_backoff_ms = BACKOFF_MIN_MS;
static char s_url[256];
static bool s_running;

static void handle_data(const esp_websocket_event_data_t *data)
{
    if (data->op_code != WS_OPCODE_TEXT) {
        return;  /* ping/pong/close e frame binari */
    }
    if (data->payload_offset != 0 || data->data_len != data->payload_len) {
        ESP_LOGW(TAG, "messaggio frammentato ignorato (%d byte)", data->payload_len);
        return;
    }
    pager_msg_t msg;
    if (!pager_proto_parse(data->data_ptr, data->data_len, &msg)) {
        ESP_LOGW(TAG, "messaggio non valido: %.*s", data->data_len, data->data_ptr);
        return;
    }
    if (msg.kind == PAGER_MSG_CALL) {
        ESP_LOGI(TAG, "chiamata da %s", msg.from[0] ? msg.from : "?");
        esp_event_post(PAGER_LINK_EVENT, PAGER_LINK_EVENT_CALL, &msg, sizeof(msg), pdMS_TO_TICKS(100));
    }
}

static void on_ws_event(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connesso a %s", s_url);
        s_backoff_ms = BACKOFF_MIN_MS;
        esp_websocket_client_set_reconnect_timeout(s_client, s_backoff_ms);
        esp_event_post(PAGER_LINK_EVENT, PAGER_LINK_EVENT_UP, NULL, 0, pdMS_TO_TICKS(100));
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGW(TAG, "server non raggiungibile, nuovo tentativo tra %d ms", s_backoff_ms);
        esp_websocket_client_set_reconnect_timeout(s_client, s_backoff_ms);
        s_backoff_ms = s_backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : s_backoff_ms * 2;
        esp_event_post(PAGER_LINK_EVENT, PAGER_LINK_EVENT_DOWN, NULL, 0, pdMS_TO_TICKS(100));
        break;
    case WEBSOCKET_EVENT_DATA:
        handle_data(event_data);
        break;
    default:
        break;
    }
}

/* Gira sul loop eventi di default, non sul task del client: qui stop() è ammesso. */
static void on_net_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    pager_net_event_t ev = (base == WIFI_EVENT) ? PAGER_NET_WIFI_LOST : PAGER_NET_IP_GOT;
    switch (pager_netguard_handle(&s_running, ev)) {
    case PAGER_NET_STOP_LINK:
        ESP_LOGW(TAG, "Wi-Fi perso: chiudo il collegamento al server");
        esp_websocket_client_stop(s_client);
        esp_event_post(PAGER_LINK_EVENT, PAGER_LINK_EVENT_DOWN, NULL, 0, pdMS_TO_TICKS(100));
        break;
    case PAGER_NET_START_LINK:
        ESP_LOGI(TAG, "Wi-Fi tornato: riapro il collegamento al server");
        s_backoff_ms = BACKOFF_MIN_MS;
        esp_websocket_client_set_reconnect_timeout(s_client, s_backoff_ms);
        esp_websocket_client_start(s_client);
        break;
    case PAGER_NET_NONE:
        break;
    }
}

esp_err_t pager_link_start(const char *server_url, const char *device_name)
{
    if (!pager_proto_valid_name(device_name)) {
        ESP_LOGE(TAG, "nome dispositivo non valido: \"%s\" (ammessi a-z 0-9 _ -, max 32)", device_name);
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(s_url, sizeof(s_url), "%s?name=%s", server_url, device_name);
    ESP_RETURN_ON_FALSE(n > 0 && n < (int)sizeof(s_url), ESP_ERR_INVALID_SIZE, TAG, "URL troppo lungo");

    esp_websocket_client_config_t cfg = {
        .uri = s_url,
        .reconnect_timeout_ms = BACKOFF_MIN_MS,
        .network_timeout_ms = 10000,
        .ping_interval_sec = 20,
        .pingpong_timeout_sec = 60,
        .enable_close_reconnect = true,  /* riconnetti anche dopo una chiusura pulita del server */
        .crt_bundle_attach = esp_crt_bundle_attach,  /* per wss://: CA pubbliche incluse in ESP-IDF */
    };
    s_client = esp_websocket_client_init(&cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_FAIL, TAG, "init client");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws_event, NULL),
                        TAG, "eventi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_net_event, NULL),
                        TAG, "eventi Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_net_event, NULL),
                        TAG, "eventi IP");
    s_running = true;
    return esp_websocket_client_start(s_client);
}

esp_err_t pager_link_send_call(void)
{
    if (s_client == NULL || !esp_websocket_client_is_connected(s_client)) {
        return ESP_ERR_INVALID_STATE;
    }
    const char *msg = pager_proto_call_message();
    int sent = esp_websocket_client_send_text(s_client, msg, strlen(msg), pdMS_TO_TICKS(1000));
    return sent < 0 ? ESP_FAIL : ESP_OK;
}
