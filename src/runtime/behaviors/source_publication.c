#include "execution/component.h"
#include "runtime/behaviors/module_set.h"
#include "source_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int publication_error(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return -1;
}
static void program_rejection(
    const source_registry *r,
    const source_publication *entry,
    const service_binding *services,
    bool replacing,
    char *error,
    size_t size
)
{
    if (!size) {
        return;
    }
    if (!replacing) {
        int count = 0;
        const char *const *required = execution_module_services(entry->module, &count);
        for (int i = 0; i < count; i++) {
            bool present = r->world &&
                           (!strcmp(required[i], "ecs.world") ||
                            !strcmp(required[i], EVAL_COMPONENT_READER_SERVICE));
            for (int j = 0; j < acount(services); j++) {
                present |= services[j].value &&
                           !strcmp(required[i], services[j].type_name);
            }
            if (!present) {
                snprintf(
                    error,
                    size,
                    "Source %.128s requires unavailable service %.128s",
                    entry->name,
                    required[i]
                );
                return;
            }
        }
    }
    snprintf(
        error,
        size,
        "Source %.128s rejected %s contract",
        entry->name,
        execution_module_components_valid(entry->module, r->world)
            ? "runtime service or state"
            : "component/query/binding"
    );
}
/* Whether only kept state refused a replacement: one that restarts the
 * program instead would succeed. */
static bool restart_applies(
    const execution_program *program,
    execution_module *module,
    const service_binding *defaults,
    const service_binding *overrides
)
{
    execution_program *restarted = execution_program_prepare_replace_services(
        program,
        module,
        true,
        defaults,
        overrides
    );
    bool applies = restarted;
    execution_program_free(restarted);
    return applies;
}
struct source_staging
{
    source_registry *registry;
    source_publication *entries;
    int count, old_count;
    int *changes, *targets;
    execution_program **programs;
    source_record *final;
};
static void free_stage(source_staging *stage)
{
    for (int i = 0; i < stage->count; i++) {
        execution_program_free(stage->programs ? stage->programs[i] : NULL);
        free((char *)stage->entries[i].name);
        free((char *)stage->entries[i].previous_name);
    }
    for (int i = 0; i < acount(stage->final); i++) {
        free(stage->final[i].name);
    }
    afree(stage->final);
    free(stage->programs);
    free(stage->entries);
    free(stage->targets);
    free(stage->changes);
    free(stage);
}

source_staging *source_stage(
    source_registry *r,
    const source_publication *entries,
    int count,
    const service_binding *services,
    char *error,
    size_t size
)
{
    /* Native bindings name the functions of the bound generation. */
    assert(
        r && !r->executing && !r->staging && !r->natives && count >= 0 &&
        (!count || entries) && (!size || error)
    );
    if (size) {
        error[0] = '\0';
    }
    if (count > MODULE_SET_MAX_MODULES * 2) {
        publication_error(error, size, "source publication entry count exceeds limit");
        return NULL;
    }
    source_staging *stage = xcalloc(1, sizeof *stage);
    stage->registry = r;
    r->staging = stage;
    stage->count = count;
    stage->old_count = acount(r->records);
    stage->entries = xcalloc((size_t)(count + 1), sizeof *stage->entries);
    for (int i = 0; i < count; i++) {
        stage->entries[i] = entries[i];
        stage->entries[i].name = entries[i].name ? xstrdup(entries[i].name) : NULL;
        stage->entries[i].previous_name = entries[i].previous_name
                                            ? xstrdup(entries[i].previous_name)
                                            : NULL;
    }
    entries = stage->entries;
    int old_count = acount(r->records);
    int *changes = stage->changes = xmalloc((size_t)(old_count + 1) * sizeof *changes);
    int *targets = stage->targets = xmalloc((size_t)(count + 1) * sizeof *targets);
    execution_program **staged = stage->programs = xcalloc((size_t)(count + 1), sizeof *staged);
    source_record *final = NULL;
    const execution_module **modules = NULL;
    service_binding *bindings = NULL;
    for (int i = 0; i < old_count; i++) {
        changes[i] = -1;
    }
    for (int i = 0; i < count; i++) {
        const source_publication *entry = &entries[i];
        targets[i] = entry->previous_name ? record_index(r, entry->previous_name)
                                          : -1;
        if ((entry->previous_name && targets[i] < 0) ||
            (entry->module && (!entry->name || !entry->name[0])) ||
            (!entry->module &&
             (targets[i] < 0 ||
              (entry->name && strcmp(entry->name, entry->previous_name))))) {
            publication_error(error, size, "source publication entry has no valid registration");
            goto done;
        }
        if (targets[i] >= 0) {
            if (changes[targets[i]] >= 0) {
                publication_error(error, size, "source publication changes a registration more than once");
                goto done;
            }
            changes[targets[i]] = i;
        }
    }
    for (int i = 0; i < old_count; i++) {
        int change = changes[i];
        if (change >= 0 && !entries[change].module) {
            continue;
        }
        source_record next = r->records[i];
        if (change >= 0) {
            next.name = (char *)entries[change].name;
            next.module = entries[change].module;
        }
        next.system = source_module_system(next.module);
        next.name = xstrdup(next.name);
        apush(final, next);
    }
    for (int i = 0; i < count; i++) {
        if (targets[i] < 0) {
            apush(
                final,
                ((source_record){ .name = xstrdup(entries[i].name),
                                  .module = entries[i].module,
                                  .system = source_module_system(entries[i].module) })
            );
        }
    }
    for (int i = 0; i < acount(final); i++) {
        for (int j = 0; j < i; j++) {
            if (!strcmp(final[i].name, final[j].name)) {
                publication_error(error, size, "source publication final name is duplicated");
                goto done;
            }
        }
        apush(modules, final[i].module);
    }
    bool valid_events = r->events
                          ? event_catalog_validate(r->events, modules, acount(modules), error, size)
                          : module_set_validate(modules, acount(modules), 0, error, size);
    if (!valid_events) {
        goto done;
    }
    for (int i = 0; i < acount(r->services); i++) {
        apush(bindings, r->services[i]);
    }
    for (int i = 0; i < acount(services); i++) {
        if (!services[i].type_name || !services[i].type_name[0]) {
            publication_error(error, size, "source publication service name is invalid");
            goto done;
        }
        int index = -1;
        for (int j = 0; j < acount(bindings); j++) {
            if (!strcmp(services[i].type_name, bindings[j].type_name)) {
                index = j;
                break;
            }
        }
        if (index >= 0) {
            bindings[index] = services[i];
        } else {
            apush(bindings, services[i]);
        }
    }
    for (int i = 0; i < count; i++) {
        if (!entries[i].module) {
            continue;
        }
        staged[i] = targets[i] >= 0
                      ? execution_program_prepare_replace_services(
                            r->records[targets[i]].program,
                            entries[i].module,
                            entries[i].restart,
                            r->services,
                            services
                        )
                      : execution_program_new(entries[i].module, r->world, bindings);
        if (!staged[i] && targets[i] >= 0 && !entries[i].restart &&
            restart_applies(r->records[targets[i]].program, entries[i].module, r->services, services)) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "Source %.128s cannot keep its state; restart it to apply "
                    "the change",
                    entries[i].name
                );
            }
            goto done;
        }
        if (!staged[i]) {
            program_rejection(r, &entries[i], bindings, targets[i] >= 0, error, size);
            goto done;
        }
        execution_program_set_name(staged[i], entries[i].name);
        for (int j = 0; j < acount(final); j++) {
            if (!strcmp(final[j].name, entries[i].name)) {
                execution_program_set_enabled(staged[i], source_record_active(r, &final[j]));
                break;
            }
        }
    }
    stage->final = final;
    afree(bindings);
    afree(modules);
    r->staging = stage;
    return stage;
done:
    r->staging = NULL;
    stage->final = final;
    afree(bindings);
    afree(modules);
    free_stage(stage);
    return NULL;
}
void source_discard(source_staging *stage)
{
    if (!stage) {
        return;
    }
    assert(stage->registry->staging == stage && !stage->registry->executing);
    source_registry *r = stage->registry;
    free_stage(stage);
    r->staging = NULL;
}
void source_commit(source_staging *stage)
{
    assert(stage && stage->registry->staging == stage && !stage->registry->executing);
    source_registry *r = stage->registry;
    const source_publication *entries = stage->entries;
    int count = stage->count, old_count = stage->old_count;
    int *changes = stage->changes, *targets = stage->targets;
    execution_program **staged = stage->programs;
    source_record *final = stage->final;
    /* Every rejecting operation has completed: the registry runs nothing while
     * staged, so kept values still match their replacement. Publication
     * performs no dispatch. */
    for (int i = 0; i < count; i++) {
        if (!entries[i].module || targets[i] < 0) {
            continue;
        }
        execution_program *program = r->records[targets[i]].program;
        int status = execution_program_commit_replace(program, staged[i]);
        assert(!status);
        (void)status;
        staged[i] = NULL;
        for (int j = 0; entries[i].restart && j < acount(r->attachments); j++) {
            if (r->attachments[j].program == program) {
                r->attachments[j].begin = 0;
            }
        }
    }
    for (int i = 0; i < old_count; i++) {
        int change = changes[i];
        if (change >= 0 && !entries[change].module) {
            execution_program *program = r->records[i].program;
            for (int j = acount(r->attachments) - 1; j >= 0; j--) {
                if (r->attachments[j].program == program) {
                    adel(r->attachments, j);
                }
            }
            execution_program_free(program);
        }
        free(r->records[i].name);
    }
    for (int i = 0; i < count; i++) {
        if (targets[i] >= 0) {
            continue;
        }
        for (int j = 0; j < acount(final); j++) {
            if (!strcmp(final[j].name, entries[i].name)) {
                final[j].program = staged[i];
                staged[i] = NULL;
                break;
            }
        }
    }
    afree(r->records);
    r->records = final;
    final = NULL;

    stage->final = NULL;
    r->staging = NULL;
    source_profile_bind(r);
    free_stage(stage);
}
int source_publish(
    source_registry *r,
    const source_publication *entries,
    int count,
    const service_binding *services,
    char *error,
    size_t size
)
{
    source_staging *stage = source_stage(r, entries, count, services, error, size);
    if (!stage) {
        return -1;
    }
    source_commit(stage);
    return 0;
}
