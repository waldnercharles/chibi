#include "execution/integer_report.h"

#include "execution/definition.h"
#include "execution/module_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>

static atomic_bool enabled;

void execution_integer_reports_enable(bool on)
{
    atomic_store(&enabled, on);
}
bool execution_integer_reports_enabled()
{
    return atomic_load(&enabled);
}
const char *execution_integer_report_text(execution_integer_report_kind kind)
{
    switch (kind) {
        case EXECUTION_INTEGER_OVERFLOW:
            return "Integer overflow wrapped around";
        case EXECUTION_INTEGER_ZERO_DIVISOR: return "Integer division by zero";
        case EXECUTION_INTEGER_CLAMP:
            return "Conversion clamped a value that does not fit";
        default: assert(false); return NULL;
    }
}

size_t behavior_integer_report_bytes(int sites)
{
    size_t width = 2 * EXECUTION_INTEGER_REPORT_KINDS * sizeof(atomic_uchar);
    return sites < 0 || (size_t)sites > SIZE_MAX / width ? SIZE_MAX
                                                         : (size_t)sites * width;
}
void behavior_function_report_integers(behavior_function *f)
{
    if (!f || f->integer_reports) {
        return;
    }
    for (int i = 0; i < acount(f->definitions); i++) {
        behavior_function_report_integers(f->definitions[i]);
    }
    if (!acount(f->sources)) {
        return;
    }
    /* The guarded fast body folds, shares and prunes operations across
     * sites, so a body with report sites always runs its canonical code. */
    assert(!f->artifact->code);
    behavior_function_free(f->fast);
    f->fast = NULL;
    f->integer_report_sites = acount(f->sources);
    size_t bytes = behavior_integer_report_bytes(f->integer_report_sites);
    assert(bytes != SIZE_MAX);
    f->integer_reports = xcalloc(1, bytes);
}
void execution_module_plan_report_integers(execution_module_plan *m)
{
    if (!execution_integer_reports_enabled()) {
        return;
    }
    for (int i = 0; i < m->query_count; i++) {
        behavior_function_report_integers(m->queries[i].predicate);
    }
    for (int i = 0; i < m->function_count; i++) {
        behavior_function_report_integers(m->functions[i].body);
        behavior_function_report_integers(m->functions[i].subscription);
    }
    for (int i = 0; i < m->binding_count; i++) {
        behavior_function_report_integers(m->bindings[i].body);
        behavior_function_report_integers(m->bindings[i].dependencies);
    }
}

bool behavior_integer_reportable(const behavior_function *f, const behavior_function_instruction *i)
{
    switch (i->op) {
        case BF_ADD:
        case BF_SUB:
        case BF_MUL:
        case BF_DIV:
        case BF_MOD:
        case BF_NEG:
        case BF_ABS:
            /* Address arithmetic belongs to the runtime, not to source values. */
            return behavior_scalar_integer(f->slots[i->left]) &&
                   (!f->slot_kinds || f->slot_kinds[i->left] == BEHAVIOR_VALUE);
        case BF_CAST:
            return i->target == BEHAVIOR_CAST_EXACT &&
                   behavior_scalar_integer(i->type) && f->slots[i->left] != i->type;
        default: return false;
    }
}

static const double cast_bounds[PIN_U64 + 1][2] = {
    [PIN_I8] = { -129, 0x1p7 },
    [PIN_U8] = { -1, 0x1p8 },
    [PIN_I16] = { -32769, 0x1p15 },
    [PIN_U16] = { -1, 0x1p16 },
    [PIN_I32] = { -2147483649.0, 0x1p31 },
    [PIN_U32] = { -1, 0x1p32 },
    /* The double just below -2^63: -2^63 itself fits. */
    [PIN_I64] = { -0x1.0000000000001p63, 0x1p63 },
    [PIN_U64] = { -1, 0x1p64 },
};
const double *behavior_integer_cast_bounds(pin_type type)
{
    assert(behavior_scalar_integer(type));
    return cast_bounds[type];
}

/* Whether the declared-width result differs from the exact one. */
static bool wraps(behavior_function_op op, pin_type type, uint64_t x, uint64_t y)
{
    uint64_t high = behavior_scalar_integer_high(type);
    if (!behavior_scalar_signed(type)) {
        switch (op) {
            case BF_ADD: return x > high - y;
            case BF_SUB: return x < y;
            default: return y && x > high / y;
        }
    }
    int64_t sx = (int64_t)x, sy = (int64_t)y,
            low = behavior_scalar_integer_low(type), max = (int64_t)high;
    switch (op) {
        case BF_ADD: return sy > 0 ? sx > max - sy : sx < low - sy;
        case BF_SUB: return sy < 0 ? sx > max + sy : sx < low + sy;
        default: break;
    }
    if (behavior_scalar_size(type) < sizeof(int64_t)) {
        int64_t product = sx * sy; /* Exact: both operands fit 32 bits. */
        return product < low || product > max;
    }
    if (sx == -1 || sy == -1) {
        return (sx == -1 ? sy : sx) == low;
    }
    return sy && (int64_t)((uint64_t)sx * (uint64_t)sy) / sy != sx;
}
int behavior_integer_event(const behavior_function_instruction *i, pin_value a, pin_value b)
{
    if (i->op == BF_CAST) {
        if (a.type == PIN_F32 || a.type == PIN_F64) {
            double x = a.type == PIN_F32 ? a.f : a.d;
            const double *bounds = behavior_integer_cast_bounds(i->type);
            return isnan(x) || x <= bounds[0] || x >= bounds[1] ? EXECUTION_INTEGER_CLAMP
                                                                : -1;
        }
        pin_value out;
        return behavior_scalar_cast(a, i->type, BEHAVIOR_CAST_EXACT, &out)
                 ? -1
                 : EXECUTION_INTEGER_CLAMP;
    }
    pin_type type = a.type;
    assert(behavior_scalar_integer(type));
    bool sign = behavior_scalar_signed(type);
    uint64_t x = behavior_scalar_bits(a);
    int64_t low = behavior_scalar_integer_low(type);
    switch (i->op) {
        case BF_NEG:
            return (sign ? (int64_t)x == low : x != 0) ? EXECUTION_INTEGER_OVERFLOW
                                                       : -1;
        case BF_ABS:
            return sign && (int64_t)x == low ? EXECUTION_INTEGER_OVERFLOW : -1;
        default: break;
    }
    uint64_t y = behavior_scalar_bits(b);
    switch (i->op) {
        case BF_ADD:
        case BF_SUB:
        case BF_MUL:
            return wraps(i->op, type, x, y) ? EXECUTION_INTEGER_OVERFLOW : -1;
        case BF_DIV:
        case BF_MOD:
            if (!y) {
                return EXECUTION_INTEGER_ZERO_DIVISOR;
            }
            /* Only a quotient overflows: the minimum % -1 is exactly 0. */
            return sign && (int64_t)x == low && (int64_t)y == -1 &&
                           (i->op == BF_DIV || i->target >= 0)
                     ? EXECUTION_INTEGER_OVERFLOW
                     : -1;
        default: assert(false); return -1;
    }
}
atomic_uchar *behavior_integer_site(
    const behavior_function *f,
    const behavior_function_instruction *i,
    int kind
)
{
    if (!f->integer_reports || i->source < 0 || i->source >= f->integer_report_sites) {
        return NULL;
    }
    return &f->integer_reports[i->source * EXECUTION_INTEGER_REPORT_KINDS + kind];
}
void behavior_integer_report(
    const behavior_function *f,
    const behavior_function_instruction *i,
    pin_value a,
    pin_value b
)
{
    if (!behavior_integer_reportable(f, i)) {
        return;
    }
    int kind = behavior_integer_event(i, a, b);
    atomic_uchar *site = kind < 0 ? NULL : behavior_integer_site(f, i, kind);
    if (site) {
        atomic_store_explicit(site, 1, memory_order_relaxed);
    }
}

static uint64_t node(const behavior_function *f, int source)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (f->instructions[pc].source == source &&
            behavior_integer_reportable(f, &f->instructions[pc])) {
            return f->instructions[pc].node_id;
        }
    }
    return 0;
}
static void take_sites(const execution_module *m, const behavior_function *f, execution_integer_report **reports)
{
    int count = f->integer_report_sites * EXECUTION_INTEGER_REPORT_KINDS;
    for (int site = 0; f->integer_reports && site < count; site++) {
        atomic_uchar *delivered = &f->integer_reports[count + site];
        if (!atomic_load_explicit(&f->integer_reports[site], memory_order_relaxed) ||
            atomic_load_explicit(delivered, memory_order_relaxed)) {
            continue;
        }
        atomic_store_explicit(delivered, 1, memory_order_relaxed);
        int index = site / EXECUTION_INTEGER_REPORT_KINDS;
        const behavior_function_source *source = &f->sources[index];
        apush(
            *reports,
            ((execution_integer_report){ .kind = site % EXECUTION_INTEGER_REPORT_KINDS,
                                         .module_id = m->plan.id,
                                         .node_id = node(f, index),
                                         .start = source->start,
                                         .end = source->end,
                                         .line = source->line,
                                         .column = source->column,
                                         .asset_name = source->asset_name })
        );
    }
}
static void reset_sites(const behavior_function *f)
{
    int count = f->integer_report_sites * EXECUTION_INTEGER_REPORT_KINDS;
    for (int site = 0; f->integer_reports && site < count; site++) {
        atomic_store_explicit(&f->integer_reports[site], 0, memory_order_relaxed);
        atomic_store_explicit(&f->integer_reports[count + site], 0, memory_order_relaxed);
    }
}
static void visit(const execution_module *m, const behavior_function *f, execution_integer_report **reports)
{
    if (!f) {
        return;
    }
    for (int i = 0; i < acount(f->definitions); i++) {
        visit(m, f->definitions[i], reports);
    }
    if (reports) {
        take_sites(m, f, reports);
        return;
    }
    reset_sites(f);
}
/* Takes when reports is not NULL, otherwise resets. */
static void visit_module(const execution_module *m, execution_integer_report **reports)
{
    const execution_module_plan *p = &m->plan;
    for (int i = 0; i < p->import_count; i++) {
        visit_module(p->imports[i], reports);
    }
    for (int i = 0; i < p->query_count; i++) {
        visit(m, p->queries[i].predicate, reports);
    }
    for (int i = 0; i < p->function_count; i++) {
        visit(m, p->functions[i].body, reports);
        visit(m, p->functions[i].subscription, reports);
    }
    for (int i = 0; i < p->binding_count; i++) {
        visit(m, p->bindings[i].body, reports);
        visit(m, p->bindings[i].dependencies, reports);
    }
}
void execution_module_take_integer_reports(const execution_module *m, execution_integer_report **reports)
{
    assert(m && reports);
    visit_module(m, reports);
}
void execution_module_reset_integer_reports(const execution_module *m)
{
    assert(m);
    visit_module(m, NULL);
}
