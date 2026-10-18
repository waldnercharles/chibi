#include "execution/row_builder.h"

#include "execution/borrow.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array_internal.h"
#include "types/record.h"

#include <stdlib.h>
#include <string.h>

enum
{
    ROW_PROOF_LIMIT = 8192,
    ROW_BUFFER_LIMIT = 65536
};
static bool scalar_allowed(const behavior_function *, const behavior_function_context *);

typedef enum
{
    OTHER,
    ZERO,
    ONE,
    BANK,
    EMPTY_CONSTRUCT,
    EMPTY_RETAIN,
    ROW_CONSTRUCT,
    ROW_RETAIN,
    PREFIX,
    ADDED,
    RETAINED,
    COUNTER,
    NEXT_COUNTER,
    CONTEXT,
    RESULTS,
    RESULT_HEADER
} row_kind;
typedef struct
{
    row_kind kind;
    int index;
} row_value;
typedef struct
{
    const behavior_function *f;
    int *definitions;
    int begin, branch, end, add_pc, retain_pc, construct_pc, base, counter, work;
    behavior_row_plan plan;
} row_proof;

static bool value_call(const node_host_call *call, node_value_call_fn value)
{
    return call && call->value == value && !call->invoke && !call->context &&
           !call->action && !call->service && !call->missing_service &&
           !call->optional_service && !call->variadic && !call->reusable;
}
static bool intrinsic(const behavior_function_host_call *h, const node_host_call *call)
{
    uint64_t id = execution_intrinsic_id(call);
    return value_call(h->call, call->value) && h->capability_id == id &&
           h->capability_abi == execution_intrinsic_abi(id) && h->result_count == 1;
}
/* Ordinary finalization has already validated these immutable headers.
 * The proof constrains their shape without repeating descriptor validation. */
static bool record_array(pin_value value)
{
    return value.type == PIN_STRUCT && value.container == PIN_CONTAINER_ARRAY &&
           value.record && value.record->type.field_count <= 64 &&
           value.record->bytes <= ROW_BUFFER_LIMIT && value.type_name &&
           behavior_borrow_name(value.type_name, value.record->type.name);
}
static bool same_shape(pin_value a, pin_value b)
{
    return a.type == b.type && a.container == b.container &&
           behavior_borrow_name(a.type_name, b.type_name) &&
           pin_record_same(a.record, b.record);
}
static bool add_call(const behavior_function *f, int host)
{
    const behavior_function_host_call *h = &f->host_calls[host];
    if (!value_call(h->call, pin_array_add_call) || !h->capability_id ||
        !h->capability_abi || h->result_first != h->first + 2 || h->result_count != 1) {
        return false;
    }
    pin_value array = f->data_values[h->first].initial,
              row = f->data_values[h->first + 1].initial,
              result = f->data_values[h->result_first].initial;
    row.container = PIN_CONTAINER_ARRAY;
    return f->data_values[h->first + 1].initial.container == PIN_CONTAINER_SINGLE &&
           record_array(array) && record_array(row) && record_array(result) &&
           same_shape(array, row) && same_shape(array, result);
}
static const behavior_function_instruction *definition(const row_proof *p, int slot)
{
    if (slot < 0 || slot >= acount(p->f->slots) || p->definitions[slot] < 0) {
        return NULL;
    }
    return &p->f->instructions[p->definitions[slot]];
}
static int address_bank(const row_proof *p, int slot)
{
    const behavior_function_instruction *i = definition(p, slot);
    return i && i->op == BF_HOST_SLOT && !i->target ? i->left : -1;
}
static int loaded_bank(const row_proof *p, int slot)
{
    const behavior_function_instruction *i = definition(p, slot);
    return i && i->op == BF_LOAD && i->offset == offsetof(pin_value, d)
             ? address_bank(p, i->left)
             : -1;
}
/* Only a candidate locator. The forward proof below checks every write and
 * effect; this search must not be mistaken for dominance or alias analysis. */
static int input_slot(row_proof *p, int pc, int bank)
{
    for (int at = pc - 1; at >= 0; at--) {
        if (++p->work > ROW_PROOF_LIMIT * 8) {
            return -1;
        }
        const behavior_function_instruction *i = &p->f->instructions[at];
        if (i->op == BF_WRITE && address_bank(p, i->right) == bank) {
            return i->offset == offsetof(pin_value, d) ? i->left : -1;
        }
    }
    return -1;
}
static bool locate(row_proof *p)
{
    const behavior_function *f = p->f;
    int back = -1, branches = 0, jumps = 0, add = -1;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            if (p->definitions[i->output] >= 0) {
                return false;
            }
            p->definitions[i->output] = pc;
        }
        if ((i->op == BF_DIV || i->op == BF_MOD) &&
            behavior_scalar_integer(i->type) && i->target >= 0) {
            if (p->definitions[i->target] >= 0) {
                return false;
            }
            p->definitions[i->target] = pc;
        }
        if (i->op == BF_BRANCH) {
            p->branch = pc;
            branches++;
        }
        if (i->op == BF_JUMP) {
            jumps++;
            if (i->target <= pc) {
                if (back >= 0) {
                    return false;
                }
                back = pc;
                p->begin = i->target;
            }
        }
        if (i->op == BF_HOST_CALL && add_call(f, i->target)) {
            if (add >= 0) {
                return false;
            }
            add = pc;
        }
    }
    if (branches != 1 || jumps != 2 || back < 0 || add < 0 ||
        p->begin > p->branch || p->branch + 2 > add || add >= back) {
        return false;
    }
    p->end = back;
    p->add_pc = add;
    const behavior_function_instruction *branch = &f->instructions[p->branch],
                                        *exit = &f->instructions[p->branch + 1],
                                        *condition = definition(p, branch->left);
    if (branch->target != p->branch + 2 || exit->op != BF_JUMP ||
        exit->target != back + 1 || !condition || condition->op != BF_LT ||
        condition->type != PIN_I32 || p->definitions[branch->left] < p->begin ||
        p->definitions[branch->left] >= p->branch ||
        p->definitions[condition->left] < p->begin ||
        p->definitions[condition->left] >= p->definitions[branch->left] ||
        p->definitions[condition->right] < p->begin ||
        p->definitions[condition->right] >= p->definitions[branch->left]) {
        return false;
    }
    p->counter = loaded_bank(p, condition->left);
    const behavior_function_instruction *argument = definition(p, condition->right);
    if (p->counter < 0 || !argument || argument->op != BF_ARGUMENT ||
        argument->indirect || argument->type != PIN_I32 ||
        f->argument_kinds[argument->left] != BEHAVIOR_VALUE ||
        f->argument_defaults[argument->left].type != PIN_I32 ||
        f->argument_defaults[argument->left].container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    p->plan.argument = argument->left;
    p->plan.add = f->instructions[add].target;
    const behavior_function_host_call *h = &f->host_calls[p->plan.add];
    p->plan.local = loaded_bank(p, input_slot(p, add, h->first));
    if (p->plan.local < 0 || p->plan.local == p->counter ||
        !same_shape(
            f->data_values[p->plan.local].initial,
            f->data_values[h->first].initial
        )) {
        return false;
    }
    p->plan.id = h->capability_id;
    p->plan.abi = h->capability_abi;
    p->plan.nominal = f->data_values[h->first].initial.record->id;
    p->plan.layout = f->data_values[h->first].initial.record->layout;
    p->construct_pc = -1;
    for (int pc = p->begin; pc < back; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op != BF_HOST_CALL) {
            continue;
        }
        const behavior_function_host_call *construct = &f->host_calls[i->target];
        if (!intrinsic(construct, &EXECUTION_construct_call) ||
            !same_shape(
                f->data_values[construct->first].initial,
                f->data_values[h->first + 1].initial
            )) {
            continue;
        }
        if (p->construct_pc >= 0 || pc >= add ||
            construct->first >= construct->result_first ||
            f->data_values[construct->first].kind != BEHAVIOR_VALUE ||
            f->data_values[construct->first].initial.ptr) {
            return false;
        }
        p->construct_pc = pc;
        p->base = construct->first;
    }
    if (p->construct_pc < 0) {
        return false;
    }
    p->retain_pc = -1;
    for (int pc = add + 1; pc < back; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op != BF_HOST_CALL) {
            continue;
        }
        const behavior_function_host_call *retain = &f->host_calls[i->target];
        if (intrinsic(retain, &EXECUTION_snapshot_call) &&
            retain->result_first == retain->first + 1 &&
            loaded_bank(p, input_slot(p, pc, retain->first)) == h->result_first) {
            if (p->retain_pc >= 0) {
                return false;
            }
            p->retain_pc = pc;
            p->plan.retain = i->target;
        }
    }
    return p->retain_pc >= 0;
}
static bool address(row_value v)
{
    return v.kind == BANK || v.kind == CONTEXT || v.kind == RESULTS ||
           v.kind == RESULT_HEADER;
}
static bool sensitive(row_value v)
{
    return v.kind == PREFIX || v.kind == ADDED || v.kind == RETAINED;
}
static bool prove(row_proof *p)
{
    const behavior_function *f = p->f;
    row_value *slots = xcalloc((size_t)acount(f->slots) + 1, sizeof *slots),
              *banks = xcalloc((size_t)acount(f->data_values) + 1, sizeof *banks),
              *locals = xcalloc((size_t)acount(f->locals) + 1, sizeof *locals);
    const behavior_function_host_call *add = &f->host_calls[p->plan.add],
                                      *retain = &f->host_calls[p->plan.retain];
    int *owners = xmalloc(((size_t)acount(f->data_values) + 1) * sizeof *owners);
    for (int j = 0; j <= acount(f->data_values); j++) {
        owners[j] = -1;
    }
    bool distinct = true;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (f->instructions[pc].op != BF_HOST_CALL) {
            continue;
        }
        int host = f->instructions[pc].target;
        const behavior_function_host_call *h = &f->host_calls[host];
        for (int j = h->first; j < h->result_first + h->result_count; j++) {
            if (++p->work > ROW_PROOF_LIMIT * 8 ||
                (owners[j] >= 0 && owners[j] != host)) {
                distinct = false;
                break;
            }
            owners[j] = host;
        }
        if (!distinct) {
            break;
        }
    }
    distinct &= owners[p->plan.local] == -1 && owners[p->counter] == -1;
    distinct &= owners[p->base] == f->instructions[p->construct_pc].target;
    bool *local_mutable = xcalloc((size_t)acount(f->locals) + 1, sizeof *local_mutable),
         *local_written = xcalloc((size_t)acount(f->locals) + 1, sizeof *local_written);
    bool *mutable = xcalloc((size_t)acount(f->data_values) + 1, sizeof *mutable),
         *written = xcalloc((size_t)acount(f->data_values) + 1, sizeof *written);
    for (int pc = p->begin; distinct && pc <= p->end; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op == BF_SAVE) {
            local_mutable[i->target] = true;
        }
        if (i->op == BF_WRITE) {
            int bank = address_bank(p, i->right);
            if (bank >= 0) {
                mutable[bank] = true;
            }
        }
        if (i->op == BF_HOST_CALL) {
            const behavior_function_host_call *h = &f->host_calls[i->target];
            for (int j = 0; j < h->result_count; j++) {
                if (++p->work > ROW_PROOF_LIMIT * 8) {
                    distinct = false;
                    break;
                }
                mutable[h->result_first + j] = true;
            }
        }
    }
    int initialized = 0, zeroed = 0, advanced = 0, committed = 0, constructed = 0;
    bool safe = distinct;
    for (int pc = 0; pc < acount(f->instructions) && safe; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (pc == p->begin && (initialized != 1 || zeroed != 1)) {
            break;
        }
        bool loop = pc >= p->begin && pc <= p->end, after = pc > p->end;
        row_value a = i->left_slot ? slots[i->left] : (row_value){ 0 },
                  b = i->right_slot ? slots[i->right] : (row_value){ 0 },
                  out = { 0 };
        if (i->indirect) {
            safe = false;
            break;
        }
        if (behavior_function_observer(i->op) || i->op == BF_POINT ||
            i->op == BF_SOURCE_POINT || i->op == BF_SOURCE_WATCH) {
            continue;
        }
        if ((i->left_slot && p->definitions[i->left] >= pc) ||
            (i->right_slot && p->definitions[i->right] >= pc) ||
            (behavior_function_third_slot(i->op) && p->definitions[i->target] >= pc)) {
            safe = false;
            break;
        }
        switch (i->op) {
            case BF_CONSTANT:
                if (i->value.type == PIN_I32 &&
                    i->value.container == PIN_CONTAINER_SINGLE) {
                    out.kind = !i->value.i     ? ZERO
                             : i->value.i == 1 ? ONE
                                               : OTHER;
                } else if (
                    f->slot_kinds[i->output] == BEHAVIOR_ADDRESS && !i->value.u64
                ) {
                    out.kind = ZERO;
                }
                break;
            case BF_ARGUMENT:
            case BF_ARRAY:
            case BF_DELTA:
            case BF_OWNER: break;
            case BF_HOST_SLOT:
                safe = !i->target && f->data_values[i->left].kind == BEHAVIOR_VALUE;
                out = (row_value){ BANK, i->left };
                break;
            case BF_LOCAL:
                safe = !loop || !local_mutable[i->left] || local_written[i->left];
                out = locals[i->left];
                break;
            case BF_SAVE:
                safe = !sensitive(a) && !address(a);
                locals[i->target] = a;
                if (loop) {
                    local_written[i->target] = true;
                }
                break;
            case BF_LOAD:
            case BF_READ:
                if (a.kind == BANK) {
                    if (a.index == p->base) {
                        safe = false;
                        break;
                    }
                    safe = i->offset == offsetof(pin_value, d) ||
                           (after && !sensitive(banks[a.index]));
                    if (loop && mutable[a.index] && a.index != p->plan.local &&
                        a.index != p->counter) {
                        safe &= written[a.index];
                    }
                    out = i->offset == offsetof(pin_value, d) ? banks[a.index]
                                                              : (row_value){ 0 };
                    if (loop && a.index == p->plan.local) {
                        out.kind = PREFIX;
                    }
                    if (loop && a.index == p->counter) {
                        out.kind = COUNTER;
                    }
                } else {
                    safe = !sensitive(a) && !address(a);
                }
                break;
            case BF_WRITE:
                if (after && b.kind == CONTEXT) {
                    safe = !sensitive(a) && !address(a) &&
                           ((i->type == PIN_BOOL &&
                             i->offset == offsetof(behavior_function_context, exit_requested)) ||
                            (i->type == PIN_U64 &&
                             i->offset ==
                                 offsetof(behavior_function_context, returned_outcome)));
                    break;
                }
                if (after && b.kind == RESULT_HEADER) {
                    safe = !sensitive(a) && !address(a);
                    break;
                }
                if (b.kind != BANK || i->offset != offsetof(pin_value, d) ||
                    address(a)) {
                    safe = false;
                    break;
                }
                if (b.index == p->base) {
                    const behavior_function_instruction *constant = definition(p, i->left);
                    safe &= i->type == BF_POINTER_TYPE && constant &&
                            constant->op == BF_CONSTANT &&
                            constant->type == BF_POINTER_TYPE &&
                            constant->value.type == BF_POINTER_TYPE &&
                            constant->value.container == PIN_CONTAINER_SINGLE &&
                            f->slot_kinds[i->left] == BEHAVIOR_ADDRESS &&
                            !constant->value.u64;
                }
                if (sensitive(a)) {
                    safe = loop &&
                           ((a.kind == PREFIX && b.index == add->first && pc < p->add_pc) ||
                            (a.kind == ADDED && b.index == retain->first &&
                             pc > p->add_pc && pc < p->retain_pc) ||
                            (a.kind == RETAINED && b.index == p->plan.local &&
                             pc > p->retain_pc));
                    if (after && owners[b.index] >= 0) {
                        const behavior_function_host_call *h = &f->host_calls[owners[b.index]];
                        safe = h->first == b.index &&
                               h->result_first == h->first + 1 &&
                               intrinsic(h, &EXECUTION_snapshot_call);
                    }
                }
                if (b.index == p->plan.local) {
                    if (after) {
                        safe &= a.kind == ZERO;
                    } else if (!loop) {
                        safe &= a.kind == EMPTY_RETAIN && ++initialized == 1;
                    } else {
                        safe &= a.kind == RETAINED && ++committed == 1;
                    }
                }
                if (b.index == p->counter) {
                    if (after) {
                        safe = false;
                    } else if (!loop) {
                        safe &= a.kind == ZERO && i->type == PIN_I32 && ++zeroed == 1;
                    } else {
                        safe &= a.kind == NEXT_COUNTER && i->type == PIN_I32 &&
                                pc > p->retain_pc && committed == 1 &&
                                ++advanced == 1;
                    }
                }
                banks[b.index] = a;
                if (loop) {
                    written[b.index] = true;
                }
                break;
            case BF_HOST_CALL: {
                const behavior_function_host_call *h = &f->host_calls[i->target];
                safe = !i->right && i->left == BEHAVIOR_FUNCTION_HOST_DATA;
                for (int j = h->first; safe && loop && j < h->result_first; j++) {
                    safe &= ++p->work <= ROW_PROOF_LIMIT * 8;
                    if (mutable[j] && j != p->plan.local && j != p->counter) {
                        /* The constructor's private base starts declared null;
                         * prepare_data may supply its first fresh backing. It
                         * has no explicit reads and only direct null clears,
                         * proved across the whole function above and below. */
                        safe &= written[j] || (pc == p->construct_pc && j == p->base);
                    }
                }
                if (pc == p->add_pc) {
                    safe &= banks[h->first].kind == PREFIX &&
                            banks[h->first + 1].kind == ROW_RETAIN;
                    banks[h->result_first] = (row_value){ .kind = ADDED };
                } else if (intrinsic(h, &EXECUTION_snapshot_call) && h->result_first == h->first + 1) {
                    row_value input = banks[h->first];
                    safe &= same_shape(
                        f->data_values[h->first].initial,
                        f->data_values[h->result_first].initial
                    );
                    if (pc == p->retain_pc) {
                        safe &= input.kind == ADDED;
                        banks[h->result_first] = (row_value){ .kind = RETAINED };
                    } else {
                        safe &= !address(input) && (after || !sensitive(input));
                        banks[h->result_first] = (row_value){
                            .kind = input.kind == EMPTY_CONSTRUCT ? EMPTY_RETAIN
                                  : input.kind == ROW_CONSTRUCT || input.kind == ROW_RETAIN
                                      ? ROW_RETAIN
                                      : OTHER
                        };
                    }
                } else if (intrinsic(h, &EXECUTION_construct_call)) {
                    for (int k = h->first; safe && k < h->result_first; k++) {
                        safe &= ++p->work <= ROW_PROOF_LIMIT * 8;
                        safe &= !sensitive(banks[k]) && !address(banks[k]);
                    }
                    pin_value header = f->data_values[h->first].initial;
                    bool empty = h->result_first == h->first + 1 &&
                                 record_array(header) && !header.ptr &&
                                 (banks[h->first].kind == OTHER ||
                                  banks[h->first].kind == ZERO) &&
                                 same_shape(header, f->data_values[add->first].initial);
                    bool
                        row = pc == p->construct_pc &&
                              same_shape(header, f->data_values[add->first + 1].initial) &&
                              same_shape(header, f->data_values[h->result_first].initial);
                    safe &= (!loop && empty) || (row && ++constructed == 1);
                    banks[h->result_first] = (row_value){
                        .kind = empty ? EMPTY_CONSTRUCT : ROW_CONSTRUCT
                    };
                } else {
                    safe = false;
                }
                for (int j = 0; safe && loop && j < h->result_count; j++) {
                    safe &= ++p->work <= ROW_PROOF_LIMIT * 8;
                    written[h->result_first + j] = true;
                }
                break;
            }
            case BF_ADD:
                safe = !sensitive(a) && !sensitive(b) && !address(a) && !address(b);
                if (i->type == PIN_I32 && a.kind == COUNTER && b.kind == ONE &&
                    committed == 1) {
                    out.kind = NEXT_COUNTER;
                }
                break;
            case BF_BRANCH: safe = pc == p->branch; break;
            case BF_JUMP: safe = pc == p->branch + 1 || pc == p->end; break;
            case BF_CHECK:
            case BF_CALL_CHECK: break;
            case BF_ASSERT:
            case BF_REQUIRE: safe = !sensitive(a) && !address(a); break;
            case BF_CONTEXT:
                safe = after;
                if (i->target == 1) {
                    out.kind = CONTEXT;
                } else if (i->offset == offsetof(behavior_function_context, results)) {
                    out.kind = RESULTS;
                } else {
                    safe = false;
                }
                break;
            case BF_RETURN:
            case BF_END: safe = after; break;
            case BF_INDEX:
                if (after && a.kind == RESULTS && i->offset == sizeof(pin_value)) {
                    out.kind = RESULT_HEADER;
                    break;
                }
                safe = !sensitive(a) && !sensitive(b) && !address(a) && !address(b);
                break;
            default:
                safe = i->op >= BF_SUB && i->op <= BF_FMA &&
                       i->op != BF_RANDOM_RANGE && !sensitive(a) &&
                       !sensitive(b) && !address(a) && !address(b);
                if (behavior_function_third_slot(i->op)) {
                    safe &= !sensitive(slots[i->target]) &&
                            !address(slots[i->target]);
                }
                break;
        }
        if (i->op < BF_STORE) {
            slots[i->output] = out;
        }
        if ((i->op == BF_DIV || i->op == BF_MOD) &&
            behavior_scalar_integer(i->type) && i->target >= 0) {
            slots[i->target] = (row_value){ 0 };
        }
    }
    /* Stale host inputs/results and loop slots remain sensitive through the
     * suffix. Only a canonical deep snapshot breaks that alias provenance. */
    free(local_written);
    free(local_mutable);
    free(owners);
    free(written);
    free(mutable);
    free(locals);
    free(banks);
    free(slots);
    return safe && initialized == 1 && zeroed == 1 && committed == 1 &&
           advanced == 1 && constructed == 1;
}

size_t behavior_function_row_preparation_bytes(const behavior_function *f)
{
    size_t slots = (size_t)acount(f->slots), data = (size_t)acount(f->data_values),
           locals = (size_t)acount(f->locals);
    if (acount(f->instructions) > ROW_PROOF_LIMIT || slots > ROW_PROOF_LIMIT ||
        data > ROW_PROOF_LIMIT || locals > ROW_PROOF_LIMIT ||
        acount(f->host_calls) > ROW_PROOF_LIMIT) {
        return 0;
    }
    return sizeof(behavior_row_plan) +
           (slots + 1) * (sizeof(int) + sizeof(row_value)) +
           (data + 1) * (sizeof(int) + sizeof(row_value) + 2 * sizeof(bool)) +
           (locals + 1) * (sizeof(row_value) + 2 * sizeof(bool)) +
           ((size_t)acount(f->host_calls) + 1) * sizeof(int) +
           (2 * (size_t)acount(f->host_calls) + 16) * sizeof(behavior_append_plan) +
           sizeof(dyna_hdr) + (slots + data + locals + 3) * sizeof(unsigned);
}

void behavior_function_row_builder(behavior_function *f)
{
    int count = acount(f->instructions), slots = acount(f->slots);
    if (f->rows || f->fast || f->batchable || f->waits || acount(f->calls) ||
        !f->runtime_body || count > ROW_PROOF_LIMIT || slots > ROW_PROOF_LIMIT ||
        acount(f->data_values) > ROW_PROOF_LIMIT || acount(f->locals) > ROW_PROOF_LIMIT ||
        acount(f->host_calls) > ROW_PROOF_LIMIT) {
        return;
    }
    row_proof proof = { .f = f };
    proof.definitions = xmalloc(((size_t)slots + 1) * sizeof *proof.definitions);
    for (int j = 0; j <= slots; j++) {
        proof.definitions[j] = -1;
    }
    bool safe = locate(&proof) && prove(&proof);
    free(proof.definitions);
    if (!safe) {
        return;
    }
    int pcs[] = { proof.add_pc, proof.retain_pc };
    int hosts[] = { proof.plan.add, proof.plan.retain };
    for (int j = 0; j < 2; j++) {
        const behavior_function_instruction *i = &f->runtime_body[pcs[j]];
        if (!behavior_function_runtime_keeps(f, pcs[j]) ||
            i->op != BF_HOST_CALL || i->target != hosts[j] || i->right ||
            i->left != BEHAVIOR_FUNCTION_HOST_DATA) {
            return;
        }
    }
    f->rows = xmalloc(sizeof *f->rows);
    *f->rows = proof.plan;
    for (int j = 0; j < 2; j++) {
        int mode = j ? BF_HOST_RETAIN_ROWS : BF_HOST_BUILD_ROW;
        f->runtime_body[pcs[j]].right = mode;
        f->runtime_instructions[f->runtime_map[pcs[j]]].right = mode;
    }
}

bool behavior_function_rows_allowed(const behavior_function *f, const behavior_function_context *ctx)
{
    if (!scalar_allowed(f, ctx)) {
        return false;
    }
    if (!f->rows) {
        return true;
    }
    if (behavior_function_inspection_required(f, ctx)) {
        return false;
    }
    if (!ctx->data || ctx->data->function != f || !ctx->value_storage ||
        (ctx->activation && ctx->activation->pc) || !ctx->arguments ||
        f->rows->argument >= ctx->argument_count ||
        acount(ctx->data->calls) != acount(f->host_calls) ||
        acount(ctx->data->values) < acount(f->data_values)) {
        return false;
    }
    const pin_value *count = &ctx->arguments[f->rows->argument];
    if (count->type != PIN_I32 || count->container != PIN_CONTAINER_SINGLE ||
        count->record || count->i < 0 ||
        (size_t)count->i > ROW_BUFFER_LIMIT / sizeof(pin_value)) {
        return false;
    }
    const behavior_borrow_layout banks = {
        f->variable_count, f->result_count,   acount(f->data_values),
        acount(f->slots),  acount(f->locals), acount(f->frames)
    };
    if (!behavior_borrow_disjoint(ctx, count, sizeof *count, banks) ||
        behavior_borrow_overlap(count, sizeof *count, ctx->data, 1, sizeof *ctx->data) ||
        behavior_borrow_overlap(
            count,
            sizeof *count,
            ctx->data->calls,
            (size_t)acount(ctx->data->calls),
            sizeof *ctx->data->calls
        )) {
        return false;
    }
    if (ctx->activation && ctx->activation->vm) {
        const behavior_function_vm_storage *vm = ctx->activation->vm;
        if (behavior_borrow_overlap(count, sizeof *count, vm, 1, sizeof *vm) ||
            behavior_borrow_overlap(
                count,
                sizeof *count,
                vm->values,
                (size_t)acount(f->slots) + (size_t)acount(f->locals) + 1,
                sizeof *vm->values
            ) ||
            behavior_borrow_overlap(
                count,
                sizeof *count,
                vm->parents,
                (size_t)acount(f->frames),
                sizeof *vm->parents
            )) {
            return false;
        }
    }
    for (int j = 0; j < acount(f->host_calls); j++) {
        const node_host_call *expected = f->host_calls[j].call;
        const behavior_data_binding *actual = &ctx->data->calls[j];
        if (!expected->value || actual->value != expected->value || actual->invoke ||
            actual->context || actual->action || actual->service) {
            return false;
        }
    }
    for (int j = 0; j < acount(f->data_values); j++) {
        pin_value value = ctx->data->values[j], expected = f->data_values[j].initial;
        if ((value.record && !pin_record_valid(value.record)) ||
            !same_shape(value, expected)) {
            return false;
        }
    }
    const behavior_function_host_call *add = &f->host_calls[f->rows->add];
    const pin_record *record = f->data_values[add->first].initial.record;
    return add->capability_id == f->rows->id && add->capability_abi == f->rows->abi &&
           value_call(add->call, pin_array_add_call) && record &&
           record->id == f->rows->nominal && record->layout == f->rows->layout;
}
const char *behavior_function_build_row(behavior_function_context *ctx, int host)
{
    const behavior_function *f = ctx->data->function;
    const behavior_function_host_call *h = &f->host_calls[host];
    return pin_array_build_row(
               ctx->value_storage,
               &ctx->data->values[h->first],
               &ctx->data->values[h->first + 1],
               ctx->arguments[f->rows->argument].i,
               &ctx->data->values[h->result_first]
           )
             ? "arena allocation failed"
             : NULL;
}
const char *behavior_function_retain_rows(behavior_function_context *ctx, int host)
{
    const behavior_function *f = ctx->data->function;
    const behavior_function_host_call *h = &f->host_calls[host];
    pin_value value = ctx->data->values[h->first];
    const pin_array *array = value.ptr;
    value.record = array->record;
    value.type_name = array->type_name;
    ctx->data->values[h->result_first] = value;
    return NULL;
}

/* Scalar append does not depend on a loop's shape or trip count. The paired
 * assignment is structural; a separate whole-body proof protects every alias
 * of its private prefixes, including aliases carried around back edges. */
enum
{
    APPEND_ARRAY = 1,
    APPEND_ITEMS = 2,
    APPEND_BANK = 4,
    APPEND_CONTEXT = 8,
    APPEND_RESULTS = 16,
    APPEND_RESULT = 32,
    APPEND_WORK = ROW_PROOF_LIMIT * 32
};
static bool scalar_array(pin_value v)
{
    return v.type == PIN_I32 && v.container == PIN_CONTAINER_ARRAY &&
           !v.type_name && !v.record;
}
static bool scalar_add(const behavior_function *f, int host)
{
    const behavior_function_host_call *h = &f->host_calls[host];
    if (!value_call(h->call, pin_array_add_call) || !h->capability_id ||
        !h->capability_abi || h->result_first != h->first + 2 || h->result_count != 1) {
        return false;
    }
    pin_value item = f->data_values[h->first + 1].initial;
    return scalar_array(f->data_values[h->first].initial) &&
           scalar_array(f->data_values[h->result_first].initial) &&
           item.type == PIN_I32 && item.container == PIN_CONTAINER_SINGLE &&
           !item.type_name && !item.record;
}
static bool scalar_snapshot(const behavior_function *f, int host)
{
    const behavior_function_host_call *h = &f->host_calls[host];
    return intrinsic(h, &EXECUTION_snapshot_call) &&
           h->result_first == h->first + 1 &&
           scalar_array(f->data_values[h->first].initial) &&
           scalar_array(f->data_values[h->result_first].initial);
}
static bool scalar_reader(const behavior_function *f, int host)
{
    const behavior_function_host_call *h = &f->host_calls[host];
    bool get = value_call(h->call, pin_array_get_call) ||
               value_call(h->call, pin_array_get_owned_call);
    bool length = value_call(h->call, pin_array_length_call) ||
                  value_call(h->call, pin_array_length_owned_call);
    if ((!get && !length) || !h->capability_id || !h->capability_abi ||
        h->result_first != h->first + (get ? 2 : 1) || h->result_count != 1 ||
        !scalar_array(f->data_values[h->first].initial)) {
        return false;
    }
    for (int j = h->first + 1; j <= h->result_first; j++) {
        pin_value value = f->data_values[j].initial;
        /* Native declared defaults retain the builtin index name. Results
         * and generic array elements are lowered as unnamed scalar pins. */
        bool index_name = get && j == h->first + 1 && value.type_name &&
                          !strcmp(value.type_name, "int");
        if (value.type != PIN_I32 || value.container != PIN_CONTAINER_SINGLE ||
            (value.type_name && !index_name) || value.record) {
            return false;
        }
    }
    return true;
}
static bool zero_slot(const row_proof *p, int slot)
{
    const behavior_function_instruction *i = definition(p, slot);
    return i && i->op == BF_CONSTANT && !i->value.u64;
}
static bool scalar_observer(const behavior_function *f, int pc)
{
    behavior_function_op op = f->instructions[pc].op;
    return !behavior_function_runtime_keeps(f, pc) &&
           (behavior_function_observer(op) || op == BF_POINT ||
            op == BF_SOURCE_POINT || op == BF_SOURCE_WATCH);
}
static bool scalar_pair(row_proof *p, int pc, behavior_append_plan *out)
{
    const behavior_function *f = p->f;
    int add = f->instructions[pc].target;
    const behavior_function_host_call *h = &f->host_calls[add];
    int local = loaded_bank(p, input_slot(p, pc, h->first));
    if (local < 0 || !scalar_array(f->data_values[local].initial) ||
        f->data_values[local].kind != BEHAVIOR_VALUE) {
        return false;
    }
    int retain = -1, retain_pc = -1, commit = -1;
    for (int at = pc + 1; at < acount(f->instructions); at++) {
        if (++p->work > APPEND_WORK) {
            return false;
        }
        /* Authored assignment points and result watches sit between the add
         * and its retain. They cannot observe this uninspected runtime body;
         * scalar_allowed selects canonical copies whenever inspection runs. */
        if (scalar_observer(f, at)) {
            continue;
        }
        const behavior_function_instruction *i = &f->instructions[at];
        if (i->op == BF_HOST_CALL) {
            if (retain >= 0 || !scalar_snapshot(f, i->target)) {
                return false;
            }
            retain = i->target;
            retain_pc = at;
            const behavior_function_host_call *r = &f->host_calls[retain];
            int source = input_slot(p, at, r->first);
            if (loaded_bank(p, source) != h->result_first ||
                p->definitions[source] <= pc) {
                return false;
            }
            continue;
        }
        if (i->op == BF_WRITE && address_bank(p, i->right) == local) {
            if (retain < 0 || i->offset != offsetof(pin_value, d) ||
                loaded_bank(p, i->left) != f->host_calls[retain].result_first ||
                p->definitions[i->left] <= retain_pc) {
                return false;
            }
            commit = at;
            break;
        }
        if (i->op != BF_CONSTANT && i->op != BF_HOST_SLOT && i->op != BF_LOAD &&
            i->op != BF_WRITE) {
            return false;
        }
    }
    if (commit < 0) {
        return false;
    }
    const behavior_function_host_call *r = &f->host_calls[retain];
    for (int at = 0; at < acount(f->instructions); at++) {
        const behavior_function_instruction *i = &f->instructions[at];
        if ((i->op == BF_JUMP || i->op == BF_BRANCH) && i->target > pc &&
            i->target <= commit) {
            return false;
        }
        if (at <= pc || at > commit || i->op == BF_HOST_CALL ||
            scalar_observer(f, at)) {
            continue;
        }
        if (i->op == BF_CONSTANT && !i->value.u64) {
            continue;
        }
        if (i->op == BF_HOST_SLOT && !i->target &&
            (i->left == h->first || i->left == h->result_first || i->left == r->first ||
             i->left == r->result_first || i->left == local)) {
            continue;
        }
        if (i->op == BF_LOAD && i->offset == offsetof(pin_value, d) &&
            (address_bank(p, i->left) == h->result_first ||
             address_bank(p, i->left) == r->result_first)) {
            continue;
        }
        if (i->op == BF_WRITE && i->offset == offsetof(pin_value, d)) {
            int bank = address_bank(p, i->right);
            if (at == commit ||
                (bank == r->first && loaded_bank(p, i->left) == h->result_first) ||
                (zero_slot(p, i->left) &&
                 (bank == h->first || bank == h->result_first ||
                  bank == r->first || bank == r->result_first))) {
                continue;
            }
        }
        return false;
    }
    if (!behavior_function_runtime_keeps(f, pc) ||
        !behavior_function_runtime_keeps(f, retain_pc) ||
        f->runtime_body[pc].op != BF_HOST_CALL || f->runtime_body[pc].target != add ||
        f->runtime_body[pc].right || f->runtime_body[retain_pc].op != BF_HOST_CALL ||
        f->runtime_body[retain_pc].target != retain ||
        f->runtime_body[retain_pc].right) {
        return false;
    }
    *out = (behavior_append_plan){ .add = add,
                                   .retain = retain,
                                   .local = local,
                                   .add_pc = pc,
                                   .retain_pc = retain_pc };
    return true;
}
static bool append_header_field(const behavior_function_instruction *i)
{
    return (i->type == PIN_I32 && (i->offset == offsetof(pin_value, type) ||
                                   i->offset == offsetof(pin_value, container))) ||
           (i->type == BF_POINTER_TYPE &&
            (i->offset == offsetof(pin_value, ptr) ||
             i->offset == offsetof(pin_value, type_name) ||
             i->offset == offsetof(pin_value, record)));
}
static bool append_merge(unsigned *value, unsigned added, bool *changed)
{
    if ((*value | added) != *value) {
        *value |= added;
        *changed = true;
    }
    return true;
}
static bool append_instruction(
    row_proof *p,
    const behavior_function_instruction *i,
    unsigned *slots,
    unsigned *banks,
    unsigned *locals,
    bool *changed
)
{
    const behavior_function *f = p->f;
    if (i->indirect || ++p->work > APPEND_WORK) {
        return false;
    }
    unsigned a = i->left_slot ? slots[i->left] : 0,
             b = i->right_slot ? slots[i->right] : 0,
             c = behavior_function_third_slot(i->op) ? slots[i->target] : 0,
             out = 0;
    switch (i->op) {
        case BF_HOST_SLOT:
            out = i->target ? banks[i->left] : APPEND_BANK;
            break;
        case BF_CONTEXT:
            if (i->target == 1) {
                out = APPEND_CONTEXT;
            } else if (i->type == BF_POINTER_TYPE && i->offset == offsetof(behavior_function_context, results)) {
                out = APPEND_RESULTS;
            } else if (!((i->type == PIN_BOOL &&
                          i->offset == offsetof(behavior_function_context, exit_requested)) ||
                         (i->type == PIN_U64 &&
                          i->offset ==
                              offsetof(behavior_function_context, returned_outcome)))) {
                return false;
            }
            break;
        case BF_LOCAL: out = locals[i->left]; break;
        case BF_SAVE:
            if (a && a != APPEND_ARRAY) {
                return false;
            }
            return append_merge(&locals[i->target], a, changed);
        case BF_LOAD:
        case BF_READ:
            if (a == APPEND_BANK) {
                int bank = address_bank(p, i->left);
                if (bank < 0 || (i->offset != offsetof(pin_value, d) &&
                                 !append_header_field(i))) {
                    return false;
                }
                out = i->offset == offsetof(pin_value, ptr) ? banks[bank] : 0;
            } else if (a == APPEND_ARRAY) {
                if (i->type == BF_POINTER_TYPE &&
                    (i->offset == offsetof(pin_array, items) ||
                     i->offset == offsetof(pin_array, data))) {
                    out = APPEND_ITEMS;
                } else if (!((i->type == PIN_I32 &&
                              i->offset == offsetof(pin_array, count)) ||
                             (i->type == BF_POINTER_TYPE &&
                              i->offset == offsetof(pin_array, stride)))) {
                    return false;
                }
            } else if (a == APPEND_ITEMS) {
                if (i->type != PIN_I32 ||
                    (i->offset && i->offset != offsetof(pin_value, d))) {
                    return false;
                }
            } else if (a) {
                return false;
            }
            break;
        case BF_ADD:
        case BF_INDEX:
            if (a == APPEND_ITEMS && !b) {
                out = APPEND_ITEMS;
            } else if (i->op == BF_INDEX && a == APPEND_RESULTS && !b && i->offset == sizeof(pin_value)) {
                const behavior_function_instruction *index = definition(p, i->right);
                if (!index || index->op != BF_CONSTANT || index->type != PIN_I32 ||
                    index->value.i < 0 || index->value.i >= f->result_count) {
                    return false;
                }
                out = APPEND_RESULT;
            } else if (a || b) {
                return false;
            }
            break;
        case BF_SELECT:
            if (a || (b && c && b != c) ||
                ((b | c) && (b | c) != APPEND_ARRAY && (b | c) != APPEND_ITEMS)) {
                return false;
            }
            out = b | c;
            break;
        case BF_WRITE:
            if (b == APPEND_BANK) {
                int bank = address_bank(p, i->right);
                if (bank < 0 || i->offset != offsetof(pin_value, d) ||
                    (a && (a != APPEND_ARRAY || i->type != BF_POINTER_TYPE ||
                           !scalar_array(f->data_values[bank].initial)))) {
                    return false;
                }
                return append_merge(&banks[bank], a, changed);
            }
            if (a) {
                return false;
            }
            if (b == APPEND_CONTEXT) {
                return (i->type == PIN_BOOL &&
                        i->offset ==
                            offsetof(behavior_function_context, exit_requested)) ||
                       (i->type == PIN_U64 &&
                        i->offset ==
                            offsetof(behavior_function_context, returned_outcome));
            }
            return b == APPEND_RESULT && append_header_field(i);
        case BF_HOST_CALL: {
            const behavior_function_host_call *h = &f->host_calls[i->target];
            const node_host_call *call = h->call;
            if (!call->value || call->invoke || call->context || call->action ||
                call->service || call->missing_service || call->optional_service) {
                return false;
            }
            for (int j = h->first; j < h->result_first; j++) {
                if (++p->work > APPEND_WORK ||
                    (banks[j] &&
                     (banks[j] != APPEND_ARRAY || j != h->first ||
                      (!scalar_snapshot(f, i->target) && !scalar_add(f, i->target) &&
                       !scalar_reader(f, i->target))))) {
                    return false;
                }
            }
            break;
        }
        case BF_INVOKE: {
            const behavior_function_invocation *call = &f->calls[i->left];
            const behavior_function *callee = call->function;
            if (!callee || call->task || call->count != callee->argument_count) {
                return false;
            }
            for (int j = 0; j < call->count; j++) {
                if (++p->work > APPEND_WORK ||
                    (banks[call->first + j] &&
                     (banks[call->first + j] != APPEND_ARRAY ||
                      callee->argument_kinds[j] != BEHAVIOR_VALUE ||
                      !scalar_array(callee->argument_defaults[j])))) {
                    return false;
                }
            }
            /* frame_new either makes an owned snapshot or uses the callee's
             * independent readonly-collection proof. Returns snapshot again. */
            break;
        }
        case BF_EQ:
        case BF_NE:
            if (a || b) {
                if (!((a == APPEND_ARRAY && !b && zero_slot(p, i->right)) ||
                      (b == APPEND_ARRAY && !a && zero_slot(p, i->left)))) {
                    return false;
                }
            }
            break;
        case BF_ASSERT: return !a || a == APPEND_ARRAY;
        case BF_COMMAND:
        case BF_COMMAND_BEGIN:
        case BF_COMPONENT_WRITE:
        case BF_RETRIGGER:
        case BF_STATE_SELECT:
        case BF_STORE_VALUE:
        case BF_SUSPEND:
        case BF_DELAY:
        case BF_WATCH_BEGIN:
        case BF_WATCH_VALUE:
        case BF_TRACE: return false;
        default:
            if (a || b || c) {
                return false;
            }
            break;
    }
    return i->op >= BF_STORE || append_merge(&slots[i->output], out, changed);
}
void behavior_function_scalar_builder(behavior_function *f)
{
    int count = acount(f->instructions), nslots = acount(f->slots),
        nbanks = acount(f->data_values), nlocals = acount(f->locals);
    if (f->rows || f->appends || f->fast || f->batchable || f->waits ||
        !f->runtime_body || count > ROW_PROOF_LIMIT ||
        nslots > ROW_PROOF_LIMIT || nbanks > ROW_PROOF_LIMIT ||
        nlocals > ROW_PROOF_LIMIT || acount(f->host_calls) > ROW_PROOF_LIMIT) {
        return;
    }
    row_proof p = { .f = f };
    p.definitions = xmalloc(((size_t)nslots + 1) * sizeof *p.definitions);
    for (int j = 0; j <= nslots; j++) {
        p.definitions[j] = -1;
    }
    int *uses = xcalloc((size_t)acount(f->host_calls) + 1, sizeof *uses);
    bool safe = true;
    for (int pc = 0; pc < count && safe; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op < BF_STORE) {
            safe = p.definitions[i->output] < 0;
            p.definitions[i->output] = pc;
        }
        if (i->op == BF_HOST_CALL) {
            uses[i->target]++;
        }
    }
    behavior_append_plan *plans = NULL;
    for (int pc = 0; pc < count && safe; pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        behavior_append_plan plan;
        if (i->op == BF_HOST_CALL && scalar_add(f, i->target) && uses[i->target] == 1 &&
            scalar_pair(&p, pc, &plan) && uses[plan.retain] == 1) {
            apush(plans, plan);
        }
    }
    free(uses);
    unsigned *slots = xcalloc((size_t)nslots + 1, sizeof *slots),
             *banks = xcalloc((size_t)nbanks + 1, sizeof *banks),
             *locals = xcalloc((size_t)nlocals + 1, sizeof *locals);
    for (int j = 0; j < acount(plans); j++) {
        banks[f->host_calls[plans[j].add].result_first] = APPEND_ARRAY;
        banks[f->host_calls[plans[j].retain].result_first] = APPEND_ARRAY;
    }
    bool changed = true;
    while (safe && changed && acount(plans)) {
        changed = false;
        for (int pc = 0; pc < count && safe; pc++) {
            if (behavior_function_runtime_keeps(f, pc)) {
                safe = append_instruction(&p, &f->runtime_body[pc], slots, banks, locals, &changed);
            }
        }
    }
    free(slots);
    free(banks);
    free(locals);
    free(p.definitions);
    if (!safe) {
        afree(plans);
        return;
    }
    f->appends = plans;
    for (int j = 0; j < acount(plans); j++) {
        int pcs[] = { plans[j].add_pc, plans[j].retain_pc };
        for (int k = 0; k < 2; k++) {
            int mode = k ? BF_HOST_RETAIN_SCALAR : BF_HOST_APPEND_SCALAR;
            f->runtime_body[pcs[k]].right = mode;
            f->runtime_instructions[f->runtime_map[pcs[k]]].right = mode;
        }
    }
}
size_t behavior_function_append_storage_bytes(const behavior_function *f)
{
    return (size_t)acount(f->appends) * sizeof(pin_array_builder);
}
void behavior_function_append_reset(behavior_data_storage *data)
{
    if (!data || !data->function->appends) {
        return;
    }
    int first = acount(data->function->data_values);
    memset(data->buffers[first], 0, behavior_function_append_storage_bytes(data->function));
    memset(
        data->values + first,
        0,
        (size_t)acount(data->function->appends) * sizeof *data->values
    );
}
static bool scalar_allowed(const behavior_function *f, const behavior_function_context *ctx)
{
    if (!f->appends) {
        return true;
    }
    bool storage = ctx->data && ctx->data->function == f &&
                   acount(ctx->data->values) ==
                       acount(f->data_values) + acount(f->appends) &&
                   acount(ctx->data->buffers) == acount(f->data_values) + 1;
    bool safe = storage && ctx->value_storage && program_call_views_allowed(ctx) &&
                !behavior_function_inspection_required(f, ctx) &&
                acount(ctx->data->calls) == acount(f->host_calls);
    for (int j = 0; safe && j < acount(f->host_calls); j++) {
        const node_host_call *expected = f->host_calls[j].call;
        const behavior_data_binding *actual = &ctx->data->calls[j];
        safe = actual->value == expected->value && !actual->invoke &&
               !actual->context && !actual->action && !actual->service;
    }
    for (int j = 0; safe && j < acount(f->data_values); j++) {
        /* The proof permits private prefixes only in declared plain I32[]
         * banks. Unrelated native results may carry owned metadata that a
         * fresh invocation overwrites after its arena has been reset. */
        if (scalar_array(f->data_values[j].initial)) {
            safe = scalar_array(ctx->data->values[j]);
        }
    }
    bool owner_changed = false;
    if (storage) {
        const pin_array_builder *builders = ctx->data->buffers[acount(f->data_values)];
        for (int j = 0; j < acount(f->appends); j++) {
            owner_changed |= builders[j].owner && builders[j].owner != ctx->value_storage;
        }
    }
    if (storage &&
        (!safe || owner_changed || !ctx->activation || !ctx->activation->pc)) {
        behavior_function_append_reset(ctx->data);
    }
    return safe;
}
static int append_cache(const behavior_function *f, int plan)
{
    for (int j = 0; j < plan; j++) {
        if (f->appends[j].local == f->appends[plan].local) {
            return j;
        }
    }
    return plan;
}
const char *behavior_function_append_scalar(behavior_function_context *ctx, int host)
{
    const behavior_function *f = ctx->data->function;
    int index = 0;
    while (index < acount(f->appends) && f->appends[index].add != host) {
        index++;
    }
    assert(index < acount(f->appends));
    index = append_cache(f, index);
    int first = acount(f->data_values);
    pin_array_builder *builders = ctx->data->buffers[first];
    const behavior_function_host_call *h = &f->host_calls[host];
    const char *error = pin_array_build_i32(
        ctx->value_storage,
        &builders[index],
        ctx->data->values + h->first,
        ctx->data->values + h->result_first
    );
    if (!error) {
        ctx->data->values[first + index] = ctx->data->values[h->result_first];
    }
    return error;
}
const char *behavior_function_retain_scalar(behavior_function_context *ctx, int host)
{
    const behavior_function *f = ctx->data->function;
    const behavior_function_host_call *h = &f->host_calls[host];
    int first = acount(f->data_values);
    for (int j = 0; j < acount(f->appends); j++) {
        if (f->appends[j].retain == host && ctx->data->values[h->first].ptr &&
            ctx->data->values[h->first].ptr ==
                ctx->data->values[first + append_cache(f, j)].ptr) {
            ctx->data->values[h->result_first] = ctx->data->values[h->first];
            return NULL;
        }
    }
    return EXECUTION_snapshot_call.value(
        ctx->value_storage,
        ctx->data->values + h->first,
        1,
        ctx->data->values + h->result_first
    );
}
