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
