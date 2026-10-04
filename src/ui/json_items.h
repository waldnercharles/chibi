#ifndef CHIBI_UI_JSON_ITEMS_H
#define CHIBI_UI_JSON_ITEMS_H

#include <stddef.h>
#include <yyjson.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Array items by index in constant time. yyjson links a mutable array's
     * items in a list, so its own lookup walks to the index; a data model's
     * views look up every item of a list they show, which made a dirty list
     * of n rows cost about n²/2 steps. The first lookup past the first few
     * items lists the array's items, and later lookups index that list while
     * the array keeps its size and last item. An array changed in place
     * otherwise, such as an item replaced, needs json_items_clear. */
    typedef struct json_items json_items;

    json_items *json_items_new();
    void json_items_free(json_items *items);
    /* `array`'s item at `index`, or NULL past its end. */
    yyjson_mut_val *json_items_get(json_items *items, yyjson_mut_val *array, size_t index);
    /* Forgets every list, as after arrays changed. */
    void json_items_clear(json_items *items);

#ifdef __cplusplus
}
#endif

#endif
