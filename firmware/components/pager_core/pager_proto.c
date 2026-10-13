#include "pager_proto.h"

#include <string.h>

#include "cJSON.h"

bool pager_proto_parse(const char *data, size_t len, pager_msg_t *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = PAGER_MSG_IGNORED;

    if (len == 0) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        return false;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "call") == 0) {
        out->kind = PAGER_MSG_CALL;

        const cJSON *from = cJSON_GetObjectItemCaseSensitive(root, "from");
        if (cJSON_IsString(from)) {
            size_t n = strlen(from->valuestring);
            if (n > PAGER_NAME_MAX) {
                n = PAGER_NAME_MAX;
            }
            memcpy(out->from, from->valuestring, n);
            out->from[n] = '\0';
        }

        const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
        if (cJSON_IsNumber(ts)) {
            out->ts = (int64_t)ts->valuedouble;
        }
    }

    cJSON_Delete(root);
    return true;
}

const char *pager_proto_call_message(void)
{
    return "{\"type\":\"call\"}";
}

const char *pager_proto_demo_message(void)
{
    return "{\"type\":\"demo\"}";
}

bool pager_proto_valid_name(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > PAGER_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}
