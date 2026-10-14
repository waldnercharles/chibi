#include "source_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

static void observe(void *context, const execution_profile_sample *sample)
{
    source_registry *r = context;
    const char *name = NULL;
    for (int i = 0; i < acount(r->records); i++) {
        if (r->records[i].program == sample->program) {
            name = r->records[i].name;
            break;
        }
    }
    assert(r->profiling && name);
    source_profile *profile = NULL;
    for (int i = 0; i < acount(r->profiles); i++) {
        source_profile *row = &r->profiles[i];
        if (row->module == sample->module && row->function == sample->function &&
            row->row.owner == sample->owner && !strcmp(row->name, name)) {
            profile = row;
            break;
        }
    }
    if (!profile) {
        if (acount(r->profiles) == SOURCE_PROFILE_ROWS_MAX) {
            r->profile_dropped++;
            return;
        }
        source_profile row = { .module = (execution_module *)sample->module,
                               .function = sample->function,
                               .name = xstrdup(name) };
        execution_module_retain(row.module);
        row.row = (source_profile_row){ .module_id = execution_module_id(row.module),
                                        .owner = sample->owner,
                                        .name = row.name,
                                        .cpu_available = true };
        bool found = false;
        for (int i = 0; i < execution_module_function_count(row.module); i++) {
            execution_module_function_info info = execution_module_function_at(row.module, i);
            if (info.body == sample->function || info.subscription == sample->function) {
                row.row.function_id = info.id;
                row.row.kind = info.kind;
                found = true;
                break;
            }
        }
        row.row.internal = !found;
        /* Index zero is the builder's unnamed sentinel. Source-less native
         * fixtures remain unlabeled; authored samples use their first origin. */
        for (int i = 0; i < behavior_function_source_count(sample->function); i++) {
            const behavior_function_source
                *source = behavior_function_source_at(sample->function, i);
            if (!source->asset_name || !source->asset_name[0]) {
                continue;
            }
            row.row.source = source->asset_name;
            row.row.start = source->start;
            row.row.end = source->end;
            row.row.line = source->line;
            row.row.column = source->column;
            break;
        }
        apush(r->profiles, row);
        profile = &r->profiles[acount(r->profiles) - 1];
    }
    profile->row.entries += !sample->resume;
    profile->row.resumes += sample->resume;
    profile->row.cpu_seconds += sample->cpu_seconds;
    profile->row.cpu_available &= sample->cpu_available;
    profile->row.last_status = sample->status;
}
void source_profile_bind(source_registry *r)
{
    assert(!r->staging && !r->executing);
    for (int i = 0; i < acount(r->records); i++) {
        execution_program_observe(r->records[i].program, r->profiling ? observe : NULL, r);
    }
}
void source_profile_reset(source_registry *r)
{
    assert(r && !r->staging && !r->executing);
    for (int i = 0; i < acount(r->profiles); i++) {
        execution_module_free(r->profiles[i].module);
        free(r->profiles[i].name);
    }
    aclear(r->profiles);
    r->profile_dropped = 0;
}
void source_profile_enable(source_registry *r, bool enabled)
{
    assert(r && !r->staging && !r->executing);
    if (enabled) {
        source_profile_reset(r);
        asetcap(r->profiles, SOURCE_PROFILE_ROWS_MAX);
    }
    r->profiling = enabled;
    source_profile_bind(r);
}
int source_profile_count(const source_registry *r)
{
    return acount(r->profiles);
}
const source_profile_row *source_profile_at(const source_registry *r, int index)
{
    assert(index >= 0 && index < acount(r->profiles));
    return &r->profiles[index].row;
}
uint64_t source_profile_dropped(const source_registry *r)
{
    return r->profile_dropped;
}
