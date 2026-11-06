#include "execution/program_inspection.h"

#include "execution/definition.h"
#include "execution/inspection.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define WATCH_LIMIT 512
#define SNAPSHOT_LIMIT (4u * 1024u * 1024u)
#define FRAME_LIMIT 128
#define BREAKPOINT_LIMIT 256
#define OWNER_LIMIT 64

typedef struct
{
    execution_debug_watch view;
    execution_module *module;
    arena storage;
    size_t bytes;
    uint64_t touched;
} watch_row;
typedef struct
{
    uint64_t module, function, occurrence;
} breakpoint;
typedef struct
{
    uint64_t owner, touched;
    execution_debug_cursor cursor;
    execution_debug_frame *frames;
} owner_row;
struct execution_inspector
{
    unsigned references;
    execution_program *program;
    execution_activation *paused;
    execution_debug_cursor cursor;
    execution_debug_frame *frames;
    watch_row *watches;
    uint64_t *owners;
    breakpoint *breakpoints;
    execution_debug_activity *activity;
    owner_row *snapshots;
    uint64_t selected_owner;
    uint64_t activity_dropped;
    uint64_t serial, next_run, generation;
    size_t bytes;
    uint64_t requested_owner;
    uint64_t recording_owner;
    bool recording;
    bool requested, stepping;
    execution_debug_control control;
    int target_depth;
};
static uint64_t generations;
static void row_free(watch_row *row)
{
    arena_free(&row->storage);
    execution_module_free(row->module);
}
execution_inspector *execution_inspector_retain(execution_inspector *i)
{
    assert(i && i->references < UINT_MAX);
    i->references++;
    return i;
}
void execution_inspector_free(execution_inspector *i)
{
    if (!i) {
        return;
    }
    assert(i->references);
    if (--i->references) {
        return;
    }
    for (int k = 0; k < acount(i->watches); k++) {
        row_free(&i->watches[k]);
    }
    afree(i->watches);
    afree(i->owners);
    afree(i->frames);
    afree(i->breakpoints);
    afree(i->activity);
    for (int k = 0; k < acount(i->snapshots); k++) {
        afree(i->snapshots[k].frames);
    }
    afree(i->snapshots);
    free(i);
}
execution_inspector *execution_program_inspector(execution_program *p)
{
    assert(p && !p->running);
    if (!p->inspector) {
        /* Preparation has already chosen retained activations or row IDs. */
        assert(!p->system_prepared);
        p->inspector = xcalloc(1, sizeof *p->inspector);
        p->inspector->references = 1;
        p->inspector->program = p;
        assert(generations < UINT64_MAX);
        p->inspector->generation = ++generations;
    }
    p->inspector->recording = true;
    p->inspector->recording_owner = 0;
    return execution_inspector_retain(p->inspector);
}
execution_inspector *execution_program_inspection(const execution_program *p)
{
    assert(p);
    return p->inspector;
}
void execution_inspector_recording(execution_inspector *i, bool enabled, uint64_t owner)
{
    assert(i);
    i->recording = enabled;
    i->recording_owner = owner;
}
bool execution_inspector_records(const execution_inspector *i, uint64_t owner)
{
    assert(i);
    return i->recording && (!i->recording_owner || i->recording_owner == owner);
}
bool execution_program_paused(const execution_program *p)
{
    assert(p);
    return p->inspector && p->inspector->paused;
}
int execution_program_owner_count(const execution_program *p)
{
    assert(p);
    return acount(p->instances);
}
uint64_t execution_program_owner_at(const execution_program *p, int n)
{
    assert(p && n >= 0 && n < acount(p->instances));
    return p->instances[n]->owner;
}
execution_debug_cursor execution_inspector_cursor(const execution_inspector *i)
{
    assert(i);
    return i->cursor;
}
static owner_row *owner_snapshot(execution_inspector *i, uint64_t owner, bool create)
{
    for (int n = 0; n < acount(i->snapshots); n++) {
        if (i->snapshots[n].owner == owner) {
            return &i->snapshots[n];
        }
    }
    if (!create) {
        return NULL;
    }
    int n = acount(i->snapshots);
    if (n == OWNER_LIMIT) {
        n = -1;
        for (int k = 0; k < OWNER_LIMIT; k++) {
            if (i->snapshots[k].owner == i->selected_owner) {
                continue;
            }
            if (n < 0 || i->snapshots[k].touched < i->snapshots[n].touched) {
                n = k;
            }
        }
        assert(n >= 0);
        afree(i->snapshots[n].frames);
        i->snapshots[n] = (owner_row){ 0 };
    } else {
        apush(i->snapshots, ((owner_row){ 0 }));
    }
    i->snapshots[n].owner = owner;
    return &i->snapshots[n];
}
bool execution_inspector_select_owner(execution_inspector *i, uint64_t owner)
{
    assert(i);
    if (!owner) {
        i->selected_owner = 0;
        return true;
    }
    owner_row *row = owner_snapshot(i, owner, false);
    if (!row && (!i->program || !execution_program_instance(i->program, owner))) {
        return false;
    }
    owner_snapshot(i, owner, true);
    i->selected_owner = owner;
    return true;
}
bool execution_inspector_owner_cursor(const execution_inspector *i, uint64_t owner, execution_debug_cursor *cursor)
{
    assert(i && cursor);
    for (int n = 0; n < acount(i->snapshots); n++) {
        if (i->snapshots[n].owner != owner || !i->snapshots[n].cursor.run) {
            continue;
        }
        *cursor = i->snapshots[n].cursor;
        return true;
    }
    return false;
}
int execution_inspector_owner_frame_count(const execution_inspector *i, uint64_t owner)
{
    assert(i);
    for (int n = 0; n < acount(i->snapshots); n++) {
        if (i->snapshots[n].owner == owner) {
            return acount(i->snapshots[n].frames);
        }
    }
    return 0;
}
execution_debug_frame execution_inspector_owner_frame_at(
    const execution_inspector *i,
    uint64_t owner,
    int index
)
{
    assert(i && index >= 0);
    for (int n = 0; n < acount(i->snapshots); n++) {
        if (i->snapshots[n].owner != owner) {
            continue;
        }
        assert(index < acount(i->snapshots[n].frames));
        return i->snapshots[n].frames[index];
    }
    assert(false);
    return (execution_debug_frame){ 0 };
}
int execution_inspector_watch_count(const execution_inspector *i)
{
    assert(i);
    return acount(i->watches);
}
const execution_debug_watch *execution_inspector_watch_at(const execution_inspector *i, int n)
{
    assert(i && n >= 0 && n < acount(i->watches));
    return &i->watches[n].view;
}
int execution_inspector_frame_count(const execution_inspector *i)
{
    assert(i);
    return acount(i->frames);
}
execution_debug_frame execution_inspector_frame_at(const execution_inspector *i, int n)
{
    assert(i && n >= 0 && n < acount(i->frames));
    return i->frames[n];
}
int execution_inspector_activity_count(const execution_inspector *i)
{
    assert(i);
    return acount(i->activity);
}
execution_debug_activity execution_inspector_activity_at(const execution_inspector *i, int n)
{
    assert(i && n >= 0 && n < acount(i->activity));
    return i->activity[n];
}
uint64_t execution_inspector_activity_dropped(const execution_inspector *i)
{
    assert(i);
    return i->activity_dropped;
}
int execution_inspector_owner_count(const execution_inspector *i)
{
    assert(i);
    return i->program ? acount(i->program->instances) : acount(i->owners);
}
uint64_t execution_inspector_owner_at(const execution_inspector *i, int n)
{
    assert(i && n >= 0 && n < execution_inspector_owner_count(i));
    return i->program ? i->program->instances[n]->owner : i->owners[n];
}
static execution_activation *root(execution_activation *a)
{
    while (a->continuation_owner) {
        a = a->continuation_owner;
    }
    return a;
}
static int depth(behavior_function_context *ctx)
{
    int n = 0;
    for (; ctx->parent && n < FRAME_LIMIT; ctx = ctx->parent) {
        n++;
    }
    return n;
}
static int activation_depth(execution_activation *a, behavior_function_context *ctx)
{
    int d = depth(ctx);
    for (int n = 0; a->continuation_context && n < FRAME_LIMIT; n++) {
        d += depth(a->continuation_context) + 1;
        a = a->continuation_owner;
    }
    return d;
}
static execution_module *function_module(execution_program *p, const behavior_function *body, uint64_t *id)
{
    execution_module *m = p->module;
    for (int k = 0; k < m->plan.function_count; k++) {
        execution_module_function *f = &m->plan.functions[k];
        if (f->body == body || f->subscription == body) {
            *id = f->id;
            return m;
        }
    }
    for (int k = 0; k < acount(p->imports); k++) {
        execution_module *found = function_module(p->imports[k], body, id);
        if (found) {
            return found;
        }
    }
    return NULL;
}
static void frames(
    execution_inspector *i,
    execution_activation *a,
    behavior_function_context *ctx,
    behavior_source_point point
)
{
    asetlen(i->frames, 0);
    for (int n = 0; ctx && n < FRAME_LIMIT; n++) {
        uint64_t id = 0;
        execution_module *m = function_module(i->program, ctx->source_function, &id);
        apush(
            i->frames,
            ((execution_debug_frame){ .module = m ? m->plan.id : 0,
                                      .function = id,
                                      .depth = activation_depth(a, ctx),
                                      .point = point })
        );
        if (ctx->parent && ctx->parent->activation) {
            int pc = ctx->parent->activation->pc - 1;
            const behavior_function *f = ctx->parent->source_function;
            if (f && pc >= 0 && pc < acount(f->instructions)) {
                const behavior_function_source
                    *s = &f->sources[f->instructions[pc].source];
                point = (behavior_source_point){ .start = s->start,
                                                 .end = s->end,
                                                 .line = s->line,
                                                 .column = s->column };
            }
        }
        ctx = ctx->parent;
        if (!ctx && a->continuation_context) {
            ctx = a->continuation_context;
            a = a->continuation_owner;
        }
    }
}
static void activity(
    execution_inspector *i,
    execution_activation *a,
    uint64_t module,
    uint64_t function,
    uint64_t occurrence,
    double time
)
{
    int row = -1;
    for (int n = 0; n < acount(i->activity); n++) {
        execution_debug_activity *v = &i->activity[n];
        if (v->owner == a->instance->owner && v->module == module &&
            !memcmp(v->query_variables, a->instance->query_variables, sizeof v->query_variables) &&
            v->function == function && v->occurrence == occurrence &&
            v->from_occurrence == a->debug_previous && v->from_module == a->debug_module &&
            v->from_function == a->debug_function) {
            row = n;
            break;
        }
    }
    if (row < 0) {
        if (acount(i->activity) == WATCH_LIMIT) {
            row = 0;
            for (int n = 1; n < WATCH_LIMIT; n++) {
                if (i->activity[n].time < i->activity[row].time) {
                    row = n;
                }
            }
            uint64_t count = i->activity[row].count;
            i->activity_dropped = count > UINT64_MAX - i->activity_dropped
                                    ? UINT64_MAX
                                    : i->activity_dropped + count;
        } else {
            row = acount(i->activity);
            apush(i->activity, ((execution_debug_activity){ 0 }));
        }
        i->activity[row] = (execution_debug_activity){
            .generation = i->generation,
            .owner = a->instance->owner,
            .module = module,
            .function = function,
            .occurrence = occurrence,
            .from_module = a->debug_module,
            .from_function = a->debug_function,
            .from_occurrence = a->debug_previous
        };
        memcpy(
            i->activity[row].query_variables,
            a->instance->query_variables,
            sizeof i->activity[row].query_variables
        );
    }
    if (i->activity[row].count < UINT64_MAX) {
        i->activity[row].count++;
    }
    i->activity[row].time = time;
    a->debug_previous = occurrence;
    a->debug_module = module;
    a->debug_function = function;
}
static int pause_point(void *user, behavior_function_context *ctx, const behavior_source_point *point)
{
    execution_activation *a = user;
    execution_inspector *i = a->instance->program->owner_program->inspector;
    if (!i) {
        return 0;
    }
    execution_activation *owner = root(a);
    if (i->paused && i->paused != a) {
        return 0;
    }
    uint64_t id = 0;
    execution_module *m = function_module(i->program, ctx->source_function, &id);
    if (!m) {
        return 0;
    }
    int d = activation_depth(a, ctx);
    bool recording = execution_inspector_records(i, a->instance->owner);
    if (recording && !point->completion) {
        activity(i, a, m->plan.id, id, point->occurrence, ctx->now);
    }
    bool stop = i->requested && i->requested_owner == a->instance->owner;
    if (i->stepping && i->cursor.run == owner->debug_run) {
        stop |= i->control == EXECUTION_DEBUG_INTO ||
                (i->control == EXECUTION_DEBUG_OVER && d <= i->target_depth) ||
                (i->control == EXECUTION_DEBUG_OUT && d < i->target_depth);
    }
    for (int n = 0; n < acount(i->breakpoints); n++) {
        breakpoint b = i->breakpoints[n];
        stop |= !point->completion && b.module == m->plan.id &&
                b.function == id && b.occurrence == point->occurrence;
    }
    if (!recording && !stop) {
        return 0;
    }
    uint64_t step = i->cursor.step + 1;
    assert(step);
    i->cursor = (execution_debug_cursor){ .generation = i->generation,
                                          .run = owner->debug_run,
                                          .step = step,
                                          .module = m->plan.id,
                                          .function = id,
                                          .owner = a->instance->owner,
                                          .point = *point,
                                          .depth = d,
                                          .status = stop ? FLOW_PAUSED_RESULT : FLOW_OK };
    memcpy(
        i->cursor.query_variables,
        a->instance->query_variables,
        sizeof i->cursor.query_variables
    );
    frames(i, a, ctx, *point);
    owner_row *row = owner_snapshot(i, a->instance->owner, true);
    row->cursor = i->cursor;
    row->touched = step;
    asetlen(row->frames, acount(i->frames));
    if (acount(i->frames)) {
        memcpy(row->frames, i->frames, (size_t)acount(i->frames) * sizeof *row->frames);
    }
    if (!stop) {
        return 0;
    }
    i->requested = i->stepping = false;
    i->paused = a;
    a->debug_paused = true;
    owner->debug_paused = true;
    return 1;
}
static bool add_size(size_t *total, size_t n)
{
    if (n > SNAPSHOT_LIMIT - *total) {
        *total = SNAPSHOT_LIMIT;
        return false;
    }
    *total += n;
    return true;
}
static bool text_size(size_t *total, const char *text)
{
    if (!text) {
        return true;
    }
    size_t left = SNAPSHOT_LIMIT - *total;
    size_t n = strnlen(text, left);
    if (n == left) {
        *total = SNAPSHOT_LIMIT;
        return false;
    }
    return add_size(total, n + 1);
}
static bool fields_size(size_t *total, const type_desc *type, const void *data, int nesting)
{
    if (!type || !data || nesting >= 16) {
        return false;
    }
    for (int n = 0; n < type->field_count; n++) {
        const type_field *f = &type->fields[n];
        if (f->offset > type->size || f->size > type->size - f->offset) {
            return false;
        }
        const void *field = (const unsigned char *)data + f->offset;
        if (f->type == PIN_STRUCT) {
            const type_desc *nested = type_catalog_find(f->type_name);
            if (!nested || nested->size != f->size ||
                !fields_size(total, nested, field, nesting + 1)) {
                return false;
            }
        } else if (f->type == PIN_STR && f->text_descriptor) {
            const pin_string *text = *(const pin_string *const *)field;
            if (f->size != sizeof text || (!add_size(total, sizeof *text + 1) ||
                                           !add_size(total, text ? text->length : 0))) {
                return false;
            }
        } else if (f->type == PIN_STR || f->type == PIN_NAME) {
            if (f->size != sizeof(char *) ||
                !text_size(total, *(const char *const *)field)) {
                return false;
            }
        }
    }
    return true;
}
static bool value_size(size_t *total, const pin_value *v, int nesting, const pin_record *shared)
{
    if (nesting >= 16) {
        return false;
    }
    if (v->record) {
        if (!pin_record_valid(v->record) || !pin_value_type(v) ||
            (shared && !pin_record_same(shared, v->record)) ||
            (!shared && !add_size(total, v->record->bytes))) {
            return false;
        }
    } else if (shared || !text_size(total, v->type_name)) {
        return false;
    }
    if (v->container == PIN_CONTAINER_MAP) {
        const pin_map *map = v->ptr;
        if (v->type != PIN_UNION || v->record) {
            return false;
        }
        if (!map) {
            return true;
        }
        if (map->count < 0 || map->count > 65536 || (map->count && !map->entries) ||
            !pin_map_key_supported(map->key_type) ||
            !add_size(total, sizeof *map + (size_t)map->count * sizeof *map->entries) ||
            !value_size(total, &map->key_type, nesting + 1, NULL) ||
            !value_size(total, &map->value_type, nesting + 1, NULL)) {
            return false;
        }
        for (int n = 0; n < map->count; n++) {
            const pin_map_entry *entry = &map->entries[n];
            if (!pin_map_entry_valid(map, &entry->key, &entry->value) ||
                !value_size(total, &entry->key, nesting + 1, NULL) ||
                !value_size(total, &entry->value, nesting + 1, map->value_type.record)) {
                return false;
            }
        }
        return true;
    }
    if (v->container >= PIN_CONTAINER_ARRAY) {
        const pin_array *a = v->ptr;
        if (!a) {
            return true;
        }
        if ((v->record && (!pin_record_valid(a->record) ||
                           !pin_record_same(v->record, a->record) ||
                           a->element_type != PIN_STRUCT || !a->type_name ||
                           strcmp(a->type_name, v->type_name))) ||
            (!v->record && (a->record || !text_size(total, a->type_name))) ||
            !pin_array_valid(a) || a->count > 65536 ||
            !add_size(total, sizeof *a + (size_t)a->count * (sizeof(pin_value) + 2 * sizeof(int)))) {
            return false;
        }
        for (int n = 0; n < a->count; n++) {
            pin_value item = pin_array_get(a, n);
            if ((v->record && (item.type != PIN_STRUCT ||
                               item.container != PIN_CONTAINER_SINGLE)) ||
                !value_size(total, &item, nesting + 1, v->record)) {
                return false;
            }
        }
        return true;
    }
    if (v->type == PIN_STR) {
        return add_size(total, sizeof(pin_string) + 1) &&
               add_size(total, pin_text(*v).length);
    }
    if (v->type == PIN_NAME) {
        return text_size(total, v->str);
    }
    if (v->type == PIN_STRUCT && v->ptr) {
        const type_desc *type = pin_value_type(v);
        return type && add_size(total, type->size) &&
               fields_size(total, type, v->ptr, nesting + 1);
    }
    if (v->type == PIN_UNION && v->ptr) {
        const pin_union *value = v->ptr;
        if (value->count < 0 || value->count > 65536 ||
            !add_size(total, sizeof *value + (size_t)value->count * sizeof(pin_value))) {
            return false;
        }
        for (int n = 0; n < value->count; n++) {
            if (!value_size(total, &value->values[n], nesting + 1, NULL)) {
                return false;
            }
        }
    }
    return v->type != PIN_FLOW;
}
static void capture(
    void *user,
    behavior_function_context *ctx,
    const behavior_source_watch *source,
    const pin_value *value
)
{
    execution_activation *a = user;
    execution_inspector *i = a->instance->program->owner_program->inspector;
    if (!i || !execution_inspector_records(i, a->instance->owner)) {
        return;
    }
    uint64_t id = 0;
    execution_module *m = function_module(i->program, ctx->source_function, &id);
    if (!m) {
        return;
    }
    int row = -1;
    for (int n = 0; n < acount(i->watches); n++) {
        const execution_debug_watch *v = &i->watches[n].view;
        if (v->generation == i->generation && v->owner == a->instance->owner &&
            !memcmp(v->query_variables, a->instance->query_variables, sizeof v->query_variables) &&
            v->module == m->plan.id && v->function == id &&
            v->source.origin.occurrence == source->origin.occurrence &&
            v->source.output == source->output && v->source.ordinal == source->ordinal) {
            row = n;
            break;
        }
    }
    if (row < 0 && acount(i->watches) == WATCH_LIMIT) {
        for (int n = 0; n < WATCH_LIMIT; n++) {
            if (a->instance->owner != i->selected_owner &&
                i->watches[n].view.owner == i->selected_owner) {
                continue;
            }
            if (row < 0 || i->watches[n].touched < i->watches[row].touched) {
                row = n;
            }
        }
        if (row < 0) {
            return;
        }
    }
    if (row < 0) {
        row = acount(i->watches);
        apush(i->watches, ((watch_row){ 0 }));
    }
    watch_row *r = &i->watches[row];
    i->bytes -= r->bytes;
    row_free(r);
    execution_module_retain(m);
    *r = (watch_row){ .module = m,
                      .touched = ++i->serial,
                      .view = { .generation = i->generation,
                                .run = root(a)->debug_run,
                                .module = m->plan.id,
                                .function = id,
                                .owner = a->instance->owner,
                                .source = *source,
                                .value = source->initial } };
    memcpy(r->view.query_variables, a->instance->query_variables, sizeof r->view.query_variables);
    if (!value) {
        r->view.availability = EXECUTION_WATCH_UNCONSUMED_REFERENCE;
        return;
    }
    size_t bytes = 0;
    if (!value_size(&bytes, value, 0, NULL)) {
        r->view.availability = bytes == SNAPSHOT_LIMIT ? EXECUTION_WATCH_BUDGET
                                                       : EXECUTION_WATCH_UNSUPPORTED;
        return;
    }
    if (bytes > SNAPSHOT_LIMIT - i->bytes) {
        r->view.availability = EXECUTION_WATCH_BUDGET;
        return;
    }
    /* A watch records this observation even when the live value mutates later. */
    if (pin_value_capture_to_arena(&r->storage, &r->view.value, value)) {
        arena_free(&r->storage);
        r->view.value = source->initial;
        r->view.availability = EXECUTION_WATCH_UNSUPPORTED;
        return;
    }
    size_t used, capacity;
    arena_usage(&r->storage, &used, &capacity);
    if (capacity > SNAPSHOT_LIMIT - i->bytes) {
        arena_free(&r->storage);
        r->view.value = source->initial;
        r->view.availability = EXECUTION_WATCH_BUDGET;
        return;
    }
    r->bytes = capacity;
    i->bytes += capacity;
}
void program_inspection_bind(execution_activation *a)
{
    execution_inspector *i = a->instance->program->owner_program->inspector;
    if (!i) {
        return;
    }
    execution_activation *owner = root(a);
    if (!owner->debug_run) {
        assert(i->next_run < UINT64_MAX);
        owner->debug_run = ++i->next_run;
    }
    a->context.source_debug_user = a;
    a->context.source_pause = pause_point;
    a->context.source_capture = capture;
}
bool program_inspection_paused(const execution_activation *a)
{
    return root((execution_activation *)a)->debug_paused || a->debug_paused;
}
void program_inspection_finish(execution_activation *a)
{
    execution_inspector *i = a->instance->program->owner_program->inspector;
    if (!i) {
        return;
    }
    int status = i->paused && i->paused->instance == a->instance ? FLOW_PAUSED_RESULT
                                                                 : a->status;
    owner_row *row = owner_snapshot(i, a->instance->owner, false);
    if (row && row->cursor.run == root(a)->debug_run) {
        row->cursor.status = status;
    }
    if (i->cursor.run == root(a)->debug_run) {
        i->cursor.status = status;
    }
    if (a->finished && i->paused == a) {
        i->paused = NULL;
    }
}
void program_inspection_retire(execution_program *p)
{
    execution_inspector *i = p->inspector;
    if (!i) {
        return;
    }
    for (int n = 0; n < acount(p->instances) && n < WATCH_LIMIT; n++) {
        apush(i->owners, p->instances[n]->owner);
    }
    i->program = NULL;
    i->paused = NULL;
    i->cursor.retired = true;
    for (int n = 0; n < acount(i->snapshots); n++) {
        i->snapshots[n].cursor.retired = true;
    }
    p->inspector = NULL;
    execution_inspector_free(i);
}
void program_inspection_release(execution_activation *a)
{
    execution_inspector *i = a->instance->program->owner_program->inspector;
    if (!i || i->paused != a) {
        return;
    }
    root(a)->debug_paused = false;
    i->paused = NULL;
    i->cursor.retired = true;
    i->requested = i->stepping = false;
}
void program_inspection_remove_owner(execution_instance *instance)
{
    if (instance->program != instance->program->owner_program) {
        return;
    }
    execution_inspector *i = instance->program->inspector;
    if (!i) {
        return;
    }
    owner_row *row = owner_snapshot(i, instance->owner, false);
    if (row && !memcmp(
                   row->cursor.query_variables,
                   instance->query_variables,
                   sizeof instance->query_variables
               )) {
        row->cursor.retired = true;
    }
    if (i->cursor.owner == instance->owner && !memcmp(
                                                  i->cursor.query_variables,
                                                  instance->query_variables,
                                                  sizeof instance->query_variables
                                              )) {
        i->cursor.retired = true;
    }
}
bool execution_inspector_pause(execution_inspector *i, uint64_t owner)
{
    assert(i);
    if (!i->program || i->paused || !execution_program_instance(i->program, owner)) {
        return false;
    }
    i->requested_owner = owner;
    i->requested = true;
    return true;
}
int execution_inspector_control(execution_inspector *i, execution_debug_control control)
{
    assert(i);
    if (!i->program || !i->paused || control < EXECUTION_DEBUG_RESUME ||
        control > EXECUTION_DEBUG_OUT) {
        return FLOW_ERR_STATE;
    }
    execution_activation *a = i->paused;
    i->paused = NULL;
    a->debug_paused = false;
    root(a)->debug_paused = false;
    i->stepping = control != EXECUTION_DEBUG_RESUME;
    i->control = control;
    i->target_depth = i->cursor.depth;
    int status = program_activation_run(
        a,
        a->context.now,
        a->context.delta_seconds,
        a->context.budget
    );
    program_apply_services(a->instance->program->owner_program);
    return status;
}
bool execution_inspector_breakpoint(
    execution_inspector *i,
    uint64_t module,
    uint64_t function,
    uint64_t occurrence,
    bool enabled
)
{
    assert(i);
    if (!i->program || !module || !function || !occurrence) {
        return false;
    }
    for (int n = 0; n < acount(i->breakpoints); n++) {
        breakpoint b = i->breakpoints[n];
        if (b.module == module && b.function == function && b.occurrence == occurrence) {
            if (!enabled) {
                adel(i->breakpoints, n);
            }
            return true;
        }
    }
    if (!enabled) {
        return true;
    }
    if (acount(i->breakpoints) == BREAKPOINT_LIMIT) {
        return false;
    }
    apush(i->breakpoints, ((breakpoint){ module, function, occurrence }));
    return true;
}
