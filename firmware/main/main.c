/* Demo temporanea (Task 7): ogni pressione del pulsante passa al pattern successivo. */
#include "esp_event.h"
#include "esp_log.h"
#include "input.h"
#include "notifier.h"

static const char *TAG = "main";

static const pager_led_t DEMO[] = {
    PAGER_LED_CONNECTING, PAGER_LED_SENT, PAGER_LED_ALERT, PAGER_LED_ERROR, PAGER_LED_OFF,
};
static const char *DEMO_NAMES[] = { "connessione", "inviata", "avviso", "errore", "spento" };

static void on_press(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    static int i = 0;
    ESP_LOGI(TAG, "pattern: %s", DEMO_NAMES[i]);
    notifier_set(DEMO[i]);
    i = (i + 1) % (int)(sizeof(DEMO) / sizeof(DEMO[0]));
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(notifier_start(CONFIG_PAGER_LED_GPIO));
    ESP_ERROR_CHECK(esp_event_handler_register(INPUT_EVENT, INPUT_EVENT_PRESSED, on_press, NULL));
    ESP_ERROR_CHECK(input_start(CONFIG_PAGER_BUTTON_GPIO));
    ESP_LOGI(TAG, "demo LED pronta: premi BOOT");
}
