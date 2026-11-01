#include "execution/module_effects.h"

#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EFFECT_DEPTH 64
#define EFFECT_RECORDS 65536
#define EFFECT_STEPS 200000
#define SCALAR_HELPERS 64

static uint64_t intrinsic(const behavior_function_host_call *host)
{
    const node_host_call *actual = host->call;
    const node_host_call *canonical = execution_intrinsic_resolve(host->capability_id);
    if (!canonical ||
        host->capability_abi != execution_intrinsic_abi(host->capability_id) ||
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
    int count = host->result_first - host->first;
    int expected = -1;
    switch (execution_intrinsic_kind(host->capability_id)) {
        case EXECUTION_INTRINSIC_CONSTRUCT:
        case EXECUTION_INTRINSIC_UNION_MAKE:
            if (count < 1 || host->result_count != 1) {
                return 0;
            }
            break;
        case EXECUTION_INTRINSIC_UNION_KEEP:
            if (count < 2 || host->result_count != 1) {
                return 0;
            }
            break;
        case EXECUTION_INTRINSIC_UNION_TEST:
        case EXECUTION_INTRINSIC_UNION_GET: {
            int minimum = execution_intrinsic_kind(host->capability_id) ==
                                  EXECUTION_INTRINSIC_UNION_TEST
                            ? 2
                            : 4;
            if (count < minimum || host->result_count != 1) {
                return 0;
            }
            break;
        }
        case EXECUTION_INTRINSIC_POINTER_MAKE: expected = 3; break;
        case EXECUTION_INTRINSIC_POINTER_READ:
        case EXECUTION_INTRINSIC_POINTER_WRITE: expected = 2; break;
        case EXECUTION_INTRINSIC_POINTER_FIELD: expected = 5; break;
        case EXECUTION_INTRINSIC_POINTER_REFERENCE: expected = 1; break;
        case EXECUTION_INTRINSIC_REFERENCE_MAKE: expected = 9; break;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
        case EXECUTION_INTRINSIC_REFERENCE_UNION: expected = 4; break;
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_LOCAL_RELEASE:
        case EXECUTION_INTRINSIC_STRING_CURSOR:
        case EXECUTION_INTRINSIC_STRING_BYTES:
        case EXECUTION_INTRINSIC_SNAPSHOT:
        case EXECUTION_INTRINSIC_VALUE_COPY:
        case EXECUTION_INTRINSIC_VALUE_DROP: expected = 1; break;
        case EXECUTION_INTRINSIC_STRING_ITEM:
        case EXECUTION_INTRINSIC_STRING_NEXT: expected = 2; break;
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
        case EXECUTION_INTRINSIC_MAP_DELETE_PLACE: expected = 2; break;
        case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
        case EXECUTION_INTRINSIC_SEQUENCE_VIEW: expected = 4; break;
        case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
        case EXECUTION_INTRINSIC_SEQUENCE_LENGTH: expected = 1; break;
        case EXECUTION_INTRINSIC_SEQUENCE_GET:
        case EXECUTION_INTRINSIC_SEQUENCE_BORROW: expected = 2; break;
        case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
            if (count < 2 || count > 4 || host->result_count != 1) {
                return 0;
            }
            break;
        default: break;
    }
    if (expected >= 0 && (count != expected || host->result_count != 1)) {
        return 0;
    }
    return host->capability_id;
}

bool behavior_function_hosts_pure(const behavior_function *root)
{
    /* Keep discovery order separate from the pointer index. Repeated edges
     * search logarithmically; inserting a new pointer charges every move. */
    const behavior_function **functions = NULL, **sorted = NULL;
    apush(functions, root);
    apush(sorted, root);
    int steps = EFFECT_STEPS;
    bool valid = true;
    for (int at = 0; valid && at < acount(functions); at++) {
        const behavior_function *f = functions[at];
        if (!f || !f->frozen || acount(functions) > EFFECT_RECORDS ||
            acount(f->queries) || acount(f->commands)) {
            valid = false;
            break;
        }
        for (int i = 0; valid && i < acount(f->host_calls); i++) {
            const behavior_function_host_call *host = &f->host_calls[i];
            if (--steps < 0) {
                valid = false;
                break;
            }
            if (intrinsic(host)) {
                continue;
            }
            const node_access *access = host->access;
            valid = access && (access->flags & NODE_EFFECT_PURE) &&
                    !(access->flags & ~(NODE_EFFECT_PURE | NODE_EFFECT_PARALLEL)) &&
                    !access->read_count && !access->write_count &&
                    !access->read_refs && !host->call->service &&
                    !host->call->missing_service && !host->call->action;
        }
        for (int i = 0; valid && i < acount(f->calls); i++) {
            const behavior_function_invocation *call = &f->calls[i];
            if (call->task) {
                valid = false;
                break;
            }
            bool seen = false;
            int low = 0, high = acount(sorted);
            while (low < high) {
                if (--steps < 0) {
                    valid = false;
                    break;
                }
                int middle = low + (high - low) / 2;
                if (sorted[middle] == call->function) {
                    seen = true;
                    break;
                }
                if ((uintptr_t)sorted[middle] < (uintptr_t)call->function) {
                    low = middle + 1;
                } else {
                    high = middle;
                }
            }
            if (valid && !seen) {
                int count = acount(sorted), moves = count - low;
                if (count == EFFECT_RECORDS || steps < moves) {
                    valid = false;
                    break;
                }
                steps -= moves;
                apush(functions, call->function);
                apush(sorted, call->function);
                memmove(sorted + low + 1, sorted + low, (size_t)moves * sizeof *sorted);
                sorted[low] = call->function;
            }
        }
    }
    afree(sorted);
    afree(functions);
    return valid;
}
/* Whether i writes the context's exit flag or outcome, which a call's
 * results do not share. */
static bool control_write(const behavior_function_instruction *i)
{
    return (i->type == PIN_BOOL &&
            i->offset == offsetof(behavior_function_context, exit_requested)) ||
           (i->type == PIN_U64 &&
            i->offset == offsetof(behavior_function_context, returned_outcome));
}
static bool share_local(const behavior_function *, bool built, bool *returned);

/* Argument sharing. A synchronous call may pass the caller's argument values
 * without copying them when the callee, and every function it can reach, leaves
 * storage it did not allocate untouched. Taint marks each pointer that may
 * reach such storage: arguments, state, context, results and anything loaded
 * through or derived from them. Nothing may write through a tainted address,
 * update a tainted local in place, or write through a reference other than a
 * value reference to an own, untainted cell. Writes through addresses the
 * analysis cannot place forget what every cell holds. Values copied by
 * snapshots, constructors, reference reads and authored returns can retain
 * mutable array roots even when their outer value storage is fresh. Plain
 * values and immutable text remain independent. The verifier rejects direct
 * writes to a cell's type and container, a return checks its results'
 * headers, a reference read checks its result's header and its root's type,
 * a reference write checks the written value's type, and `retyped` covers the
 * writes none of them can place. */
enum
{
    SHARE_NONE = -2,
    SHARE_UNKNOWN = -1,
    SHARE_RESULTS = -3,
    SHARE_CONTEXT = -4,
    SHARE_PASSES = 64
};
/* Text is immutable, so storing the caller's text in an own cell or record
 * leaves that storage the definition's own: `texts` marks such cells and
 * `text_memory` storage the analysis cannot place. Only reading those bytes
 * back as an address could reach the caller, so such a read is tainted.
 * `referenced` marks the cells a value reference may write. `headers` marks
 * slots that may hold a cell's header address. `retyped` says a write may have
 * changed a stored value's type or container, after which a copy may keep the
 * bits of the pointer it copied. */
typedef struct
{
    const behavior_function *f;
    bool *slots, *cells, *locals, *texts, *referenced, *headers;
    /* Per slot: the cell or context it addresses; per cell: the address it
     * holds, or for references the root cell they write. */
    int *addresses, *contents;
    /* Small nonnegative integer constants a slot or cell holds. */
    int *literals, *cell_literals;
    bool memory, results, text_memory, retyped, unsafe, changed;
    /* Whether a result may be written from a value that reaches the caller's. */
    bool returned;
    /* Whether values built in the caller's storage count as the caller's:
     * then only updates that run there may change them. */
    bool built;
} share_flow;

static bool pointer_value(pin_value v, behavior_value_kind kind)
{
    return kind != BEHAVIOR_VALUE || v.container != PIN_CONTAINER_SINGLE ||
           v.type == BF_POINTER_TYPE || v.type == PIN_STRUCT ||
           v.type == PIN_STR || v.type == PIN_NAME || v.type == PIN_UNION ||
           v.type == PIN_POINTER || v.type == PIN_QUERY;
}
static bool text_type(pin_type type)
{
    return type == PIN_STR || type == PIN_NAME;
}
static bool pointer_slot(const behavior_function *f, int slot)
{
    return pointer_value((pin_value){ .type = f->slots[slot] }, f->slot_kinds[slot]);
}
/* VALUE copies separate collection owners. Explicit pointers, queries and
 * views still reach shared targets; opaque union payloads may contain either. */
static bool shared_targets(pin_value value, int depth)
{
    if (value.container == PIN_CONTAINER_VIEW || value.type == PIN_POINTER ||
        value.type == PIN_QUERY || value.type == PIN_UNION) {
        return true;
    }
    value.container = PIN_CONTAINER_SINGLE;
    if (value.type != PIN_STRUCT) {
        return false;
    }
    const type_desc *type = pin_value_type(&value);
    if (!type || depth >= TYPE_MAX_NESTING) {
        return true;
    }
    if (type_alias_layout(type)) {
        const type_desc *target = type_alias_type(type, (type_resolver){ 0 });
        return !target || shared_targets(
                              (pin_value){ .type = PIN_STRUCT,
                                           .type_name = target->name,
                                           .record = target->carrier },
                              depth + 1
                          );
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        const type_desc *child = field->type == PIN_STRUCT
                                   ? type_field_type(field, (type_resolver){ 0 })
                                   : NULL;
        if (shared_targets(
                (pin_value){ .type = field->type,
                             .type_name = child ? child->name : field->type_name,
                             .record = child ? child->carrier : NULL },
                depth + 1
            )) {
            return true;
        }
    }
    return false;
}
static int share_merge(int old, int value)
{
    return old == SHARE_NONE || old == value ? value : SHARE_UNKNOWN;
}
static void share_taint(share_flow *s, bool *bit, bool tainted)
{
    if (tainted && !*bit) {
        *bit = true;
        s->changed = true;
    }
}
static void share_set(share_flow *s, int *field, int value)
{
    int merged = share_merge(*field, value);
    if (merged != *field) {
        *field = merged;
        s->changed = true;
    }
}
static void share_define(share_flow *s, int slot, bool tainted, int address, int literal)
{
    if (slot < 0 || slot >= acount(s->f->slots)) {
        return;
    }
    share_taint(s, &s->slots[slot], tainted && pointer_slot(s->f, slot));
    share_set(s, &s->addresses[slot], address);
    share_set(s, &s->literals[slot], literal);
}
static bool share_cell(const share_flow *s, int cell)
{
    return cell >= 0 && cell < acount(s->f->data_values) && s->cells[cell];
}
static bool share_any_cell(const share_flow *s)
{
    for (int cell = 0; cell < acount(s->f->data_values); cell++) {
        if (s->cells[cell]) {
            return true;
        }
    }
    return false;
}
/* Taint of the value an address reads. */
static bool share_load(const share_flow *s, int address_slot)
{
    int address = s->addresses[address_slot];
    if (s->slots[address_slot] || address == SHARE_CONTEXT) {
        return true;
    }
    if (address >= 0) {
        return s->cells[address];
    }
    if (address == SHARE_RESULTS) {
        return s->results;
    }
    return s->memory || share_any_cell(s);
}
/* Whether an address may read the caller's text as a pointer. */
static bool share_text(const share_flow *s, int address_slot)
{
    int address = s->addresses[address_slot];
    if (address >= 0) {
        return s->texts[address];
    }
    if (address == SHARE_CONTEXT || address == SHARE_RESULTS) {
        return false;
    }
    if (s->text_memory) {
        return true;
    }
    for (int cell = 0; cell < acount(s->f->data_values); cell++) {
        if (s->texts[cell]) {
            return true;
        }
    }
    return false;
}
/* A write through a value reference may change any cell such a reference
 * roots, so none of them keeps a known address or literal. */
static void share_forget_referenced(share_flow *s, int written)
{
    for (int cell = 0; cell < acount(s->f->data_values); cell++) {
        if (cell == written || (written < 0 && s->referenced[cell])) {
            share_set(s, &s->contents[cell], SHARE_UNKNOWN);
            share_set(s, &s->cell_literals[cell], SHARE_UNKNOWN);
        }
    }
}
/* An address derived from `base` reaches what the base can reach. */
static bool share_derived(const share_flow *s, int base)
{
    int address = s->addresses[base];
    return s->slots[base] || address == SHARE_CONTEXT ||
           address == SHARE_RESULTS || (address >= 0 && s->cells[address]);
}
/* A write through an address the analysis cannot place may change any cell,
 * its header included. */
static void share_scatter(share_flow *s, bool tainted)
{
    share_taint(s, &s->memory, tainted);
    share_taint(s, &s->returned, tainted);
    share_taint(s, &s->retyped, true);
    for (int cell = 0; cell < acount(s->f->data_values); cell++) {
        share_taint(s, &s->cells[cell], tainted);
        share_set(s, &s->contents[cell], SHARE_UNKNOWN);
        share_set(s, &s->cell_literals[cell], SHARE_UNKNOWN);
    }
}
/* Header fields a write may change while the value keeps the type and
 * container its copies follow. */
static bool value_field(size_t offset)
{
    return offset == offsetof(pin_value, d) ||
           offset == offsetof(pin_value, type_name) ||
           offset == offsetof(pin_value, record) ||
           offset == offsetof(pin_value, schema);
}
/* The own, untainted cell whose address host argument `cell` holds, or -1. */
static int share_target(const share_flow *s, int cell)
{
    if (cell < 0 || cell >= acount(s->f->data_values)) {
        return -1;
    }
    int target = s->contents[cell];
    return target >= 0 && !s->cells[target] ? target : -1;
}
static void share_host(share_flow *s, int pc)
{
    const behavior_function *f = s->f;
    const behavior_function_host_call *host = &f->host_calls[f->instructions[pc].target];
    bool inputs = false, text = false;
    for (int cell = host->first; cell < host->result_first; cell++) {
        inputs |= share_cell(s, cell);
        text |= s->texts[cell];
    }
    share_taint(s, &s->text_memory, text);
    bool output = inputs;
    int root = SHARE_UNKNOWN;
    uint64_t id = intrinsic(host);
    if (s->built && id && execution_intrinsic_in_result(id)) {
        /* What it builds is the caller's, the array an update of a local
         * leaves included; such an update may change it again. */
        output = execution_intrinsic_kind(id) == EXECUTION_INTRINSIC_CONSTRUCT ||
                 execution_intrinsic_kind(id) == EXECUTION_INTRINSIC_UNION_KEEP;
        if (!output) {
            int updated = s->contents[host->first];
            if (updated < 0) {
                share_scatter(s, true);
            } else {
                share_taint(s, &s->cells[updated], true);
                share_set(s, &s->contents[updated], SHARE_UNKNOWN);
                share_set(s, &s->cell_literals[updated], SHARE_UNKNOWN);
            }
        }
        for (int k = 0; k < host->result_count; k++) {
            share_taint(s, &s->cells[host->result_first + k], output);
            share_set(s, &s->contents[host->result_first + k], SHARE_UNKNOWN);
            share_set(s, &s->cell_literals[host->result_first + k], SHARE_UNKNOWN);
        }
        return;
    }
    if (!id) {
        const node_access *access = host->access;
        unsigned allowed = NODE_EFFECT_PURE | NODE_EFFECT_WORLD_READ |
                           NODE_EFFECT_PARALLEL;
        s->unsafe |= !access || access->write_count ||
                     !(access->flags & (NODE_EFFECT_PURE | NODE_EFFECT_WORLD_READ)) ||
                     (access->flags & ~allowed) || host->call->action;
        /* A native's results own fresh collections: it publishes them, or the
         * call copies them. Only pointers and views inside still reach. */
        output = s->retyped && inputs;
        for (int k = 0; k < host->result_count; k++) {
            output |= inputs &&
                      shared_targets(f->data_values[host->result_first + k].initial, 0);
        }
    } else {
        switch (execution_intrinsic_kind(id)) {
            /* A copy reaches only what its source reaches. */
            case EXECUTION_INTRINSIC_VALUE_COPY:
                output = inputs &&
                         (s->retyped ||
                          shared_targets(f->data_values[host->result_first].initial, 0));
                break;
            /* Copies follow the copied headers; a rewritten one may keep the
             * bits of a pointer instead of copying what it points to. */
            case EXECUTION_INTRINSIC_SNAPSHOT:
            case EXECUTION_INTRINSIC_CONSTRUCT:
            case EXECUTION_INTRINSIC_REFERENCE_READ:
                output = s->retyped && inputs;
                for (int k = 0; k < host->result_count; k++) {
                    output |= inputs &&
                              shared_targets(
                                  f->data_values[host->result_first + k].initial,
                                  0
                              );
                }
                break;
            case EXECUTION_INTRINSIC_UNION_MAKE:
            case EXECUTION_INTRINSIC_UNION_KEEP: {
                /* Both constructors fork ordinary VALUE owners, while payload
                 * references can still reach shared mutable targets. */
                int first = host->first + (execution_intrinsic_kind(id) == EXECUTION_INTRINSIC_UNION_KEEP
                                               ? 2
                                               : 1);
                output = s->retyped && inputs;
                for (int cell = first; cell < host->result_first; cell++) {
                    output |= share_cell(s, cell) &&
                              shared_targets(f->data_values[cell].initial, 0);
                }
                break;
            }
            case EXECUTION_INTRINSIC_POINTER_MAKE:
            case EXECUTION_INTRINSIC_POINTER_READ:
            case EXECUTION_INTRINSIC_POINTER_FIELD:
                /* A pointer is an alias, including when nested in a copied
                 * record. Its pointee never becomes private call storage. */
                output = true;
                break;
            case EXECUTION_INTRINSIC_POINTER_REFERENCE:
                /* A reference through a pointer reaches only its pointee in
                 * the managed heap, never a lent argument; see the write. */
                output = s->built;
                break;
            case EXECUTION_INTRINSIC_POINTER_WRITE:
                /* A pointee lives in the managed heap, never in a lent
                 * argument, and the write copies its value there: it is safe
                 * when what it writes shares nothing. A result built in place
                 * may still update a payload the pointee shares. */
                s->unsafe |= s->built;
                for (int cell = host->first + 1; cell < host->result_first; cell++) {
                    s->unsafe |= share_cell(s, cell) &&
                                 shared_targets(f->data_values[cell].initial, 0);
                }
                output = false;
                break;
            case EXECUTION_INTRINSIC_SEQUENCE_VIEW:
                /* Array views retain the pointer cell's logical pointee. */
                output = f->data_values[host->first].initial.type != PIN_STR ||
                         f->data_values[host->first].initial.container !=
                             PIN_CONTAINER_SINGLE;
                break;
            case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
            case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
                /* Nested values fork their owners. Pointers inside elements
                 * retain shared target provenance after that fork. */
                {
                    pin_value element = f->data_values[host->first].initial;
                    element.container = PIN_CONTAINER_SINGLE;
                    output = s->retyped || shared_targets(element, 0);
                }
                break;
            case EXECUTION_INTRINSIC_SEQUENCE_GET:
            case EXECUTION_INTRINSIC_SEQUENCE_BORROW:
                output = s->retyped ||
                         shared_targets(f->data_values[host->result_first].initial, 0);
                break;
            case EXECUTION_INTRINSIC_SEQUENCE_LENGTH: output = false; break;
            case EXECUTION_INTRINSIC_UNION_TEST:
            case EXECUTION_INTRINSIC_VALUE_EQUAL:
            case EXECUTION_INTRINSIC_VALUE_DROP:
            case EXECUTION_INTRINSIC_LOCAL_RELEASE: output = false; break;
            case EXECUTION_INTRINSIC_UNION_GET: break;
            case EXECUTION_INTRINSIC_REFERENCE_FIELD:
            case EXECUTION_INTRINSIC_REFERENCE_UNION:
                output = share_cell(s, host->first);
                root = s->contents[host->first];
                break;
            case EXECUTION_INTRINSIC_REFERENCE_MAKE:
                /* A value reference made in this frame to an own, untainted
                 * cell writes only that cell. */
                root = share_target(s, host->first + 4);
                output = root < 0 || s->contents[host->first] != SHARE_CONTEXT ||
                         s->cell_literals[host->first + 2] != LANGUAGE_REFERENCE_VALUE;
                if (root >= 0) {
                    share_taint(s, &s->referenced[root], true);
                }
                break;
            case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
            case EXECUTION_INTRINSIC_MAP_DELETE_PLACE:
            case EXECUTION_INTRINSIC_REFERENCE_WRITE: {
                /* Text the value cell holds marked `texts`, not its taint, and
                 * the text input above records where it went. */
                int written = s->contents[host->first];
                execution_intrinsic kind = execution_intrinsic_kind(id);
                bool value = false;
                int first = kind == EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE
                              ? host->first + 2
                              : host->first + 1;
                for (int cell = first; cell < host->result_first; cell++) {
                    value |= share_cell(s, cell) &&
                             shared_targets(f->data_values[cell].initial, 0);
                }
                if (kind == EXECUTION_INTRINSIC_MAP_DELETE_PLACE) {
                    value = false;
                }
                /* A write through a reference that reaches no tracked cell
                 * is safe when what it writes shares nothing. */
                s->unsafe |= share_cell(s, host->first) || (written < 0 && value);
                if (written >= 0) {
                    share_taint(s, &s->cells[written], value);
                }
                share_forget_referenced(s, written);
                output = false;
                break;
            }
            case EXECUTION_INTRINSIC_ARRAY_PUSH:
            case EXECUTION_INTRINSIC_ARRAY_STORE:
            case EXECUTION_INTRINSIC_ARRAY_EXTEND: {
                int target = share_target(s, host->first);
                s->unsafe |= target < 0;
                if (target >= 0) {
                    share_set(s, &s->contents[target], SHARE_UNKNOWN);
                    share_set(s, &s->cell_literals[target], SHARE_UNKNOWN);
                }
                for (int cell = host->first + 1; cell < host->result_first; cell++) {
                    if (share_cell(s, cell) &&
                        shared_targets(f->data_values[cell].initial, 0)) {
                        share_scatter(s, true);
                    }
                }
                output = false;
                break;
            }
            case EXECUTION_INTRINSIC_MAP_STORE:
            case EXECUTION_INTRINSIC_MAP_DELETE: {
                int target = share_target(s, host->first);
                s->unsafe |= target < 0;
                if (target >= 0) {
                    share_set(s, &s->contents[target], SHARE_UNKNOWN);
                    share_set(s, &s->cell_literals[target], SHARE_UNKNOWN);
                }
                output = false;
                break;
            }
            default: s->unsafe = true; break;
        }
    }
    for (int k = 0; k < host->result_count; k++) {
        int cell = host->result_first + k;
        share_taint(s, &s->cells[cell], output);
        /* A text result may be the caller's text; others hold fresh storage
         * the text entered, which `text_memory` covers, unless a rewritten
         * header copied the text's address. */
        share_taint(
            s,
            &s->texts[cell],
            text && (s->retyped || text_type(f->data_values[cell].initial.type))
        );
        share_set(s, &s->contents[cell], root);
        share_set(s, &s->cell_literals[cell], SHARE_UNKNOWN);
    }
}
static void share_instruction(share_flow *s, int pc)
{
    const behavior_function *f = s->f;
    const behavior_function_instruction *i = &f->instructions[pc];
    int out = i->output;
    switch (i->op) {
        case BF_CONSTANT: {
            int literal = i->value.type == PIN_I32 && i->value.i >= 0
                            ? i->value.i
                            : SHARE_UNKNOWN;
            share_define(s, out, false, SHARE_NONE, literal);
            break;
        }
        case BF_COPY_ARRAY:
            /* This raw opcode has no element schema proving pointer-free
             * payloads, so preserve potential shared-target provenance. */
            share_define(s, out, s->slots[i->left], s->addresses[i->left], SHARE_UNKNOWN);
            break;
        case BF_COPY_STRING:
            share_define(s, out, false, SHARE_NONE, SHARE_UNKNOWN);
            break;
        case BF_ARGUMENT:
            /* A scoped reference argument is the caller's location, which it
             * lends for writing: the caller lends no other argument that
             * shares it, so writes through it reach no lent argument. */
            share_define(s, out, f->argument_kinds[i->left] != BEHAVIOR_REFERENCE, SHARE_UNKNOWN, SHARE_UNKNOWN);
            break;
        case BF_VARIABLE:
        case BF_ARRAY:
        case BF_TYPE_NAME:
        case BF_STORAGE_NAME:
        case BF_OWN_NAME:
        case BF_HOST_SERVICE:
        case BF_COMMAND_SERVICE:
        case BF_COMMAND_RESULT:
            share_define(s, out, true, SHARE_UNKNOWN, SHARE_UNKNOWN);
            break;
        case BF_CONTEXT:
            if (i->target == 1) {
                share_define(s, out, false, SHARE_CONTEXT, SHARE_UNKNOWN);
            } else if (i->offset == offsetof(behavior_function_context, results)) {
                share_define(s, out, false, SHARE_RESULTS, SHARE_UNKNOWN);
            } else {
                share_define(s, out, true, SHARE_UNKNOWN, SHARE_UNKNOWN);
            }
            break;
        case BF_LOCAL:
            share_define(s, out, s->locals[i->left], SHARE_UNKNOWN, SHARE_UNKNOWN);
            break;
        case BF_SAVE:
            share_taint(s, &s->locals[i->target], s->slots[i->left]);
            break;
        case BF_HOST_SLOT:
            share_define(s, out, false, i->left, SHARE_UNKNOWN);
            share_taint(s, &s->headers[out], !i->target);
            break;
        case BF_ADD:
        case BF_INDEX:
            if (f->slot_kinds[i->left] != BEHAVIOR_ADDRESS) {
                share_define(s, out, s->slots[i->left] || s->slots[i->right], SHARE_NONE, SHARE_UNKNOWN);
            } else if (
                i->op == BF_INDEX && s->addresses[i->left] == SHARE_RESULTS &&
                s->literals[i->right] >= 0 && s->literals[i->right] < f->result_count
            ) {
                /* A result row at a constant ordinal. */
                share_define(s, out, false, SHARE_RESULTS, SHARE_UNKNOWN);
            } else {
                bool tainted = share_derived(s, i->left) || s->slots[i->right];
                share_define(s, out, tainted, SHARE_UNKNOWN, SHARE_UNKNOWN);
            }
            break;
        case BF_SELECT: {
            int address = share_merge(s->addresses[i->right], s->addresses[i->target]);
            share_define(s, out, s->slots[i->right] || s->slots[i->target], address, SHARE_UNKNOWN);
            share_taint(s, &s->headers[out], s->headers[i->right] || s->headers[i->target]);
            break;
        }
        case BF_LOAD:
        case BF_READ:
            share_define(s, out, share_load(s, i->left) || share_text(s, i->left), SHARE_UNKNOWN, SHARE_UNKNOWN);
            break;
        case BF_WRITE: {
            int address = s->addresses[i->right];
            /* Through a cell's header, only the value and names stay in it. */
            if (address >= 0 && s->headers[i->right] && !value_field(i->offset)) {
                address = SHARE_UNKNOWN;
            }
            bool text = text_type(i->type) && s->slots[i->left];
            bool value = s->slots[i->left] && !text;
            s->unsafe |= s->slots[i->right];
            if (address == SHARE_CONTEXT) {
                /* Only the scalar exit and outcome fields, as a return writes. */
                s->unsafe |= !control_write(i);
            } else if (address >= 0) {
                share_taint(s, &s->cells[address], value);
                share_taint(s, &s->texts[address], text);
                share_set(s, &s->contents[address], s->addresses[i->left]);
                /* Only a whole I32 keeps its literal; other writes may
                 * change part of a wider value. */
                share_set(
                    s,
                    &s->cell_literals[address],
                    f->data_values[address].initial.type == PIN_I32
                        ? s->literals[i->left]
                        : SHARE_UNKNOWN
                );
            } else if (address == SHARE_RESULTS) {
                share_taint(s, &s->results, value || text);
                share_taint(s, &s->returned, value || text);
            } else {
                share_scatter(s, value);
                share_taint(s, &s->text_memory, text);
            }
            break;
        }
        case BF_RESULT:
            s->unsafe |= i->indirect;
            share_taint(s, &s->results, s->slots[i->left]);
            share_taint(s, &s->returned, s->slots[i->left]);
            break;
        case BF_RESULT_PRESENT:
        case BF_RESULT_TYPE_NAME:
        case BF_RESULT_ARRAY:
            share_taint(s, &s->results, s->slots[i->left]);
            share_taint(s, &s->returned, s->slots[i->left]);
            break;
        case BF_HOST_CALL: share_host(s, pc); break;
        case BF_INVOKE: {
            const behavior_function_invocation *call = &f->calls[i->left];
            const behavior_function *callee = call->function;
            s->unsafe |= call->task;
            /* A value argument reaches the callee as a copy, or shared with
             * one that changes none; a raw address to what this definition
             * builds would let any other callee write through it. */
            for (int k = 0; s->built && k < call->count; k++) {
                const behavior_function_data_value *argument = &f->data_values[call->first + k];
                s->unsafe |= share_cell(s, call->first + k) &&
                             (argument->kind != BEHAVIOR_VALUE ||
                              argument->initial.type == BF_POINTER_TYPE) &&
                             callee->argument_sharing != BEHAVIOR_SHARING_SAFE;
            }
            /* A callee's result that reaches shared targets may alias its
             * arguments or state unless its result-building proof or its fresh
             * results establish independence. */
            for (int k = 0; k < call->result_count; k++) {
                int cell = call->result_first + k;
                bool raw = k >= callee->result_count ||
                           callee->result_kinds[k] != BEHAVIOR_VALUE ||
                           callee->result_defaults[k].type == BF_POINTER_TYPE;
                bool retained = k < callee->result_count &&
                                !callee->builds_results && !callee->fresh_results &&
                                shared_targets(callee->result_defaults[k], 0);
                share_taint(s, &s->cells[cell], raw || retained);
                share_set(s, &s->contents[cell], SHARE_UNKNOWN);
                share_set(s, &s->cell_literals[cell], SHARE_UNKNOWN);
            }
            break;
        }
#define SHARE_MATH(op, name, fn) case BF_##op:
            BEHAVIOR_MATH_UNARY(SHARE_MATH)
            BEHAVIOR_MATH_BINARY(SHARE_MATH)
#undef SHARE_MATH
        case BF_DELTA:
        case BF_PRESENT:
        case BF_OWNER:
        case BF_RANDOM:
        case BF_RANDOM_RANGE:
        case BF_STRING_EQUAL:
        case BF_SUB:
        case BF_MUL:
        case BF_DIV:
        case BF_MOD:
        case BF_NEG:
        case BF_ABS:
        case BF_MIN:
        case BF_MAX:
        case BF_CONVERT:
        case BF_CAST:
        case BF_FLAGS:
        case BF_LT:
        case BF_LE:
        case BF_GT:
        case BF_GE:
        case BF_EQ:
        case BF_NE:
        case BF_BIT_AND:
        case BF_BIT_OR:
        case BF_BIT_XOR:
        case BF_BIT_NOT:
        case BF_SHL:
        case BF_SHR:
        case BF_MOD_F32:
        case BF_MOD_F64:
        case BF_HYPOT:
        case BF_FMA:
            share_define(s, out, false, SHARE_NONE, SHARE_UNKNOWN);
            break;
        case BF_REQUIRE:
        case BF_ASSERT:
        case BF_POINT:
        case BF_WATCH_BEGIN:
        case BF_WATCH_RESET:
        case BF_WATCH_VALUE:
        case BF_WATCH_PRESENT:
        case BF_WATCH_END:
        case BF_TRACE:
        case BF_CHECK:
        case BF_CALL_CHECK:
        case BF_ENTER:
        case BF_LEAVE:
        case BF_FAIL:
        case BF_JUMP:
        case BF_BRANCH:
        case BF_RETURN:
        case BF_SOURCE_POINT:
        case BF_SOURCE_WATCH:
        case BF_END: break;
        default: s->unsafe = true; break;
    }
}
static int *share_column(int count)
{
    int *column = xmalloc(((size_t)count + 1) * sizeof *column);
    for (int i = 0; i <= count; i++) {
        column[i] = SHARE_NONE;
    }
    return column;
}
/* Whether a result-storage update may run after union_keep has kept a value,
 * which the union would then share with the local it updates. */
static bool updates_kept(const share_flow *s)
{
    const behavior_function *f = s->f;
    int count = acount(f->instructions), *pending = NULL;
    bool *seen = xcalloc((size_t)count + 1, sizeof *seen), updates = false;
    for (int pc = 0; pc < count; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op != BF_HOST_CALL ||
            execution_intrinsic_kind(intrinsic(&f->host_calls[i->target])) !=
                EXECUTION_INTRINSIC_UNION_KEEP) {
            continue;
        }
        if (s->cell_literals[f->host_calls[i->target].first + 1] != 0) {
            apush(pending, pc);
        }
    }
    while (acount(pending) && !updates) {
        int pc = pending[acount(pending) - 1], targets[2];
        asetlen(pending, acount(pending) - 1);
        for (int k = 0, n = behavior_function_successors(f, pc, targets); k < n; k++) {
            if (seen[targets[k]]) {
                continue;
            }
            seen[targets[k]] = true;
            const behavior_function_instruction *next = &f->instructions[targets[k]];
            if (next->op == BF_HOST_CALL) {
                uint64_t id = intrinsic(&f->host_calls[next->target]);
                execution_intrinsic kind = execution_intrinsic_kind(id);
                updates |= execution_intrinsic_in_result(id) &&
                           (kind == EXECUTION_INTRINSIC_ARRAY_PUSH ||
                            kind == EXECUTION_INTRINSIC_ARRAY_STORE ||
                            kind == EXECUTION_INTRINSIC_ARRAY_EXTEND);
            }
            apush(pending, targets[k]);
        }
    }
    afree(pending);
    free(seen);
    return updates;
}
/* One definition's own rules, ignoring what its callees do. Results start
 * tainted: they hold the caller's result cells until written. With `built`,
 * what the definition builds in its caller's storage is tainted too, and no
 * result-storage update may follow a union_keep that may keep a value.
 * `returned`, when given, says whether a result may reach the caller's. */
static bool share_local(const behavior_function *f, bool built, bool *returned)
{
    int slots = acount(f->slots), cells = acount(f->data_values),
        locals = acount(f->locals);
    share_flow s = { .f = f, .results = true, .built = built };
    s.slots = xcalloc((size_t)slots + 1, sizeof *s.slots);
    s.cells = xcalloc((size_t)cells + 1, sizeof *s.cells);
    s.locals = xcalloc((size_t)locals + 1, sizeof *s.locals);
    s.texts = xcalloc((size_t)cells + 1, sizeof *s.texts);
    s.referenced = xcalloc((size_t)cells + 1, sizeof *s.referenced);
    s.headers = xcalloc((size_t)slots + 1, sizeof *s.headers);
    s.addresses = share_column(slots);
    s.literals = share_column(slots);
    s.contents = share_column(cells);
    s.cell_literals = share_column(cells);
    /* A cell no instruction writes keeps its initial value, as reference
     * kinds do; writes merge with it. */
    for (int cell = 0; cell < cells; cell++) {
        pin_value initial = f->data_values[cell].initial;
        if (initial.type == PIN_I32 &&
            initial.container == PIN_CONTAINER_SINGLE && initial.i >= 0) {
            s.cell_literals[cell] = initial.i;
        }
        if (initial.type == PIN_U64 &&
            initial.container == PIN_CONTAINER_SINGLE && initial.u64 <= INT_MAX) {
            s.cell_literals[cell] = (int)initial.u64;
        }
    }
    int passes = 0;
    do {
        s.changed = false;
        for (int pc = 0; pc < acount(f->instructions) && !s.unsafe; pc++) {
            share_instruction(&s, pc);
        }
    } while (s.changed && !s.unsafe && ++passes < SHARE_PASSES);
    bool safe = !s.unsafe && !s.changed && !(built && updates_kept(&s));
    if (returned) {
        *returned = s.returned;
    }
    free(s.slots);
    free(s.cells);
    free(s.locals);
    free(s.texts);
    free(s.referenced);
    free(s.headers);
    free(s.addresses);
    free(s.literals);
    free(s.contents);
    free(s.cell_literals);
    return safe;
}
static int by_definition(const void *a, const void *b)
{
    uintptr_t x = (uintptr_t)(*(behavior_function *const *)a);
    uintptr_t y = (uintptr_t)(*(behavior_function *const *)b);
    return (x > y) - (x < y);
}
/* A definition with one result builds it in its caller's storage when only
 * the updates that run there change what it builds, none after union_keep
 * keeps a value, and no result may be the caller's own data. Without `built`,
 * what the definition builds is its own, so a result that reaches the
 * caller's came from its arguments or other data: moving it would let the
 * caller's result share that data. Two results could share what they reach. */
bool execution_builds_safely(const behavior_function *f)
{
    bool returned = true;
    return f->result_count <= 1 && share_local(f, true, NULL) &&
           share_local(f, false, &returned) && !returned;
}
/* Each pending definition's own verdict, and whether its results are fresh: no
 * result reaches what its arguments or state reach. A verdict takes pending
 * callees' results as fresh until their own rules show otherwise, and then
 * their callers decide again; verdicts only ever weaken. `callers` lists, for
 * each definition in `sorted` order, the pending indices of its callers. */
static void decide_locally(
    behavior_function **pending,
    behavior_function **sorted,
    const int *starts,
    const int *callers
)
{
    int count = acount(pending), *queue = NULL;
    bool *queued = xmalloc((size_t)count * sizeof *queued);
    for (int at = count - 1; at >= 0; at--) {
        apush(queue, at);
        queued[at] = true;
    }
    while (acount(queue)) {
        int at = apop(queue);
        behavior_function *f = pending[at];
        queued[at] = false;
        bool returned = true;
        bool safe = f->frozen && share_local(f, false, &returned);
        if (!safe) {
            f->argument_sharing = BEHAVIOR_SHARING_PENDING_COPY;
        }
        if (!f->fresh_results || (safe && !returned)) {
            continue;
        }
        f->fresh_results = false;
        behavior_function **found = bsearch(&f, sorted, (size_t)count, sizeof *sorted, by_definition);
        int callee = (int)(found - sorted);
        for (int c = starts[callee]; c < starts[callee + 1]; c++) {
            if (!queued[callers[c]]) {
                queued[callers[c]] = true;
                apush(queue, callers[c]);
            }
        }
    }
    afree(queue);
    free(queued);
}
void execution_prepare_argument_sharing(behavior_function *root)
{
    if (!root || root->argument_sharing != BEHAVIOR_SHARING_UNKNOWN) {
        return;
    }
    /* Every reachable definition still unknown takes a pending verdict from its
     * own rules; known callees are final. A copying verdict then flows back to
     * every caller over reverse call edges. Definitions are immutable; this
     * derived verdict is their publication cache, as the borrow proof's is. */
    behavior_function **pending = NULL, **copying = NULL;
    root->argument_sharing = BEHAVIOR_SHARING_PENDING_SAFE;
    apush(pending, root);
    for (int at = 0; at < acount(pending); at++) {
        behavior_function *f = pending[at];
        /* Fresh until decide_locally shows otherwise. */
        f->fresh_results = true;
        for (int i = 0; i < acount(f->calls); i++) {
            behavior_function *callee = (behavior_function *)f->calls[i].function;
            if (callee->argument_sharing == BEHAVIOR_SHARING_UNKNOWN) {
                callee->argument_sharing = BEHAVIOR_SHARING_PENDING_SAFE;
                apush(pending, callee);
            }
        }
    }
    int count = acount(pending);
    behavior_function **sorted = xmalloc((size_t)count * sizeof *sorted);
    memcpy(sorted, pending, (size_t)count * sizeof *sorted);
    qsort(sorted, (size_t)count, sizeof *sorted, by_definition);
    /* Callers of each pending definition, grouped by callee. */
    int *starts = xcalloc((size_t)count + 1, sizeof *starts), *callers = NULL;
    int *edges = NULL;
    for (int at = 0; at < count; at++) {
        const behavior_function *f = pending[at];
        for (int i = 0; i < acount(f->calls); i++) {
            behavior_function *callee = (behavior_function *)f->calls[i].function;
            behavior_function **found = bsearch(&callee, sorted, (size_t)count, sizeof *sorted, by_definition);
            if (!found) {
                if (callee->argument_sharing != BEHAVIOR_SHARING_SAFE) {
                    pending[at]->argument_sharing = BEHAVIOR_SHARING_PENDING_COPY;
                }
                continue;
            }
            apush(edges, (int)(found - sorted));
            apush(edges, at);
            starts[found - sorted + 1]++;
        }
    }
    for (int at = 0; at < count; at++) {
        starts[at + 1] += starts[at];
    }
    asetlen(callers, starts[count]);
    int *fill = xmalloc(((size_t)count + 1) * sizeof *fill);
    memcpy(fill, starts, ((size_t)count + 1) * sizeof *fill);
    for (int e = 0; e + 1 < acount(edges); e += 2) {
        callers[fill[edges[e]]++] = edges[e + 1];
    }
    decide_locally(pending, sorted, starts, callers);
    for (int at = 0; at < count; at++) {
        if (pending[at]->argument_sharing == BEHAVIOR_SHARING_PENDING_COPY) {
            apush(copying, pending[at]);
        }
    }
    while (acount(copying)) {
        behavior_function *f = apop(copying);
        behavior_function **found = bsearch(&f, sorted, (size_t)count, sizeof *sorted, by_definition);
        int callee = (int)(found - sorted);
        for (int c = starts[callee]; c < starts[callee + 1]; c++) {
            behavior_function *caller = pending[callers[c]];
            if (caller->argument_sharing == BEHAVIOR_SHARING_PENDING_SAFE) {
                caller->argument_sharing = BEHAVIOR_SHARING_PENDING_COPY;
                apush(copying, caller);
            }
        }
    }
    for (int at = 0; at < count; at++) {
        pending[at]->argument_sharing = pending[at]->argument_sharing == BEHAVIOR_SHARING_PENDING_SAFE
                                          ? BEHAVIOR_SHARING_SAFE
                                          : BEHAVIOR_SHARING_COPY;
    }
    afree(pending);
    afree(copying);
    afree(edges);
    afree(callers);
    free(sorted);
    free(starts);
    free(fill);
}

/* Task dependencies follow argument values through one definition. Weak
 * masks hold what a slot, cell or local may carry at any point; within one
 * straight-line block, a value defined there replaces them, since banks and
 * slots are reused. A dereference on any path counts, so removal cancels a
 * task before a continuation that would have skipped it. A value stored where
 * the flow cannot place it may come back from any read it cannot place. */
enum
{
    DEPEND_NO_CELL = -1,
    DEPEND_ANY_CELL = -2
};
typedef struct
{
    uint64_t *weak, *now;
    int *block; /* Where `now` was defined. */
} depend_values;
typedef struct
{
    const behavior_function *f;
    depend_values slots, cells;
    uint64_t *locals;
    int *addressed, *addressed_now, *addressed_block; /* A cell or an enum above. */
    int block; /* The current block, numbered from 1; 0 in the weak passes. */
    uint64_t memory, accessed;
    bool changed;
} depend_flow;

static void depend_join(depend_flow *s, uint64_t *mask, uint64_t value)
{
    if ((*mask | value) != *mask) {
        *mask |= value;
        s->changed = true;
    }
}
static depend_values depend_values_new(int count)
{
    return (depend_values){ .weak = xcalloc((size_t)count + 1, sizeof(uint64_t)),
                            .now = xcalloc((size_t)count + 1, sizeof(uint64_t)),
                            .block = xcalloc((size_t)count + 1, sizeof(int)) };
}
static void depend_values_free(depend_values v)
{
    free(v.weak);
    free(v.now);
    free(v.block);
}
static uint64_t depend_get(const depend_flow *s, const depend_values *v, int index)
{
    return s->block && v->block[index] == s->block ? v->now[index] : v->weak[index];
}
/* `replace` drops what the value held before this block's first write. */
static void depend_set(depend_flow *s, depend_values *v, int index, uint64_t value, bool replace)
{
    depend_join(s, &v->weak[index], value);
    if (!s->block) {
        return;
    }
    if (v->block[index] != s->block) {
        v->now[index] = replace ? 0 : v->weak[index];
        v->block[index] = s->block;
    }
    v->now[index] |= value;
}
static int depend_address_of(const depend_flow *s, int slot)
{
    return s->block && s->addressed_block[slot] == s->block ? s->addressed_now[slot]
                                                            : s->addressed[slot];
}
static void depend_address(depend_flow *s, int slot, int cell)
{
    int old = s->addressed[slot];
    int merged = old == DEPEND_NO_CELL || old == cell ? cell : DEPEND_ANY_CELL;
    if (merged != old) {
        s->addressed[slot] = merged;
        s->changed = true;
    }
    if (s->block) {
        s->addressed_now[slot] = cell;
        s->addressed_block[slot] = s->block;
    }
}
static uint64_t depend_load(const depend_flow *s, int address)
{
    int cell = depend_address_of(s, address);
    uint64_t value = depend_get(s, &s->slots, address) | s->memory;
    for (int c = 0; cell == DEPEND_ANY_CELL && c < acount(s->f->data_values); c++) {
        value |= depend_get(s, &s->cells, c);
    }
    return cell >= 0 ? value | depend_get(s, &s->cells, cell) : value;
}
static void depend_store(depend_flow *s, const behavior_function_instruction *i)
{
    int cell = depend_address_of(s, i->right);
    uint64_t value = depend_get(s, &s->slots, i->left);
    if (cell >= 0) {
        /* Writing the payload word replaces the pointer a cell held. */
        bool payload = i->offset == offsetof(pin_value, d) &&
                       behavior_scalar_size(i->type) >= sizeof(void *);
        depend_set(s, &s->cells, cell, value, payload);
        return;
    }
    depend_join(s, &s->memory, value);
    for (int c = 0; cell == DEPEND_ANY_CELL && c < acount(s->f->data_values); c++) {
        depend_set(s, &s->cells, c, value, false);
    }
}
static void depend_access(depend_flow *s, uint64_t value)
{
    if (s->block) {
        depend_join(s, &s->accessed, value);
    }
}
static void depend_host(depend_flow *s, const behavior_function_host_call *host)
{
    uint64_t inputs = 0;
    for (int cell = host->first; cell < host->result_first; cell++) {
        inputs |= depend_get(s, &s->cells, cell);
    }
    switch (execution_intrinsic_kind(host->capability_id)) {
        case EXECUTION_INTRINSIC_POINTER_WRITE:
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
            depend_join(s, &s->memory, inputs);
            depend_access(s, depend_get(s, &s->cells, host->first));
            break;
        /* A scoped parameter reaches its lent target through references. */
        case EXECUTION_INTRINSIC_POINTER_READ:
        case EXECUTION_INTRINSIC_POINTER_FIELD:
        case EXECUTION_INTRINSIC_POINTER_REFERENCE:
        case EXECUTION_INTRINSIC_REFERENCE_READ:
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
            depend_access(s, depend_get(s, &s->cells, host->first));
            break;
        /* Comparing values keeps neither. */
        case EXECUTION_INTRINSIC_VALUE_EQUAL: inputs = 0; break;
        default: depend_join(s, &s->memory, inputs); break;
    }
    for (int k = 0; k < host->result_count; k++) {
        depend_set(s, &s->cells, host->result_first + k, inputs, true);
    }
}
/* A synchronous callee's dereference is this definition's. A task started
 * with the pointer takes its own dependency instead. */
static void depend_invoke(depend_flow *s, const behavior_function_invocation *call)
{
    uint64_t arguments = 0;
    for (int k = 0; k < call->count; k++) {
        uint64_t argument = depend_get(s, &s->cells, call->first + k);
        arguments |= argument;
        if (!call->task &&
            (call->function->task_dependencies & execution_task_argument_bit(k))) {
            depend_access(s, argument);
        }
    }
    if (!call->task) {
        depend_join(s, &s->memory, arguments);
    }
    for (int k = 0; k < call->result_count; k++) {
        depend_set(s, &s->cells, call->result_first + k, call->task ? 0 : arguments, true);
    }
}
static void depend_operands(depend_flow *s, const behavior_function_instruction *i)
{
    int operands[3] = { -1, -1, -1 };
    if (behavior_function_left_slot(i->op)) {
        operands[0] = i->left;
    }
    if (behavior_function_right_slot(i->op)) {
        operands[1] = i->right;
    }
    if (behavior_function_third_slot(i->op)) {
        operands[2] = i->target;
    }
    uint64_t value = 0;
    bool address = false;
    for (int k = 0; k < 3; k++) {
        if (operands[k] >= 0) {
            value |= depend_get(s, &s->slots, operands[k]);
            address |= depend_address_of(s, operands[k]) != DEPEND_NO_CELL;
        }
    }
    depend_set(s, &s->slots, i->output, value, true);
    if (!address) {
        return;
    }
    /* A selected cell address keeps its cell; one computed from a cell's
     * address may point into any cell. */
    int selected = depend_address_of(s, i->right);
    bool same = i->op == BF_SELECT && selected == depend_address_of(s, i->target);
    depend_address(s, i->output, same ? selected : DEPEND_ANY_CELL);
}
static void depend_instruction(depend_flow *s, const behavior_function_instruction *i)
{
    const behavior_function *f = s->f;
    int out = i->output;
    switch (i->op) {
        case BF_ARGUMENT:
            depend_set(s, &s->slots, out, execution_task_argument_bit(i->left), true);
            return;
        case BF_CONTEXT: {
            /* Fields other than the exit and outcome words may reach any
             * argument or cell. */
            bool control = i->target == 1 ||
                           i->offset == offsetof(behavior_function_context, results);
            depend_set(s, &s->slots, out, control ? 0 : UINT64_MAX, true);
            if (!control) {
                depend_address(s, out, DEPEND_ANY_CELL);
            }
            return;
        }
        case BF_HOST_SLOT:
            depend_set(s, &s->slots, out, 0, true);
            depend_address(s, out, i->target ? DEPEND_ANY_CELL : i->left);
            return;
        case BF_LOCAL:
            depend_set(s, &s->slots, out, s->locals[i->left], true);
            return;
        case BF_SAVE:
            depend_join(s, &s->locals[i->target], depend_get(s, &s->slots, i->left));
            return;
        case BF_LOAD:
        case BF_READ:
            depend_set(s, &s->slots, out, depend_load(s, i->left), true);
            return;
        case BF_WRITE: depend_store(s, i); return;
        case BF_HOST_CALL: depend_host(s, &f->host_calls[i->target]); return;
        case BF_INVOKE: depend_invoke(s, &f->calls[i->left]); return;
        default: break;
    }
    if (out >= 0 && out < acount(f->slots)) {
        depend_operands(s, i);
    }
}
/* Blocks start at jump targets and after jumps; resumed continuations
 * restore the values their block had, so they need no boundary. */
static bool *depend_leaders(const behavior_function *f)
{
    int count = acount(f->instructions);
    bool *leaders = xcalloc((size_t)count + 1, sizeof *leaders);
    leaders[0] = true;
    for (int pc = 0; pc < count; pc++) {
        int targets[2], n = behavior_function_successors(f, pc, targets);
        for (int k = 0; k < n; k++) {
            if (targets[k] != pc + 1) {
                leaders[targets[k]] = true;
                leaders[pc + 1] = true;
            }
        }
    }
    return leaders;
}
static uint64_t depend_local(const behavior_function *f)
{
    int slots = acount(f->slots), cells = acount(f->data_values),
        count = acount(f->instructions);
    depend_flow s = { .f = f,
                      .slots = depend_values_new(slots),
                      .cells = depend_values_new(cells) };
    s.locals = xcalloc((size_t)acount(f->locals) + 1, sizeof *s.locals);
    s.addressed = xmalloc(((size_t)slots + 1) * sizeof *s.addressed);
    s.addressed_now = xcalloc((size_t)slots + 1, sizeof *s.addressed_now);
    s.addressed_block = xcalloc((size_t)slots + 1, sizeof *s.addressed_block);
    for (int slot = 0; slot <= slots; slot++) {
        s.addressed[slot] = DEPEND_NO_CELL;
    }
    /* Weak masks and addresses only grow, so the passes end. */
    do {
        s.changed = false;
        for (int pc = 0; pc < count; pc++) {
            depend_instruction(&s, &f->instructions[pc]);
        }
    } while (s.changed);
    bool *leaders = depend_leaders(f);
    for (int pc = 0; pc < count; pc++) {
        s.block += leaders[pc];
        depend_instruction(&s, &f->instructions[pc]);
    }
    free(leaders);
    depend_values_free(s.slots);
    depend_values_free(s.cells);
    free(s.locals);
    free(s.addressed);
    free(s.addressed_now);
    free(s.addressed_block);
    return s.accessed;
}
void execution_prepare_task_dependencies(behavior_function *root)
{
    if (!root || root->task_dependencies_ready) {
        return;
    }
    /* Reachable definitions not yet settled start empty and grow together
     * until no callee's dependencies change a caller's. */
    behavior_function **pending = NULL;
    root->task_dependencies_ready = true;
    apush(pending, root);
    for (int at = 0; at < acount(pending); at++) {
        for (int i = 0; i < acount(pending[at]->calls); i++) {
            behavior_function
                *callee = (behavior_function *)pending[at]->calls[i].function;
            if (!callee->task_dependencies_ready) {
                callee->task_dependencies_ready = true;
                apush(pending, callee);
            }
        }
    }
    bool changed;
    do {
        changed = false;
        for (int at = 0; at < acount(pending); at++) {
            uint64_t dependencies = depend_local(pending[at]);
            changed |= dependencies != pending[at]->task_dependencies;
            pending[at]->task_dependencies = dependencies;
        }
    } while (changed);
    afree(pending);
}
