#include "pager_led_mono.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_off_is_always_off(void)
{
    TEST_ASSERT_FALSE(pager_led_mono_on(PAGER_LED_OFF, 0));
    TEST_ASSERT_FALSE(pager_led_mono_on(PAGER_LED_OFF, 12345));
    TEST_ASSERT_FALSE(pager_led_mono_on(PAGER_LED_NO_CHANGE, 0));
}

static void test_error_is_always_on(void)
{
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_ERROR, 0));
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_ERROR, 999999));
}

static void test_connecting_blinks_slowly(void)
{
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_CONNECTING, 0));
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_CONNECTING, PAGER_MONO_SLOW_HALF_MS - 1));
    TEST_ASSERT_FALSE(pager_led_mono_on(PAGER_LED_CONNECTING, PAGER_MONO_SLOW_HALF_MS));
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_CONNECTING, 2 * PAGER_MONO_SLOW_HALF_MS));
}

static void test_alert_blinks_fast(void)
{
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_ALERT, 0));
    TEST_ASSERT_FALSE(pager_led_mono_on(PAGER_LED_ALERT, PAGER_MONO_FAST_HALF_MS));
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_ALERT, 2 * PAGER_MONO_FAST_HALF_MS));
    TEST_ASSERT_TRUE(PAGER_MONO_FAST_HALF_MS * 3 <= PAGER_MONO_SLOW_HALF_MS);  /* distinguibili a occhio */
}

static void test_sent_is_a_single_short_flash(void)
{
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_SENT, 0));
    TEST_ASSERT_TRUE(pager_led_mono_on(PAGER_LED_SENT, PAGER_MONO_SENT_MS - 1));
    TEST_ASSERT_FALSE(pager_led_mono_on(PAGER_LED_SENT, PAGER_MONO_SENT_MS));
    TEST_ASSERT_FALSE(pager_led_mono_on(PAGER_LED_SENT, 10 * PAGER_MONO_SENT_MS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_is_always_off);
    RUN_TEST(test_error_is_always_on);
    RUN_TEST(test_connecting_blinks_slowly);
    RUN_TEST(test_alert_blinks_fast);
    RUN_TEST(test_sent_is_a_single_short_flash);
    return UNITY_END();
}
