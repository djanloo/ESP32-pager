#include "input.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POLL_MS 10
#define STABLE_SAMPLES 5  /* 5 letture uguali = 50 ms di debounce */

ESP_EVENT_DEFINE_BASE(INPUT_EVENT);

static const char *TAG = "input";

static void input_task(void *arg)
{
    int gpio = (int)(intptr_t)arg;
    bool last_raw = false;
    bool stable_pressed = false;
    int same_count = 0;

    for (;;) {
        bool raw = gpio_get_level(gpio) == 0;
        if (raw == last_raw) {
            if (same_count < STABLE_SAMPLES) {
                same_count++;
            }
        } else {
            last_raw = raw;
            same_count = 0;
        }

        if (same_count == STABLE_SAMPLES && raw != stable_pressed) {
            stable_pressed = raw;
            if (stable_pressed) {
                ESP_LOGI(TAG, "pulsante premuto");
                esp_event_post(INPUT_EVENT, INPUT_EVENT_PRESSED, NULL, 0, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t input_start(int gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "config GPIO");
    ESP_RETURN_ON_FALSE(xTaskCreate(input_task, "input", 2560, (void *)(intptr_t)gpio, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}
