#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static char * dup_range(const char * start, const char * end) {
    size_t len = (size_t)(end - start);
    char * s = (char *)malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

cJSON * cJSON_Parse(const char * text) {
    if (!text) return NULL;
    const char * p = text;
    cJSON * head = NULL;
    cJSON * tail = NULL;
    while ((p = strstr(p, "\"")) != NULL) {
        const char * key_start = p + 1;
        const char * key_end = strchr(key_start, '\"');
        if (!key_end) break;
        const char * colon = strchr(key_end, ':');
        if (!colon) break;
        const char * val_start = strchr(colon, '\"');
        if (!val_start) { p = key_end + 1; continue; }
        val_start++;
        const char * val_end = strchr(val_start, '\"');
        if (!val_end) break;

        cJSON * node = (cJSON *)calloc(1, sizeof(cJSON));
        if (!node) break;
        node->type = 1; /* string */
        node->string = dup_range(key_start, key_end);
        node->valuestring = dup_range(val_start, val_end);
        if (!head) head = node;
        if (tail) tail->next = node;
        tail = node;
        p = val_end + 1;
    }
    return head;
}

cJSON * cJSON_GetObjectItemCaseSensitive(const cJSON * object, const char * string) {
    const cJSON * p = object;
    while (p) {
        if (p->string && strcmp(p->string, string) == 0) {
            return (cJSON *)p;
        }
        p = p->next;
    }
    return NULL;
}

bool cJSON_IsString(const cJSON * item) {
    return item && item->type == 1 && item->valuestring != NULL;
}

void cJSON_Delete(cJSON * item) {
    while (item) {
        cJSON * next = item->next;
        free(item->string);
        free(item->valuestring);
        free(item);
        item = next;
    }
}
