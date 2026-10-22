#include "graph/document/serialize_internal.h"
#include "graph/model/registry.h"
#include "graph/model/value_copy.h"
#include "types/value_document.h"

#include <string.h>

const char *serialize_pin_type_name(pin_type type)
{
    const char *name = pin_type_name(type);
    return name ? name : "flow";
}

const char *serialize_implied_type_name(pin_type type, const char *template_name)
{
    bool primitive = (type >= PIN_BOOL && type <= PIN_F64) ||
                     type == PIN_NAME || type == PIN_STR;
    return primitive ? pin_type_name(type) : template_name;
}

pin_value serialize_zero_value(pin_type type, pin_container container, const char *type_name)
{
    pin_value value = { .type = type, .container = container, .type_name = type_name };
    if (container == PIN_CONTAINER_SINGLE && type == PIN_NAME) {
        value.str = "";
    }
    return value;
}

bool serialize_read_pin_type(av_value *value, pin_type *out)
{
    for (int type = PIN_FLOW; av_is_str(value) && type <= PIN_ENUM; type++) {
        if (!strcmp(av_get_str(value), serialize_pin_type_name((pin_type)type))) {
            *out = (pin_type)type;
            return true;
        }
    }
    return false;
}

static int read_document(graph *g, pin_value declaration, const av_value *document, pin_value *out)
{
    arena storage = { 0 };
    pin_value value;
    char error[192] = "";
    int result = pin_value_read_document(g ? &storage : NULL, declaration, document, &value, error, sizeof error);
    if (!result) {
        pin_value retained = value;
        result = g ? pin_value_copy_to_graph(g, &retained, &value) : 0;
        if (!result) {
            *out = retained;
        }
    }
    arena_free(&storage);
    if (result) {
        serialize_error_set("%s", error[0] ? error : "could not retain document value");
    }
    return result;
}

int serialize_read_array(pin_type type, const char *name, av_value *document, pin_value *out, graph *g)
{
    return read_document(
        g,
        (pin_value){ .type = type, .type_name = name, .container = PIN_CONTAINER_ARRAY },
        document,
        out
    );
}
int serialize_read_value(pin_type type, const char *name, av_value *document, pin_value *out, graph *g)
{
    return read_document(g, (pin_value){ .type = type, .type_name = name }, document, out);
}
