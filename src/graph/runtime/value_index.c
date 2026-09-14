#include "graph/runtime/value_array.h"
#include "support/arena.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

enum
{
    INDEX_MIN_COUNT = 64
};

static bool supported(pin_type type)
{
    return (type >= PIN_BOOL && type <= PIN_HANDLE) || type == PIN_ENUM;
}
/* NaNs never match, so storing them in the index only creates collisions. */
static bool matches_itself(const pin_value *v)
{
    return v->type == PIN_F32 ? !isnan(v->f) : v->type != PIN_F64 || !isnan(v->d);
}
static unsigned hash(const pin_value *v)
{
    uint64_t bits = 0;
    switch (v->type) {
        case PIN_BOOL: bits = v->b; break;
        case PIN_I8: bits = v->i8; break;
        case PIN_U8: bits = v->u8; break;
        case PIN_I16: bits = v->i16; break;
        case PIN_U16: bits = v->u16; break;
        case PIN_I32: bits = v->i; break;
        case PIN_U32: bits = v->u; break;
        case PIN_I64: bits = v->i64; break;
        case PIN_U64:
        case PIN_HANDLE: bits = v->u64; break;
        case PIN_ENUM: bits = v->e; break;
        case PIN_F32:
            if (v->f != 0) {
                memcpy(&bits, &v->f, sizeof v->f);
            }
            break;
        case PIN_F64:
            if (v->d != 0) {
                memcpy(&bits, &v->d, sizeof v->d);
            }
            break;
        case PIN_STR:
        case PIN_NAME:
            bits = 2166136261u;
            for (const unsigned char *s = (const unsigned char *)(v->str ? v->str : "");
                 *s;
                 s++) {
                bits = (unsigned)bits * 16777619u ^ *s;
            }
            break;
        default: assert(false);
    }
    unsigned h = (unsigned)bits ^ (unsigned)(bits >> 32);
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    return h ^ (h >> 16);
}
static int *buckets(arena *storage, const pin_value *items, int count, unsigned *mask)
{
    if (count < INDEX_MIN_COUNT || count > INT_MAX / 2) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        if (items[i].container != PIN_CONTAINER_SINGLE || !supported(items[i].type)) {
            return NULL;
        }
    }
    unsigned capacity = 1;
    while (capacity < (unsigned)count * 2) {
        capacity *= 2;
    }
    int *table = arena_alloc(storage, (size_t)capacity * sizeof *table);
    memset(table, 0, (size_t)capacity * sizeof *table);
    *mask = capacity - 1;
    return table;
}
static unsigned position(const pin_array *array, const pin_value *value)
{
    unsigned at = hash(value) & array->bucket_mask;
    while (array->buckets[at] &&
           !pin_value_equal(&array->items[array->buckets[at] - 1], value)) {
        at = (at + 1) & array->bucket_mask;
    }
    return at;
}
int pin_array_find(const pin_array *array, const pin_value *value)
{
    if (!array) {
        return -1;
    }
    if (array->buckets && value->container == PIN_CONTAINER_SINGLE &&
        supported(value->type)) {
        return matches_itself(value) ? array->buckets[position(array, value)] - 1
                                     : -1;
    }
    for (int i = 0; i < array->count; i++) {
        if (pin_value_equal(&array->items[i], value)) {
            return i;
        }
    }
    return -1;
}
void pin_array_index(arena *storage, pin_array *array)
{
    if (!array || array->buckets) {
        return;
    }
    int *table = buckets(storage, array->items, array->count, &array->bucket_mask);
    if (!table) {
        return;
    }
    array->buckets = table;
    for (int i = 0; i < array->count; i++) {
        if (!matches_itself(&array->items[i])) {
            continue;
        }
        unsigned at = position(array, &array->items[i]);
        if (!table[at]) {
            table[at] = i + 1;
        }
    }
}
int pin_set_unique(arena *storage, pin_value *out, const pin_value *items, int count)
{
    pin_array unique = { .items = out };
    int *table = buckets(storage, items, count, &unique.bucket_mask);
    unique.buckets = table;
    for (int i = 0; i < count; i++) {
        if (table && !matches_itself(&items[i])) {
            unique.items[unique.count++] = items[i];
            continue;
        }
        unsigned at = table ? position(&unique, &items[i]) : 0;
        bool found = table ? table[at] != 0 : pin_array_find(&unique, &items[i]) >= 0;
        if (found) {
            continue;
        }
        unique.items[unique.count++] = items[i];
        if (table) {
            table[at] = unique.count;
        }
    }
    return unique.count;
}
