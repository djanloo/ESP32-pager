#include <string.h>

#include "pager_proto.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static bool parse_str(const char *s, pager_msg_t *out)
{
    return pager_proto_parse(s, strlen(s), out);
}

static void test_parse_full_call(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"call\",\"from\":\"giulia\",\"ts\":1760000000}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("giulia", m.from);
    TEST_ASSERT_TRUE(m.ts == 1760000000);
}

static void test_parse_uses_only_len_bytes(void)
{
    const char buf[] = "{\"type\":\"call\",\"from\":\"marco\"}SPAZZATURA";
    size_t len = strlen("{\"type\":\"call\",\"from\":\"marco\"}");
    pager_msg_t m;
    TEST_ASSERT_TRUE(pager_proto_parse(buf, len, &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("marco", m.from);
}

static void test_parse_call_without_from_or_ts(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"call\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("", m.from);
    TEST_ASSERT_TRUE(m.ts == 0);
}

static void test_parse_call_with_non_string_from(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"call\",\"from\":42,\"ts\":\"ieri\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("", m.from);
    TEST_ASSERT_TRUE(m.ts == 0);
}

static void test_parse_truncates_long_from(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"call\",\"from\":\"abcdefghijklmnopqrstuvwxyz0123456789\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_NAME_MAX, strlen(m.from));
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuvwxyz012345", m.from);
}

static void test_parse_ignores_extra_fields(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"nuovo\":[1,2],\"type\":\"call\",\"from\":\"a\",\"x\":{}}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_CALL, m.kind);
    TEST_ASSERT_EQUAL_STRING("a", m.from);
}

static void test_parse_unknown_type_is_ignored(void)
{
    pager_msg_t m;
    TEST_ASSERT_TRUE(parse_str("{\"type\":\"presence\",\"from\":\"a\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_IGNORED, m.kind);
    TEST_ASSERT_TRUE(parse_str("{\"from\":\"a\"}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_IGNORED, m.kind);
    TEST_ASSERT_TRUE(parse_str("{\"type\":1}", &m));
    TEST_ASSERT_EQUAL(PAGER_MSG_IGNORED, m.kind);
}

static void test_parse_rejects_non_objects(void)
{
    pager_msg_t m;
    TEST_ASSERT_FALSE(parse_str("non json", &m));
    TEST_ASSERT_FALSE(parse_str("{\"type\":\"call\"", &m));
    TEST_ASSERT_FALSE(parse_str("[1,2]", &m));
    TEST_ASSERT_FALSE(parse_str("\"call\"", &m));
    TEST_ASSERT_FALSE(pager_proto_parse("{}", 0, &m));
}

static void test_call_message(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"call\"}", pager_proto_call_message());
}

static void test_demo_message(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"demo\"}", pager_proto_demo_message());
}

static void test_valid_name(void)
{
    TEST_ASSERT_TRUE(pager_proto_valid_name("giulia"));
    TEST_ASSERT_TRUE(pager_proto_valid_name("a"));
    TEST_ASSERT_TRUE(pager_proto_valid_name("piano_2-nord"));
    TEST_ASSERT_TRUE(pager_proto_valid_name("abcdefghijklmnopqrstuvwxyz012345"));
    TEST_ASSERT_FALSE(pager_proto_valid_name(""));
    TEST_ASSERT_FALSE(pager_proto_valid_name("Giulia"));
    TEST_ASSERT_FALSE(pager_proto_valid_name("a b"));
    TEST_ASSERT_FALSE(pager_proto_valid_name("è"));
    TEST_ASSERT_FALSE(pager_proto_valid_name("abcdefghijklmnopqrstuvwxyz0123456"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_full_call);
    RUN_TEST(test_parse_uses_only_len_bytes);
    RUN_TEST(test_parse_call_without_from_or_ts);
    RUN_TEST(test_parse_call_with_non_string_from);
    RUN_TEST(test_parse_truncates_long_from);
    RUN_TEST(test_parse_ignores_extra_fields);
    RUN_TEST(test_parse_unknown_type_is_ignored);
    RUN_TEST(test_parse_rejects_non_objects);
    RUN_TEST(test_call_message);
    RUN_TEST(test_demo_message);
    RUN_TEST(test_valid_name);
    return UNITY_END();
}
