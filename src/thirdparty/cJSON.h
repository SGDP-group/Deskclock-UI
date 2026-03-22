#ifndef CJSON_H
#define CJSON_H

#include <stdbool.h>

typedef struct cJSON {
    int type;
    char * string;
    char * valuestring;
    struct cJSON * next;
} cJSON;

cJSON * cJSON_Parse(const char * text);
cJSON * cJSON_GetObjectItemCaseSensitive(const cJSON * object, const char * string);
bool    cJSON_IsString(const cJSON * item);
void    cJSON_Delete(cJSON * item);

#endif /* CJSON_H */
