/* Standalone descriptor regression. No runtime library or native kernel runs. */
#include "capabilities/definition.h"
#include "graph/model/native_definition.h"
#include "runtime/text_definition.h"

#include <string.h>

#ifdef NATIVE_DEFAULT_WRONG_TYPE
/* The same fields must not make an unrelated type a valid pin_value. */
typedef struct { pin_type type; int i; } default_value_type;
#else
typedef pin_value default_value_type;
#endif

static const float VECTOR[] = { -1, 0, 1 };

#define ROWS(X)                                                                                   \
    X("Zero", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0,                  \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ 0 }))                                           \
    X("Boolean", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, 0, 0,              \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ .type = PIN_BOOL, .b = true }))                  \
    X("Integer", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE, 0, 0,               \
      SCRIPT_REFERENCE_NONE, false, ((default_value_type){ .type = PIN_I32, .i = 7 }))             \
    X("Float", NATIVE_PARAMETER_DATA, PIN_F32, "number", PIN_CONTAINER_SINGLE, 0, 0,              \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ .type = PIN_F32, .f = 1 }))                      \
    X("Double", NATIVE_PARAMETER_DATA, PIN_F64, "f64", PIN_CONTAINER_SINGLE, 0, 0,                \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ .type = PIN_F64, .d = 1 }))                      \
    X("String", NATIVE_PARAMETER_DATA, PIN_STR, "const char *", PIN_CONTAINER_SINGLE, 0, 0,       \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ .type = PIN_STR, .str = "default" }))            \
    X("Name", NATIVE_PARAMETER_DATA, PIN_NAME, "name", PIN_CONTAINER_SINGLE, 0, 0,                 \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ .type = PIN_NAME, .str = "symbol" }))             \
    X("Array", NATIVE_PARAMETER_DATA, PIN_U8, "u8", PIN_CONTAINER_ARRAY, SCRIPT_TYPE_UNTYPED, 1,   \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ .type = PIN_U8, .container = PIN_CONTAINER_ARRAY })) \
    X("Names", NATIVE_PARAMETER_DATA, PIN_NAME, "name", PIN_CONTAINER_ARRAY, 0, 0,                 \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ .type = PIN_NAME, .container = PIN_CONTAINER_ARRAY })) \
    X("Vector", NATIVE_PARAMETER_DATA, PIN_STRUCT, "vec3", PIN_CONTAINER_SINGLE, 0, 0,             \
      SCRIPT_REFERENCE_NONE, false,                                                              \
      ((pin_value){ .type = PIN_STRUCT, .type_name = "vec3", .ptr = (void *)VECTOR }))              \
    X("Owner", NATIVE_PARAMETER_DATA, PIN_HANDLE, "ecs.entity", PIN_CONTAINER_SINGLE, 0, 0,       \
      SCRIPT_REFERENCE_NONE, true, ((pin_value){ .type = PIN_HANDLE, .type_name = "ecs.entity" })) \
    X("Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL, PIN_CONTAINER_SINGLE, 0, 0,                   \
      SCRIPT_REFERENCE_NONE, false, ((pin_value){ 0 }))

static const native_parameter_definition PORTS[] = { ROWS(NATIVE_PORT) };
static const native_parameter_definition CSTRINGS[] = { ROWS(NATIVE_CSTRING_PORT) };

/* Two forwarding steps must retain commas inside VALUE's parentheses. */
#define FLAGGED(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    NATIVE_PORT_FLAGS(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE, 123)
static const native_parameter_definition FLAGS[] = { ROWS(FLAGGED) };
#undef FLAGGED

static const native_parameter_definition TEXT[] = {
    NATIVE_TEXT_BYTES_OUTPUTS(NATIVE_PORT) NATIVE_FORMAT_TEXT_INPUTS(NATIVE_PORT)
};

#ifdef NATIVE_DEFAULT_WRONG_TYPE
/* Rejection must come from the static consumers, not the expression reference. */
int main(void) { return 0; }
#else
/* An automatic reference retains the original expression-consuming contract. */
#define EXPRESSION(NAME, KIND, STORAGE, TYPE_NAME, CONTAINER, FLAGS, GROUP, REFERENCE, OWNER, VALUE) \
    { .name = NAME, .kind = KIND, .storage = STORAGE, .type_name = TYPE_NAME,                       \
      .container = CONTAINER, .type_flags = FLAGS, .generic_group = GROUP,                         \
      .reference_kind = REFERENCE, .has_default = true, .default_owner = OWNER,                    \
      .default_value = VALUE },

static bool text_equal(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

int main(void)
{
    int evaluations = 0;
    const native_parameter_definition once[] = {
        NATIVE_PORT("Once", NATIVE_PARAMETER_DATA, PIN_I32, "int", PIN_CONTAINER_SINGLE,
                    0, 0, SCRIPT_REFERENCE_NONE, false,
                    ((pin_value){ .type = PIN_I32, .i = ++evaluations }))
    };
    if (evaluations != 1 || once[0].default_value.i != 1) return 3;
    /* The real archived expression consumer receives unchanged live text rows. */
    const pin_spec graph[] = {
        NATIVE_TEXT_BYTES_OUTPUTS(GRAPH_NATIVE_PORT)
        NATIVE_FORMAT_TEXT_INPUTS(GRAPH_NATIVE_PORT)
    };
    for (int i = 0; i < NATIVE_COUNT(graph); ++i) {
        if (!text_equal(graph[i].name, TEXT[i].name) || graph[i].type != TEXT[i].storage ||
            graph[i].container != TEXT[i].container ||
            graph[i].default_value.type != TEXT[i].default_value.type ||
            graph[i].default_value.container != TEXT[i].default_value.container ||
            graph[i].default_value.b != TEXT[i].default_value.b) return 4;
    }
    const native_parameter_definition expected[] = { ROWS(EXPRESSION) };
    const native_parameter_definition *tables[] = { PORTS, CSTRINGS, FLAGS };
    for (int table = 0; table < NATIVE_COUNT(tables); ++table) {
        for (int i = 0; i < NATIVE_COUNT(expected); ++i) {
            const native_parameter_definition *a = &tables[table][i], *b = &expected[i];
            bool c_string = table == 1 && (b->storage == PIN_STR || b->storage == PIN_NAME) &&
                            b->container == PIN_CONTAINER_SINGLE;
            if (!text_equal(a->name, b->name) || a->kind != b->kind || a->storage != b->storage ||
                a->container != b->container || !text_equal(a->type_name, b->type_name) ||
                a->unit != b->unit || a->type_flags != b->type_flags ||
                a->generic_group != b->generic_group || a->reference_kind != b->reference_kind ||
                a->member_name != b->member_name || a->c_string != c_string ||
                a->has_default != b->has_default || a->default_owner != b->default_owner ||
                a->default_value.type != b->default_value.type ||
                a->default_value.container != b->default_value.container ||
                !text_equal(a->default_value.type_name, b->default_value.type_name) ||
                a->default_value.record != b->default_value.record ||
                a->default_value.schema != b->default_value.schema) {
                return 1;
            }
            /* Compare only the authored union member, never padding or raw bytes. */
            const pin_value *v = &a->default_value, *w = &b->default_value;
            switch (i) {
            case 2: if (v->i != w->i) return 2; break;
            case 3: if (v->f != w->f) return 2; break;
            case 4: if (v->d != w->d) return 2; break;
            case 5: case 6: if (!text_equal(v->str, w->str)) return 2; break;
            case 9: if (v->ptr != VECTOR || v->ptr != w->ptr) return 2; break;
            default: if (v->b != w->b) return 2; break;
            }
        }
    }
    return 0;
}
#endif
