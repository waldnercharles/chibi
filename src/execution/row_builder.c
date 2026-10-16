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
           (locals + 1) * (sizeof(row_value) + 2 * sizeof(bool));
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
