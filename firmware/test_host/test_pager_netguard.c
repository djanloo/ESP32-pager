#include "pager_netguard.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_wifi_lost_stops_running_link(void)
{
    bool running = true;
    TEST_ASSERT_EQUAL(PAGER_NET_STOP_LINK, pager_netguard_handle(&running, PAGER_NET_WIFI_LOST));
    TEST_ASSERT_FALSE(running);
}

static void test_repeated_wifi_lost_does_nothing(void)
{
    bool running = false;
    TEST_ASSERT_EQUAL(PAGER_NET_NONE, pager_netguard_handle(&running, PAGER_NET_WIFI_LOST));
    TEST_ASSERT_FALSE(running);
}

static void test_ip_got_restarts_stopped_link(void)
{
    bool running = false;
    TEST_ASSERT_EQUAL(PAGER_NET_START_LINK, pager_netguard_handle(&running, PAGER_NET_IP_GOT));
    TEST_ASSERT_TRUE(running);
}

static void test_ip_got_with_running_link_does_nothing(void)
{
    bool running = true;
    TEST_ASSERT_EQUAL(PAGER_NET_NONE, pager_netguard_handle(&running, PAGER_NET_IP_GOT));
    TEST_ASSERT_TRUE(running);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wifi_lost_stops_running_link);
    RUN_TEST(test_repeated_wifi_lost_does_nothing);
    RUN_TEST(test_ip_got_restarts_stopped_link);
    RUN_TEST(test_ip_got_with_running_link_does_nothing);
    return UNITY_END();
}
