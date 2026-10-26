#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Unobserved, nonescaping unions are tags and immutable payloads. Forward
 * control flow meets identities across aliases; nested candidates share escape
 * dependencies. Canonical inspection and real suspension ownership stay intact. */
typedef struct
{
    int object, owner, slot;
    bool known;
    pin_value value;
} scalar_value;
typedef struct
{
    uint64_t tag;
    scalar_value *payload;
    int region, group;
    bool escapes;
} scalar_union;

#define FLOW_SCRATCH_LIMIT (4 * 1024 * 1024)

size_t behavior_function_scalarize_scratch(const behavior_function *f)
{
    size_t edges = 0, count = acount(f->instructions);
    for (int pc = 0; pc < (int)count; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_JUMP || i->op == BF_BRANCH) {
            if (i->target <= pc) {
                return 0;
            }
            edges++;
        }
    }
    size_t width = 0, limit = FLOW_SCRATCH_LIMIT / sizeof(scalar_value);
    const int sizes[] = { acount(f->slots), acount(f->locals), acount(f->data_values) };
    for (int j = 0; j < 3; j++) {
        if ((size_t)sizes[j] > limit - width) {
            return 0;
        }
        width += sizes[j];
    }
    if (count > FLOW_SCRATCH_LIMIT / sizeof(scalar_value *)) {
        return 0;
    }
    size_t row = width * sizeof(scalar_value),
           pointers = count * sizeof(scalar_value *);
    if (row > FLOW_SCRATCH_LIMIT || pointers > FLOW_SCRATCH_LIMIT ||
        (row && edges > (FLOW_SCRATCH_LIMIT - pointers) / row)) {
        return 0;
    }
    return pointers + edges * row;
}

static bool plain(pin_value value)
{
    return value.container == PIN_CONTAINER_SINGLE &&
           ((value.type >= PIN_BOOL && value.type <= PIN_F64) ||
            value.type == PIN_HANDLE || value.type == PIN_ENUM);
}

/* These callbacks receive scalar banks, never the execution context or a
 * reference to a virtual object. They still execute, including side effects
 * and errors. Purity by itself would not prove this isolation. */
static bool isolated_call(const behavior_function *f, const behavior_function_instruction *i)
{
    if (i->op != BF_HOST_CALL) {
        return false;
    }
    const behavior_function_host_call *h = &f->host_calls[i->target];
    if (h->call->context || h->call->action || (!h->call->invoke && !h->call->value)) {
        return false;
    }
    for (int j = h->first; j < h->result_first + h->result_count; j++) {
        if (!plain(f->data_values[j].initial) ||
            f->data_values[j].kind != BEHAVIOR_VALUE) {
            return false;
        }
    }
    return true;
}

static int intrinsic(const behavior_function *f, const behavior_function_instruction *i)
{
    if (i->op != BF_HOST_CALL || i->left != BEHAVIOR_FUNCTION_HOST_DATA) {
        return 0;
    }
    const behavior_function_host_call *h = &f->host_calls[i->target];
    uint64_t id = h->capability_id;
    const node_host_call *canonical = execution_intrinsic_resolve(id),
                         *actual = h->call;
    if (!canonical || h->capability_abi != execution_intrinsic_abi(id) ||
        actual->service || actual->missing_service ||
        actual->invoke != canonical->invoke || actual->value != canonical->value ||
        actual->context != canonical->context || actual->action != canonical->action ||
        actual->optional_service != canonical->optional_service ||
        actual->variadic != canonical->variadic ||
        actual->reusable != canonical->reusable ||
        actual->language_values != canonical->language_values ||
        actual->owned_result != canonical->owned_result) {
        return 0;
    }
    if (canonical == &EXECUTION_union_make_call ||
        canonical == &EXECUTION_union_test_call ||
        canonical == &EXECUTION_union_get_call || canonical == &EXECUTION_snapshot_call ||
        canonical == &EXECUTION_value_copy_call ||
        canonical == &EXECUTION_value_drop_call) {
        return (int)(id & UINT64_C(0xffffffff));
    }
    return 0;
}

static bool boundary(const behavior_function *f, const behavior_function_instruction *i)
{
    return i->op == BF_JUMP || i->op == BF_BRANCH || i->op == BF_RETURN ||
           i->op == BF_END || i->op == BF_FAIL || i->op == BF_SUSPEND ||
           i->op == BF_DELAY || i->op == BF_INVOKE || i->op == BF_COMMAND ||
           i->op == BF_COMMAND_BEGIN || i->op == BF_QUERY ||
           i->op == BF_COMPONENT_WRITE || i->op == BF_ENTER || i->op == BF_LEAVE ||
           (i->op == BF_HOST_CALL && !intrinsic(f, i) && !isolated_call(f, i));
}

static void read_region(int *regions, int index, int region, bool forward)
{
    regions[index] = forward || regions[index] == -1 || regions[index] == region
                       ? region
                       : -2;
}

static bool same_type(pin_value a, pin_value b)
{
    return a.type == b.type && a.container == b.container &&
           !strcmp(a.type_name ? a.type_name : "", b.type_name ? b.type_name : "");
}

static scalar_value scalar(pin_value value)
{
    return (scalar_value){ .known = true, .slot = -1, .value = value };
}

static bool constant_value(scalar_value value, pin_type type)
{
    return value.known && !value.object && value.slot == -1 && value.value.type == type;
}

static void escape(scalar_union *objects, int object)
{
    if (object) {
        objects[object - 1].escapes = true;
    }
}

static int group(scalar_union *objects, int object)
{
    int root = object;
    while (objects[root - 1].group != root) {
        root = objects[root - 1].group;
    }
    while (object != root) {
        int parent = objects[object - 1].group;
        objects[object - 1].group = root;
        object = parent;
    }
    return root;
}

static bool same_value(scalar_value a, scalar_value b)
{
    return a.known == b.known &&
           (!a.known ||
            (a.object == b.object && a.owner == b.owner && a.slot == b.slot &&
             same_type(a.value, b.value) && a.value.u64 == b.value.u64));
}

typedef struct
{
    int width, mutable;
    const int *reads;
} scalar_flow;

static void meet(
    scalar_union *objects,
    scalar_value *to,
    const scalar_value *from,
    const scalar_flow *flow,
    int pc
)
{
    for (int j = 0; j < flow->width; j++) {
        if (!same_value(to[j], from[j])) {
            /* Dead path-local banks need no materialization. Unique slots
             * additionally have verifier-proven dominating definitions, so a
             * slot absent on one predecessor cannot be read after this join. */
            if (j < flow->mutable && flow->reads[j] >= pc) {
                escape(objects, to[j].object);
                escape(objects, to[j].owner);
                escape(objects, from[j].object);
                escape(objects, from[j].owner);
            }
            to[j] = (scalar_value){ 0 };
        }
    }
}

static void forget(scalar_union *objects, scalar_value *state, int count)
{
    for (int j = 0; j < count; j++) {
        escape(objects, state[j].object);
        escape(objects, state[j].owner);
        state[j] = (scalar_value){ 0 };
    }
}

static scalar_value union_call(
    const behavior_function *f,
    const behavior_function_host_call *h,
    int kind,
    scalar_value *banks,
    scalar_union **objects,
    int region,
    int *owner
)
{
    int count = h->result_first - h->first;
    if (h->result_count != 1 || count < 1) {
        return (scalar_value){ 0 };
    }
    pin_value output = f->data_values[h->result_first].initial;
    scalar_value *args = banks + h->first;
    if (kind == EXECUTION_INTRINSIC_UNION_MAKE) {
        if (!constant_value(args[0], PIN_U64) || !args[0].value.u64 ||
            output.type != PIN_UNION || output.container != PIN_CONTAINER_SINGLE) {
            return (scalar_value){ 0 };
        }
        scalar_union object = { .tag = args[0].value.u64, .region = region };
        for (int j = 1; j < count; j++) {
            pin_value header = f->data_values[h->first + j].initial;
            bool nested = args[j].object && header.type == PIN_UNION &&
                          header.container == PIN_CONTAINER_SINGLE;
            if (!args[j].known || (!nested && (args[j].object || !plain(header)))) {
                afree(object.payload);
                return (scalar_value){ 0 };
            }
            scalar_value payload = args[j];
            payload.value = header;
            if (args[j].slot == -1) {
                payload.value.u64 = args[j].value.u64;
            }
            apush(object.payload, payload);
        }
        apush(*objects, object);
        *owner = acount(*objects);
        (*objects)[*owner - 1].group = *owner;
        for (int j = 0; j < acount(object.payload); j++) {
            int nested = object.payload[j].object;
            if (nested) {
                (*objects)[group(*objects, nested) - 1].group = *owner;
            }
        }
        return (scalar_value){ .known = true, .object = *owner, .owner = *owner };
    }
    int object = args[0].object;
    if (!object || (*objects)[object - 1].region != region) {
        return (scalar_value){ 0 };
    }
    scalar_union *value = &(*objects)[object - 1];
    /* A scalarized union has no storage, so copies alias it and drops are
     * no-ops. */
    if ((kind == EXECUTION_INTRINSIC_SNAPSHOT || kind == EXECUTION_INTRINSIC_VALUE_COPY) &&
        count == 1 && output.type == PIN_UNION &&
        output.container == PIN_CONTAINER_SINGLE) {
        *owner = object;
        return args[0];
    }
    if (kind == EXECUTION_INTRINSIC_VALUE_DROP && count == 1 &&
        output.type == PIN_BOOL && output.container == PIN_CONTAINER_SINGLE) {
        *owner = object;
        scalar_value result = scalar((pin_value){ .type = PIN_BOOL, .b = true });
        result.owner = object;
        return result;
    }
    if (kind == EXECUTION_INTRINSIC_UNION_TEST && count >= 2 &&
        output.type == PIN_BOOL && output.container == PIN_CONTAINER_SINGLE) {
        bool match = false;
        for (int j = 1; j < count; j++) {
            if (!constant_value(args[j], PIN_U64)) {
                return (scalar_value){ 0 };
            }
            match |= args[j].value.u64 == value->tag;
        }
        *owner = object;
        scalar_value result = scalar((pin_value){ .type = PIN_BOOL, .b = match });
        result.owner = object;
        return result;
    }
    if (kind != EXECUTION_INTRINSIC_UNION_GET || count < 4 ||
        !constant_value(args[1], PIN_U64) || !constant_value(args[2], PIN_I32) ||
        !constant_value(args[3], PIN_BOOL)) {
        return (scalar_value){ 0 };
    }
    bool match = !args[1].value.u64 || args[1].value.u64 == value->tag;
    for (int j = 4; j < count; j++) {
        if (!constant_value(args[j], PIN_U64)) {
            return (scalar_value){ 0 };
        }
        match |= args[j].value.u64 == value->tag;
    }
    scalar_value result;
    if (!match) {
        output.u64 = 0;
        result = scalar(output);
    } else if (args[3].value.b) {
        if (output.type != PIN_UNION || output.container != PIN_CONTAINER_SINGLE) {
            return (scalar_value){ 0 };
        }
        result = args[0];
    } else {
        int field = args[2].value.i;
        if (field < 0 || field >= acount(value->payload) ||
            !same_type(value->payload[field].value, output)) {
            return (scalar_value){ 0 };
        }
        result = value->payload[field];
    }
    *owner = object;
    result.owner = object;
    return result;
}

void behavior_function_scalarize_unions(behavior_function *f, bool *keep)
{
    int count = acount(f->instructions), values = acount(f->slots);
    int bank_count = acount(f->data_values), local_count = acount(f->locals);
    if (bank_count > INT_MAX - local_count ||
        values >= INT_MAX - bank_count - local_count) {
        return;
    }
    if (!acount(f->host_calls)) {
        return;
    }
    behavior_function_instruction *code = f->runtime_body;
    bool constructs = false;
    for (int pc = 0; pc < count; pc++) {
        constructs |= keep[pc] &&
                      intrinsic(f, &code[pc]) == EXECUTION_INTRINSIC_UNION_MAKE;
    }
    if (!constructs) {
        return;
    }
    /* A scalar slot must identify one definition. The general VM also accepts
     * manually built code that overwrites a slot, which needs a reaching-defs
     * analysis before scalar replacement is safe. */
    bool *defined = xcalloc((size_t)(values + 1), sizeof *defined);
    bool unique = true;
    for (int pc = 0; pc < count; pc++) {
        if (keep[pc] && code[pc].op < BF_STORE) {
            unique &= !defined[code[pc].output];
            defined[code[pc].output] = true;
            if ((code[pc].op == BF_DIV || code[pc].op == BF_MOD) &&
                behavior_scalar_integer(code[pc].type) && code[pc].target >= 0) {
                unique &= !defined[code[pc].target];
                defined[code[pc].target] = true;
            }
        }
    }
    free(defined);
    if (!unique) {
        return;
    }
    bool forward = behavior_function_scalarize_scratch(f) != 0;
    int *regions = xcalloc((size_t)count, sizeof *regions);
    bool *starts = xcalloc((size_t)count, sizeof *starts);
    int *addresses = xmalloc((size_t)(values + 1) * sizeof *addresses);
    int *bank_reads = xmalloc((size_t)(bank_count + local_count + 1) * sizeof *bank_reads);
    int *local_reads = bank_reads + bank_count;
    bool *written = xcalloc((size_t)(bank_count + 1), sizeof *written);
    bool *unsafe = xcalloc((size_t)(bank_count + 1), sizeof *unsafe);
    bool *pinned = xcalloc((size_t)(values + local_count + 1), sizeof *pinned);
    for (int j = 0; j < values; j++) {
        addresses[j] = -1;
    }
    for (int j = 0; j < bank_count; j++) {
        bank_reads[j] = -1;
    }
    for (int j = 0; j < local_count; j++) {
        local_reads[j] = -1;
    }
    for (int pc = 0; pc < count; pc++) {
        if (code[pc].op == BF_JUMP || code[pc].op == BF_BRANCH) {
            starts[code[pc].target] = true;
        }
        if (keep[pc] && boundary(f, &code[pc]) && pc + 1 < count) {
            starts[pc + 1] = true;
        }
        if (code[pc].op == BF_HOST_SLOT && !code[pc].target) {
            addresses[code[pc].output] = code[pc].left;
        }
    }
    for (int j = 0; j < acount(f->suspensions); j++) {
        const behavior_function_suspension *s = &f->suspensions[j];
        if (code[s->pc].op == BF_POINT || code[s->pc].op == BF_SOURCE_POINT) {
            continue;
        }
        for (int k = 0; k < s->value_count; k++) {
            pinned[s->values[k]] = true;
        }
    }
    int region = 0;
    for (int pc = 0; pc < count; pc++) {
        regions[pc] = forward ? 0 : (region += starts[pc]);
        const behavior_function_instruction *i = &code[pc];
        if (!keep[pc]) {
            continue;
        }
        if ((i->op == BF_LOAD || i->op == BF_READ) && addresses[i->left] >= 0) {
            read_region(bank_reads, addresses[i->left], forward ? pc : region, forward);
        }
        if (i->op == BF_WRITE && addresses[i->right] >= 0) {
            written[addresses[i->right]] = true;
        }
        int left_address = i->left_slot ? addresses[i->left] : -1;
        int right_address = i->right_slot ? addresses[i->right] : -1;
        if (left_address >= 0 &&
            !((i->op == BF_LOAD || i->op == BF_READ) &&
              i->offset == offsetof(pin_value, d) &&
              i->type == f->data_values[left_address].initial.type)) {
            unsafe[left_address] = true;
        }
        if (right_address >= 0 &&
            !(i->op == BF_WRITE && i->offset == offsetof(pin_value, d) &&
              i->type == f->data_values[right_address].initial.type &&
              f->slot_kinds[i->left] == BEHAVIOR_VALUE)) {
            unsafe[right_address] = true;
        }
        if (behavior_function_third_slot(i->op) && addresses[i->target] >= 0) {
            unsafe[addresses[i->target]] = true;
        }
        if (i->op == BF_STORE_VALUE) {
            unsafe[i->left] = true;
        }
        if (i->op == BF_INVOKE) {
            const behavior_function_invocation *call = &f->calls[i->left];
            for (int j = 0; j < call->count; j++) {
                unsafe[call->first + j] = true;
            }
            for (int j = 0; j < call->result_count; j++) {
                unsafe[call->result_first + j] = true;
            }
        }
        if (i->op == BF_LOCAL) {
            read_region(local_reads, i->left, forward ? pc : region, forward);
        }
        if (i->op == BF_HOST_CALL) {
            const behavior_function_host_call *h = &f->host_calls[i->target];
            for (int j = h->first; j < h->result_first; j++) {
                read_region(bank_reads, j, forward ? pc : region, forward);
            }
            for (int j = 0; j < h->result_count; j++) {
                written[h->result_first + j] = true;
            }
        }
    }
    int width = bank_count + local_count + values;
    const scalar_flow flow = { .width = width,
                               .mutable = bank_count + local_count,
                               .reads = bank_reads };
    scalar_value *state = xcalloc((size_t)(width + 1), sizeof *state);
    scalar_value *banks = state, *locals = banks + bank_count,
                 *slots = locals + local_count;
    scalar_value **incoming = forward ? xcalloc((size_t)count, sizeof *incoming) : NULL;
    scalar_value *replacement = xcalloc((size_t)count, sizeof *replacement);
    int *owners = xcalloc((size_t)count, sizeof *owners);
    scalar_union *objects = NULL;
    int previous_region = -1;
    bool reached = true;
    for (int pc = 0; pc < count; pc++) {
        if (forward && incoming[pc]) {
            if (reached) {
                meet(objects, state, incoming[pc], &flow, pc);
            } else {
                memcpy(state, incoming[pc], (size_t)width * sizeof *state);
            }
            free(incoming[pc]);
            incoming[pc] = NULL;
            reached = true;
        }
        if (!reached) {
            continue;
        }
        if (!keep[pc]) {
            continue;
        }
        behavior_function_instruction *i = &code[pc];
        region = regions[pc];
        if (region != previous_region) {
            memset(locals, 0, (size_t)local_count * sizeof *locals);
            for (int j = 0; j < bank_count; j++) {
                banks[j] = written[j] ? (scalar_value){ 0 }
                                      : scalar(f->data_values[j].initial);
            }
            previous_region = region;
        }
        int left = i->left_slot ? slots[i->left].object : 0;
        int right = i->right_slot ? slots[i->right].object : 0;
        bool consumed = false;
        if (i->op == BF_CONSTANT) {
            slots[i->output] = scalar(i->value);
        } else if (i->op == BF_WRITE && addresses[i->right] >= 0 && i->offset == offsetof(pin_value, d)) {
            int bank = addresses[i->right];
            scalar_value value = slots[i->left];
            if (!value.known) {
                value = (scalar_value){ .known = true,
                                        .slot = i->left,
                                        .value = f->data_values[bank].initial };
            }
            banks[bank] = value;
            if (left) {
                owners[pc] = left;
                consumed = true;
                if (unsafe[bank] || (!forward && bank_reads[bank] != region &&
                                     bank_reads[bank] != -1)) {
                    escape(objects, left);
                }
            }
        } else if (i->op == BF_SAVE) {
            locals[i->target] = slots[i->left];
            if (left) {
                owners[pc] = left;
                consumed = true;
                if (pinned[values + i->target] ||
                    (!forward && local_reads[i->target] != region &&
                     local_reads[i->target] != -1)) {
                    escape(objects, left);
                }
            }
        } else if (i->op == BF_LOCAL) {
            slots[i->output] = locals[i->left];
            owners[pc] = slots[i->output].object;
        } else if ((i->op == BF_LOAD || i->op == BF_READ) && addresses[i->left] >= 0 && i->offset == offsetof(pin_value, d)) {
            scalar_value value = banks[addresses[i->left]];
            if (value.object) {
                slots[i->output] = value;
                owners[pc] = value.object;
            } else if (value.owner) {
                replacement[pc] = value;
            }
        } else if (i->op == BF_HOST_CALL) {
            const behavior_function_host_call *h = &f->host_calls[i->target];
            bool safe = true;
            for (int j = h->first; j < h->result_first + h->result_count; j++) {
                safe &= !unsafe[j];
            }
            scalar_value value = safe
                                   ? union_call(f, h, intrinsic(f, i), banks, &objects, region, &owners[pc])
                                   : (scalar_value){ 0 };
            if (value.known) {
                banks[h->result_first] = value;
            } else {
                for (int j = h->first; j < h->result_first; j++) {
                    escape(objects, banks[j].object);
                }
                for (int j = 0; j < h->result_count; j++) {
                    banks[h->result_first + j] = (scalar_value){ 0 };
                }
            }
        }
        if (i->op < BF_STORE && pinned[i->output]) {
            escape(objects, slots[i->output].object);
            escape(objects, replacement[pc].owner);
        }
        if (left && (!consumed || objects[left - 1].region != region)) {
            escape(objects, left);
        }
        escape(objects, right);
        if (behavior_function_third_slot(i->op)) {
            escape(objects, slots[i->target].object);
        }
        if (forward) {
            if (i->op == BF_BRANCH || i->op == BF_JUMP) {
                if (!incoming[i->target]) {
                    incoming[i->target] = xmalloc((size_t)width * sizeof *state);
                    memcpy(incoming[i->target], state, (size_t)width * sizeof *state);
                } else {
                    meet(objects, incoming[i->target], state, &flow, i->target);
                }
            }
            if (i->op == BF_JUMP || i->op == BF_RETURN || i->op == BF_END ||
                i->op == BF_FAIL) {
                reached = false;
            } else if (boundary(f, i) && i->op != BF_BRANCH) {
                forget(objects, state, width);
                for (int j = 0; j < bank_count; j++) {
                    banks[j] = written[j] ? (scalar_value){ 0 }
                                          : scalar(f->data_values[j].initial);
                }
            }
        }
    }
    /* A materialized parent needs its nested payloads, and an escaping nested
     * alias must keep every construction/snapshot sharing that candidate. */
    for (int j = 0; j < acount(objects); j++) {
        if (objects[j].escapes) {
            objects[group(objects, j + 1) - 1].escapes = true;
        }
    }
    for (int j = 0; j < acount(objects); j++) {
        objects[j].escapes = objects[group(objects, j + 1) - 1].escapes;
    }
    int *aliases = xmalloc((size_t)(values + 1) * sizeof *aliases);
    for (int j = 0; j < values; j++) {
        aliases[j] = j;
    }
    for (int pc = 0; pc < count; pc++) {
        int owner = owners[pc];
        if (owner && !objects[owner - 1].escapes) {
            keep[pc] = false;
        }
        scalar_value value = replacement[pc];
        if (value.owner && !objects[value.owner - 1].escapes) {
            if (value.slot >= 0) {
                aliases[code[pc].output] = aliases[value.slot];
                keep[pc] = false;
            } else {
                code[pc].op = BF_CONSTANT;
                code[pc].value = value.value;
                code[pc].left_slot = code[pc].right_slot = false;
            }
        }
        behavior_function_instruction *i = &code[pc];
        if (i->left_slot) {
            while (aliases[i->left] != i->left) {
                i->left = aliases[i->left];
            }
        }
        if (i->right_slot) {
            while (aliases[i->right] != i->right) {
                i->right = aliases[i->right];
            }
        }
        if (behavior_function_third_slot(i->op)) {
            while (aliases[i->target] != i->target) {
                i->target = aliases[i->target];
            }
        }
    }
    for (int j = 0; j < acount(objects); j++) {
        afree(objects[j].payload);
    }
    afree(objects);
    free(aliases);
    free(owners);
    free(replacement);
    free(incoming);
    free(state);
    free(written);
    free(unsafe);
    free(pinned);
    free(bank_reads);
    free(addresses);
    free(starts);
    free(regions);
}
