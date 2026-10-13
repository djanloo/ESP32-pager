#include "pager_app.h"
#include "unity.h"

static pager_app_t app;

void setUp(void) {}
void tearDown(void) {}

/* Porta l'app in IDLE all'istante t. */
static void start_idle(uint32_t t)
{
    pager_app_init(&app, t);
    pager_app_handle(&app, PAGER_EV_LINK_UP, t);
}

static void test_init_is_connecting(void)
{
    pager_app_init(&app, 0);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
}

static void test_link_up_goes_idle_with_led_off(void)
{
    pager_app_init(&app, 0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_LINK_UP, 100);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
    TEST_ASSERT_FALSE(out.send_call);
}

static void test_button_when_idle_sends_call(void)
{
    start_idle(0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_BUTTON, 200);
    TEST_ASSERT_TRUE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_SENT, out.led);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_button_when_connecting_does_nothing(void)
{
    pager_app_init(&app, 0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_BUTTON, 200);
    TEST_ASSERT_FALSE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_NO_CHANGE, out.led);
}

static void test_double_button_when_idle_sends_demo(void)
{
    start_idle(0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_DOUBLE_BUTTON, 200);
    TEST_ASSERT_TRUE(out.send_demo);
    TEST_ASSERT_FALSE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_SENT, out.led);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_single_button_does_not_send_demo(void)
{
    start_idle(0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_BUTTON, 200);
    TEST_ASSERT_FALSE(out.send_demo);
}

static void test_double_button_when_connecting_does_nothing(void)
{
    pager_app_init(&app, 0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_DOUBLE_BUTTON, 200);
    TEST_ASSERT_FALSE(out.send_demo);
    TEST_ASSERT_FALSE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_NO_CHANGE, out.led);
}

static void test_double_button_during_alert_dismisses_without_sending(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_DOUBLE_BUTTON, 2000);
    TEST_ASSERT_FALSE(out.send_demo);
    TEST_ASSERT_FALSE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_call_received_when_idle_alerts(void)
{
    start_idle(0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_ALERT, out.led);
}

static void test_call_received_when_connecting_is_ignored(void)
{
    pager_app_init(&app, 0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_NO_CHANGE, out.led);
}

static void test_button_during_alert_dismisses_without_calling(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_BUTTON, 2000);
    TEST_ASSERT_FALSE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_alert_ends_after_timeout(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_ALERT_MS - 1);
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_NO_CHANGE, out.led);
    out = pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_ALERT_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
}

static void test_new_call_during_alert_restarts_timer(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 6000);
    TEST_ASSERT_EQUAL(PAGER_LED_ALERT, out.led);
    pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_ALERT_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    pager_app_handle(&app, PAGER_EV_TICK, 6000 + PAGER_ALERT_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_alert_timeout_across_wraparound(void)
{
    uint32_t t = 0xFFFFF000u;  /* 4096 ms prima dell'overflow */
    start_idle(t);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, t);
    pager_app_handle(&app, PAGER_EV_TICK, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    pager_app_handle(&app, PAGER_EV_TICK, t + PAGER_ALERT_MS - 1);  /* dopo l'overflow */
    TEST_ASSERT_EQUAL(PAGER_STATE_ALERTING, app.state);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_TICK, t + PAGER_ALERT_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
}

static void test_link_down_goes_connecting(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_CALL_RECEIVED, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_LINK_DOWN, 2000);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_CONNECTING, out.led);
}

static void test_offline_too_long_shows_error(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_LINK_DOWN, 1000);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_OFFLINE_ERROR_MS - 1);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
    out = pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_OFFLINE_ERROR_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_OFFLINE_ERROR, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_ERROR, out.led);
}

static void test_never_connected_shows_error_after_timeout(void)
{
    pager_app_init(&app, 500);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_TICK, 500 + PAGER_OFFLINE_ERROR_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_OFFLINE_ERROR, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_ERROR, out.led);
}

static void test_repeated_link_down_does_not_reset_offline_timer(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_LINK_DOWN, 1000);
    pager_app_handle(&app, PAGER_EV_LINK_DOWN, 30000);
    pager_app_handle(&app, PAGER_EV_TICK, 1000 + PAGER_OFFLINE_ERROR_MS);
    TEST_ASSERT_EQUAL(PAGER_STATE_OFFLINE_ERROR, app.state);
}

static void test_link_up_after_error_goes_idle(void)
{
    pager_app_init(&app, 0);
    pager_app_handle(&app, PAGER_EV_TICK, PAGER_OFFLINE_ERROR_MS);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_LINK_UP, PAGER_OFFLINE_ERROR_MS + 5000);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
}

static void test_send_failed_cancels_green_flash(void)
{
    start_idle(0);
    pager_app_handle(&app, PAGER_EV_BUTTON, 100);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_SEND_FAILED, 100);
    TEST_ASSERT_EQUAL(PAGER_LED_OFF, out.led);
    TEST_ASSERT_FALSE(out.send_call);
    TEST_ASSERT_EQUAL(PAGER_STATE_IDLE, app.state);
}

static void test_send_failed_outside_idle_changes_nothing(void)
{
    pager_app_init(&app, 0);
    pager_output_t out = pager_app_handle(&app, PAGER_EV_SEND_FAILED, 100);
    TEST_ASSERT_EQUAL(PAGER_LED_NO_CHANGE, out.led);
    TEST_ASSERT_EQUAL(PAGER_STATE_CONNECTING, app.state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_is_connecting);
    RUN_TEST(test_link_up_goes_idle_with_led_off);
    RUN_TEST(test_button_when_idle_sends_call);
    RUN_TEST(test_button_when_connecting_does_nothing);
    RUN_TEST(test_double_button_when_idle_sends_demo);
    RUN_TEST(test_single_button_does_not_send_demo);
    RUN_TEST(test_double_button_when_connecting_does_nothing);
    RUN_TEST(test_double_button_during_alert_dismisses_without_sending);
    RUN_TEST(test_call_received_when_idle_alerts);
    RUN_TEST(test_call_received_when_connecting_is_ignored);
    RUN_TEST(test_button_during_alert_dismisses_without_calling);
    RUN_TEST(test_alert_ends_after_timeout);
    RUN_TEST(test_new_call_during_alert_restarts_timer);
    RUN_TEST(test_alert_timeout_across_wraparound);
    RUN_TEST(test_link_down_goes_connecting);
    RUN_TEST(test_offline_too_long_shows_error);
    RUN_TEST(test_never_connected_shows_error_after_timeout);
    RUN_TEST(test_repeated_link_down_does_not_reset_offline_timer);
    RUN_TEST(test_link_up_after_error_goes_idle);
    RUN_TEST(test_send_failed_cancels_green_flash);
    RUN_TEST(test_send_failed_outside_idle_changes_nothing);
    return UNITY_END();
}
