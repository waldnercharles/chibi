#include "registry.h"

#include <assert.h>

/* Descriptor invariants, enforced once at registry init instead of on every
 * activation. Runtime state (arena allocation, a missing struct input) stays
 * in eval.c where it belongs. */
static int node_desc_validate(const node_desc *d)
{
    if (!d->name) return 0;
    if (d->input_count < 0 || d->output_count < 0) return 0;
    if ((d->input_count == 0) != (d->inputs == NULL)) return 0;
    if ((d->output_count == 0) != (d->outputs == NULL)) return 0;
    switch (d->pure_kind) {
        case NODE_PURE_CONST: break;
        case NODE_PURE_MAKE:
            if (!d->type || d->input_count != d->type->field_count ||
                d->output_count != 1 || d->outputs[0].type != PIN_STRUCT)
                return 0;
            break;
        case NODE_PURE_BREAK:
            if (!d->type || d->input_count != 1 || d->inputs[0].type != PIN_STRUCT ||
                d->output_count != d->type->field_count)
                return 0;
            break;
        case NODE_PURE_CALL:
            if (!d->fn || d->output_count != 1) return 0;
            break;
        case NODE_PURE_GET:
        case NODE_PURE_SELF:
            if (d->input_count != 0 || d->output_count != 1 || !d->outputs ||
                d->outputs[0].kind != PIN_DATA)
                return 0;
            break;
        default: return 0;
    }
    return 1;
}

void registry_validate(void)
{
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *d = registry_node(i);
        assert(d && node_desc_validate(d));
    }
}
