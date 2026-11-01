#include "execution/preparation_budget.h"

#include "execution/integer_report.h"
#include "execution/module_internal.h"
#include "support/array.h"

#include <limits.h>
#include <string.h>

static size_t sum(size_t a, size_t b)
{
    return b > SIZE_MAX - a ? SIZE_MAX : a + b;
}
static size_t product(size_t n, size_t width)
{
    return width && n > SIZE_MAX / width ? SIZE_MAX : n * width;
}
/* apush doubles capacity from a minimum of 16. Count each resulting vector,
 * including its header, without assuming a ratio to serialized bytes. */
static size_t bank(size_t n, size_t width)
{
    if (!n) {
        return 0;
    }
    if (n > INT_MAX / 2) {
        return SIZE_MAX;
    }
    return sum(sizeof(dyna_hdr), product(n < 16 ? 16 : n * 2, width));
}
/* A vector sized once to at most `n` elements keeps that capacity, from the
 * same minimum of 16. */
static size_t sized(size_t n, size_t width)
{
    if (n > INT_MAX) {
        return SIZE_MAX;
    }
    return sum(sizeof(dyna_hdr), product(n < 16 ? 16 : n, width));
}
behavior_function_preparation behavior_function_preparation_bytes(const behavior_function *f)
{
    const behavior_function_preparation unsupported = { SIZE_MAX, SIZE_MAX };
    size_t count = acount(f->instructions), slots = acount(f->slots);
    size_t locals = acount(f->locals), data = acount(f->data_values);
    size_t values = sum(slots, locals), pauses = 0;
    bool calls = false;
    for (int i = 0; i < acount(f->instructions); i++) {
        const behavior_function_instruction *op = &f->instructions[i];
        pauses += op->op == BF_SUSPEND || op->op == BF_DELAY || op->op == BF_POINT ||
                  op->op == BF_SOURCE_POINT || op->op == BF_INVOKE;
        calls |= op->op == BF_INVOKE;
    }
    if (values > INT_MAX) {
        return unsupported;
    }
    /* Live value and root sets are counted after liveness and allocated
     * exactly from the bounded owned arena, so only the table is reserved. */
    size_t retained = bank(pauses, sizeof(behavior_function_suspension));
    /* Publication allocates definition-owned report banks before compilation.
     * The host chooses reports once at startup, before this preflight. */
    if (execution_integer_reports_enabled() && !f->integer_reports) {
        retained = sum(retained, behavior_integer_report_bytes(acount(f->sources)));
    }
    /* Runtime read reuse owns a canonical-PC body, a compact body and map,
     * each sized once from the instruction count. */
    retained = sum(retained, sized(count, sizeof(behavior_function_instruction)));
    retained = sum(retained, sized(count, sizeof(behavior_function_instruction)));
    retained = sum(retained, product(count + 1, sizeof(int)));
    /* Verification: provenance headers; defined_operands matrix+row,
     * reached/queued/queue; checked_control incoming+queue. */
    size_t words = slots / 64 + 1;
    size_t defined = product(count + 1, product(words, sizeof(uint64_t)));
    size_t scratch = product(count, 2 * sizeof(bool) + 3 * sizeof(int));
    scratch = sum(scratch, product(slots, sizeof(int)));
    if (acount(f->value_roots)) {
        scratch = sum(scratch, product(values + 1, sizeof(bool)));
    }
    /* Defined-operands verification frees its matrix+row before suspension
     * liveness begins. Reserve their larger phase, preserving overflow. */
    words = (values + 63) / 64 + 1;
    size_t live = product(count + 1, product(words, sizeof(uint64_t)));
    scratch = sum(scratch, defined > live ? defined : live);
    /* runtime_code + reuse_reads simultaneously own live/keep/saved,
     * parents/slots/banks/unused/pinned/scratch/writes. */
    scratch = sum(scratch, product(count, sizeof(int) + 2 * sizeof(bool)));
    scratch = sum(scratch, product(slots + 1, 2 * sizeof(int) + 3 * sizeof(bool)));
    scratch = sum(scratch, product(data + 1, sizeof(int) + sizeof(bool)));
    /* Numeric interval facts (two doubles, three ints, a bool and padding),
     * definition versions and control-entry marks. */
    scratch = sum(scratch, product(slots + 1, 3 * sizeof(max_align_t) + sizeof(int)));
    scratch = sum(scratch, product(count + 1, sizeof(bool)));
    /* Read-only array borrowing keeps one kind/bank fact per scalar slot and
     * a logical-array marker per local, and its callees' facts while it
     * walks them. */
    scratch = sum(scratch, behavior_function_borrow_bytes(f));
    if (calls) {
        scratch = sum(scratch, BEHAVIOR_BORROW_NESTED_BYTES);
    }
    /* Union scalar replacement keeps facts for slots, banks, locals and PCs,
     * plus one candidate per constructor and its immutable scalar payloads. */
    if (acount(f->host_calls)) {
        scratch = sum(scratch, behavior_function_scalarize_scratch(f));
        size_t fact = sizeof(pin_value) + 3 * sizeof(int) + sizeof(max_align_t);
        size_t object = sizeof(uint64_t) + sizeof(void *) + 2 * sizeof(int) +
                        sizeof(max_align_t);
        scratch = sum(scratch, product(count, fact + 2 * sizeof(int) + sizeof(bool)));
        scratch = sum(scratch, product(slots + 1, fact + 2 * sizeof(int) + 2 * sizeof(bool)));
        scratch = sum(scratch, product(data + 1, fact + sizeof(int) + 2 * sizeof(bool)));
        scratch = sum(scratch, product(locals + 1, fact + sizeof(int) + sizeof(bool)));
        scratch = sum(scratch, bank(count, object));
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            const behavior_function_instruction *i = &f->instructions[pc];
            if (i->op == BF_HOST_CALL) {
                if (i->target < 0 || i->target >= acount(f->host_calls)) {
                    return unsupported;
                }
                const behavior_function_host_call *h = &f->host_calls[i->target];
                if (h->first < 0 || h->result_first < h->first ||
                    h->result_first > acount(f->data_values)) {
                    return unsupported;
                }
                scratch = sum(scratch, bank((size_t)(h->result_first - h->first), fact));
            }
        }
    }
    /* parallel proof origin is two bools + two ints, padded no larger than
     * two ints and two max_align_t units. Count its actual target bool bank. */
    scratch = sum(scratch, product(slots + 1, 2 * sizeof(int) + 2 * sizeof(max_align_t)));
    scratch = sum(scratch, product(data + 1, sizeof(bool)));
    /* Host outcome duplicate checks allocate one sorted ID buffer. */
    for (int i = 0; i < acount(f->host_calls); i++) {
        if (f->host_calls[i].outcome_count < 0) {
            return unsupported;
        }
        scratch = sum(scratch, product((size_t)f->host_calls[i].outcome_count, sizeof(uint64_t)));
    }
    return (behavior_function_preparation){ retained, scratch };
}

static void *copy_bytes(arena *storage, const void *source, size_t bytes, size_t budget)
{
    void *copy = arena_alloc_bounded(storage, bytes, budget);
    if (copy && bytes) {
        memcpy(copy, source, bytes);
    }
    return copy;
}
static bool string_copy(arena *storage, const char **text, size_t budget)
{
    if (!*text) {
        return true;
    }
    char *copy = copy_bytes(storage, *text, strlen(*text) + 1, budget);
    if (!copy) {
        return false;
    }
    *text = copy;
    return true;
}
bool behavior_function_own_decoded(behavior_function *f, size_t budget)
{
    arena *storage = &f->owned;
    for (int i = 0; i < acount(f->host_calls); i++) {
        behavior_function_host_call *host = &f->host_calls[i];
        node_host_call *call = copy_bytes(storage, host->call, sizeof *call, budget);
        if (!call || !string_copy(storage, &call->service, budget) ||
            !string_copy(storage, &call->missing_service, budget)) {
            return false;
        }
        host->call = call;
        const node_access *source = host->access;
        if (!source) {
            continue;
        }
        if (source->read_count < 0 || source->write_count < 0 ||
            (source->read_count && !source->reads) ||
            (source->write_count && !source->writes)) {
            return false;
        }
        node_access *access = copy_bytes(storage, source, sizeof *access, budget);
        if (!access) {
            return false;
        }
        host->access = access;
        access->reads = copy_bytes(
            storage,
            source->reads,
            product((size_t)source->read_count, sizeof(uint64_t)),
            budget
        );
        access->writes = copy_bytes(
            storage,
            source->writes,
            product((size_t)source->write_count, sizeof(uint64_t)),
            budget
        );
        if (!access->reads || !access->writes) {
            return false;
        }
        if (!source->read_refs) {
            continue;
        }
        size_t count = 0;
        while (source->read_refs[count]) {
            count++;
        }
        const uint64_t **refs = arena_alloc_bounded(storage, product(count + 1, sizeof *refs), budget);
        uint64_t *ids = arena_alloc_bounded(storage, product(count, sizeof *ids), budget);
        if (!refs || !ids) {
            return false;
        }
        for (size_t j = 0; j < count; j++) {
            ids[j] = *source->read_refs[j];
            refs[j] = &ids[j];
        }
        refs[count] = NULL;
        access->read_refs = refs;
    }
    for (int i = 0; i < acount(f->commands); i++) {
        behavior_function_command *host = &f->commands[i];
        node_command *command = copy_bytes(storage, host->command, sizeof *command, budget);
        if (!command || !string_copy(storage, &command->service, budget) ||
            !string_copy(storage, &command->missing_service, budget) ||
            !string_copy(storage, &command->missing_reference, budget) ||
            !string_copy(storage, &command->failure_message, budget)) {
            return false;
        }
        host->command = command;
    }
    /* Every other pointer came from the decoder's bounded owned arena.
     * Runtime banks copy these headers and borrow that same immutable payload. */
    behavior_function_note_derived(f);
    f->frozen = true;
    return true;
}

#define PREPARATION_CLOSURE_DEPTH 64
#define PREPARATION_CLOSURE_FUNCTIONS 65536
static bool owned_functions(const behavior_function *f, unsigned depth, size_t *count)
{
    if (!f) {
        return true;
    }
    if (depth == PREPARATION_CLOSURE_DEPTH || *count == PREPARATION_CLOSURE_FUNCTIONS) {
        return false;
    }
    (*count)++;
    for (int i = 0; i < acount(f->definitions); i++) {
        if (!owned_functions(f->definitions[i], depth + 1, count)) {
            return false;
        }
    }
    return !f->fast || owned_functions(f->fast, depth + 1, count);
}
static bool module_functions(const execution_module_plan *p, unsigned depth, size_t *count)
{
    if (depth == PREPARATION_CLOSURE_DEPTH || p->function_count < 0 ||
        p->binding_count < 0 || p->import_count < 0 || p->query_count < 0 ||
        p->function_count > acount(p->functions) ||
        p->binding_count > acount(p->bindings) ||
        p->import_count > acount(p->imports) || p->query_count > acount(p->queries)) {
        return false;
    }
    if (*count == PREPARATION_CLOSURE_FUNCTIONS) {
        return false;
    }
    (*count)++;
    for (int i = 0; i < p->function_count; i++) {
        const execution_module_function *f = &p->functions[i];
        if (!owned_functions(f->body, 0, count) ||
            !owned_functions(f->subscription, 0, count)) {
            return false;
        }
    }
    for (int i = 0; i < p->binding_count; i++) {
        if (!owned_functions(p->bindings[i].body, 0, count) ||
            !owned_functions(p->bindings[i].dependencies, 0, count)) {
            return false;
        }
    }
    for (int i = 0; i < p->query_count; i++) {
        if (!owned_functions(p->queries[i].predicate, 0, count)) {
            return false;
        }
    }
    /* Repeated import DAG paths deliberately overcount. Bound traversal before
     * any scratch allocation; no visited table is needed to establish safety. */
    for (int i = 0; i < p->import_count; i++) {
        if (!module_functions(&p->imports[i]->plan, depth + 1, count)) {
            return false;
        }
    }
    return true;
}
size_t execution_module_preparation_bytes(const execution_module_plan *p)
{
    size_t count = 0;
    if (p->query_count < 0 || !module_functions(p, 0, &count)) {
        return SIZE_MAX;
    }
    /* Service, predicate purity and predicate-effect walks each own a seen
     * vector bounded by the retained closure. Query validation owns one byte
     * per node. Sum phases conservatively rather than relying on their order. */
    return sum(
        product(3, bank(count, sizeof(const behavior_function *))),
        (size_t)p->query_count + 1
    );
}
