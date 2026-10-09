#include "notifier.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_PAGER_LED_TYPE_GPIO
#include "driver/gpio.h"
#include "pager_led_mono.h"
#else
#include "led_strip.h"
#endif

#define FRAME_MS 20
#define SENT_FLASH_MS 300
#define BLINK_HALF_PERIOD_MS 250
#define PULSE_PERIOD_MS 2000
#define BRIGHT 32  /* luminosità massima (0-255): il LED è molto forte */

static const char *TAG = "notifier";
static QueueHandle_t s_queue;

#if CONFIG_PAGER_LED_TYPE_GPIO

/* LED a un solo colore: i pattern diventano ritmi di accensione (vedi pager_led_mono.h). */
static int s_gpio;

static esp_err_t init_led(int gpio)
{
    s_gpio = gpio;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "config GPIO LED");
    return gpio_set_level(gpio, 0);
}

static void render(pager_led_t pattern, uint32_t t)
{
    gpio_set_level(s_gpio, pager_led_mono_on(pattern, t) ? 1 : 0);
}

#else

static led_strip_handle_t s_strip;

static esp_err_t init_led(int gpio)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    return led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
}

static void show(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void render(pager_led_t pattern, uint32_t t)
{
    switch (pattern) {
    case PAGER_LED_CONNECTING: {
        uint32_t phase = t % PULSE_PERIOD_MS;
        uint32_t half = PULSE_PERIOD_MS / 2;
        uint32_t level = phase < half ? phase : PULSE_PERIOD_MS - phase;  /* 0..half */
        show(0, 0, (uint8_t)(level * BRIGHT / half));
        break;
    }
    case PAGER_LED_SENT:
        show(0, BRIGHT, 0);
        break;
    case PAGER_LED_ALERT:
        if ((t / BLINK_HALF_PERIOD_MS) % 2 == 0) {
            show(BRIGHT, BRIGHT / 4, 0);
        } else {
            show(0, 0, 0);
        }
        break;
    case PAGER_LED_ERROR:
        show(BRIGHT, 0, 0);
        break;
    case PAGER_LED_OFF:
    case PAGER_LED_NO_CHANGE:
    default:
        show(0, 0, 0);
        break;
    }
}

#endif

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void notifier_task(void *arg)
{
    pager_led_t pattern = PAGER_LED_OFF;
    int64_t started_ms = now_ms();

    for (;;) {
        pager_led_t requested;
        if (xQueueReceive(s_queue, &requested, pdMS_TO_TICKS(FRAME_MS)) == pdTRUE) {
            pattern = requested;
            started_ms = now_ms();
        }
        uint32_t t = (uint32_t)(now_ms() - started_ms);
        if (pattern == PAGER_LED_SENT && t >= SENT_FLASH_MS) {
            pattern = PAGER_LED_OFF;  /* il lampo verde dura poco, poi si spegne */
        }
        render(pattern, t);
    }
}

esp_err_t notifier_start(int gpio)
{
    ESP_RETURN_ON_ERROR(init_led(gpio), TAG, "init LED");

    s_queue = xQueueCreate(1, sizeof(pager_led_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "coda");
    ESP_RETURN_ON_FALSE(xTaskCreate(notifier_task, "notifier", 3072, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

void notifier_set(pager_led_t pattern)
{
    if (pattern == PAGER_LED_NO_CHANGE || s_queue == NULL) {
        return;
    }
    xQueueOverwrite(s_queue, &pattern);
}
