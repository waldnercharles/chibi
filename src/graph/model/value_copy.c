#include "graph/model/value_copy.h"

#include "graph/model/graph.h"
#include "support/arena.h"
#include "support/xalloc.h"

#include <assert.h>

static arena *storage(graph *g)
{
    if (!g->value_storage) {
        g->value_storage = xcalloc(1, sizeof *g->value_storage);
    }
    return g->value_storage;
}
pin_array *pin_array_clone_to_graph(graph *g, const pin_array *src)
{
    if (!g || !src) {
        return NULL;
    }
    pin_value source = { .type = src->element_type,
                         .type_name = src->type_name,
                         .record = src->record,
                         .container = PIN_CONTAINER_ARRAY,
                         .ptr = (void *)src },
              copy;
    return pin_value_capture_to_arena(storage(g), &copy, &source) ? NULL : copy.ptr;
}
int pin_value_copy_to_graph(graph *g, pin_value *dst, const pin_value *src)
{
    if (!g || !dst || !src) {
        return -1;
    }
    return pin_value_capture_to_arena(storage(g), dst, src);
}

pin_value graph_text_value(graph *g, const char *bytes, size_t length)
{
    pin_value value;
    const char *error = pin_value_text(storage(g), &value, bytes, length);
    assert(!error);
    (void)error;
    return value;
}
const char *graph_text_cstring(const graph *g, pin_value value)
{
    const char *text;
    return pin_text_cstring(storage((graph *)g), value, &text, NULL) ? NULL : text;
}
