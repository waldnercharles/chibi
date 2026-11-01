#ifndef TEST_REFLECTED_TYPES_H
#define TEST_REFLECTED_TYPES_H
#include "types/catalog.h"

typedef struct
{
    float x, y;
} whole_pair;
typedef struct
{
    uint8_t tag;
    whole_pair position;
    double weight;
    bool active;
    int64_t stamp;
} whole_record;
typedef struct
{
    const char *text;
    float weight;
} whole_managed_inner;
typedef struct
{
    const char *label;
    whole_managed_inner nested;
    int count;
} whole_managed;
typedef struct
{
    float x, y;
} alias_vector;
extern const type_desc WHOLE_PAIR_TYPE, WHOLE_RECORD_TYPE;
extern const type_desc WHOLE_MANAGED_INNER_TYPE, WHOLE_MANAGED_TYPE;
extern const type_desc WHOLE_ALIAS_SOURCE_TYPE, WHOLE_ALIAS_TARGET_TYPE,
    WHOLE_SEARCH_TYPE;
extern const type_catalog_pack TEST_REFLECTED_TYPES_PACK;
#endif
