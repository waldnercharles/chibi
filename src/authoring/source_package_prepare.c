#include "authoring/source_package_prepare.h"

#include "authoring/source_catalog_snapshot.h"
#include "authoring/source_link.h"
#include "authoring/source_package_exports.h"
#include "capabilities/catalog_snapshot.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct package_generation package_generation;
typedef struct
{
    int document;
    script_range range;
    const char *alias, *identity;
} package_import_key;
struct package_generation
{
    authoring_package view;
    int references;
    arena storage;
    script_catalog native;
    unsigned char *defaults;
    bool defaults_known;
    script_lower_bindings bindings;
    behavior_backend backend;
    script_source_document *documents;
    int document_count;
    package_import_key *imports;
    package_generation **dependencies;
    source_package_exports *export_owner;
    script_diagnostic *diagnostics;
    script_specialization_request *requests;
};
struct authoring_packages
{
    source_package_snapshot *snapshot;
    package_generation **packages;
    int count, reused;
    bool valid;
    char error[192];
    arena storage;
    script_specialization_request *requests;
};

#define PACKAGE_SPECIALIZATIONS 256

static const char *copy(arena *storage, const char *text)
{
    size_t bytes = strlen(text) + 1;
    char *owned = arena_alloc(storage, bytes);
    memcpy(owned, text, bytes);
    return owned;
}

static script_specialization_request own_request(arena *storage, const script_specialization_request *source)
{
    script_specialization_request result = *source;
    result.owner = copy(storage, source->owner);
    script_compile_binding
        *bindings = arena_alloc(storage, (size_t)source->binding_count * sizeof *bindings);
    for (int i = 0; i < source->binding_count; i++) {
        bindings[i] = source->bindings[i];
        bindings[i].name = (char *)copy(storage, bindings[i].name);
        if (!bindings[i].value.is_type && !bindings[i].value.is_callable) {
            int error = pin_value_capture_to_arena(
                storage,
                &bindings[i].value.value,
                &source->bindings[i].value.value
            );
            assert(!error);
            (void)error;
        }
    }
    result.bindings = bindings;
    return result;
}
static bool same_request(const script_specialization_request *a, const script_specialization_request *b)
{
    if (a->id != b->id || a->template_id != b->template_id ||
        strcmp(a->owner, b->owner) || a->evaluate != b->evaluate ||
        a->binding_count != b->binding_count) {
        return false;
    }
    for (int i = 0; i < a->binding_count; i++) {
        const script_compile_binding *x = &a->bindings[i], *y = &b->bindings[i];
        if (strcmp(x->name, y->name) || x->value.is_type != y->value.is_type ||
            x->value.is_callable != y->value.is_callable ||
            (x->value.is_type && !script_type_equal(x->value.type, y->value.type)) ||
            (x->value.is_callable && x->value.callable != y->value.callable)) {
            return false;
        }
    }
    script_catalog_specialization x = { .id = a->id,
                                        .template_id = a->template_id,
                                        .bindings = a->bindings,
                                        .binding_count = a->binding_count },
                                  y = { .id = b->id,
                                        .template_id = b->template_id,
                                        .bindings = b->bindings,
                                        .binding_count = b->binding_count };
    script_catalog left = { .specializations = &x, .specialization_count = 1 },
                   right = { .specializations = &y, .specialization_count = 1 };
    unsigned char *left_key = NULL, *right_key = NULL;
    bool equal = script_catalog_values_key(&left, &left_key) &&
                 script_catalog_values_key(&right, &right_key) &&
                 acount(left_key) == acount(right_key) &&
                 !memcmp(left_key, right_key, (size_t)acount(left_key));
    afree(left_key);
    afree(right_key);
    return equal;
}

static package_generation *retain(package_generation *generation)
{
    assert(generation && generation->references > 0 && generation->references < INT_MAX);
    generation->references++;
    return generation;
}

static void release(package_generation *generation)
{
    if (!generation) {
        return;
    }
    assert(generation->references > 0);
    if (--generation->references) {
        return;
    }
    execution_module_free(generation->view.module);
    source_package_exports_free(generation->export_owner);
    script_analysis_free(generation->view.analysis);
    for (int i = 0; i < acount(generation->dependencies); i++) {
        release(generation->dependencies[i]);
    }
    afree(generation->dependencies);
    afree(generation->imports);
    afree(generation->diagnostics);
    afree(generation->requests);
    afree(generation->defaults);
    free(generation->documents);
    arena_free(&generation->storage);
    free(generation);
}

static void diagnostic(package_generation *generation, script_range range, const char *message)
{
    script_diagnostic item = { .range = range };
    snprintf(item.message, sizeof item.message, "%s", message);
    apush(generation->diagnostics, item);
    generation->view.diagnostics = generation->diagnostics;
    generation->view.diagnostic_count = acount(generation->diagnostics);
}

static const char *import_alias(const source_package_import *import)
{
    if (import->alias) {
        return import->alias;
    }
    const char *slash = strrchr(import->directory, '/');
    return slash ? slash + 1 : import->directory;
}

static int target_package(const source_packages *graph, const source_package_import *import)
{
    for (int i = 0; i < source_packages_count(graph); i++) {
        if (!strcmp(source_packages_at(graph, i)->directory, import->directory)) {
            return i;
        }
    }
    assert(!"resolved import has no package");
    return -1;
}

static package_generation *previous_package(const authoring_packages *previous, const char *identity)
{
    if (!previous) {
        return NULL;
    }
    for (int i = 0; i < previous->count; i++) {
        package_generation *generation = previous->packages[i];
        if (generation && !strcmp(generation->view.identity, identity)) {
            return generation;
        }
    }
    return NULL;
}

static bool same_bindings(const script_lower_bindings *a, const script_lower_bindings *b)
{
    return a->generation && a->generation == b->generation && a->user == b->user &&
           a->host == b->host && a->action == b->action && a->global == b->global;
}

static bool reusable(const package_generation *old, const package_generation *current)
{
    if (!old || !old->view.valid || old->backend != current->backend ||
        !same_bindings(&old->bindings, &current->bindings) ||
        !old->defaults_known || !current->defaults_known ||
        !source_catalog_metadata_equal(&old->native, &current->native) ||
        acount(old->defaults) != acount(current->defaults) ||
        (acount(old->defaults) &&
         memcmp(old->defaults, current->defaults, (size_t)acount(old->defaults))) ||
        old->document_count != current->document_count ||
        acount(old->imports) != acount(current->imports) ||
        acount(old->dependencies) != acount(current->dependencies) ||
        acount(old->requests) != acount(current->requests)) {
        return false;
    }
    for (int i = 0; i < acount(current->requests); i++) {
        if (!same_request(&old->requests[i], &current->requests[i])) {
            return false;
        }
    }
    for (int i = 0; i < current->document_count; i++) {
        const script_source_document *a = &old->documents[i],
                                     *b = &current->documents[i];
        if (strcmp(a->path, b->path) || strcmp(a->key, b->key) ||
            strcmp(a->text, b->text)) {
            return false;
        }
    }
    for (int i = 0; i < acount(current->imports); i++) {
        const package_import_key *a = &old->imports[i], *b = &current->imports[i];
        if (a->document != b->document || strcmp(a->alias, b->alias) ||
            strcmp(a->identity, b->identity)) {
            return false;
        }
    }
    for (int i = 0; i < acount(current->dependencies); i++) {
        if (old->dependencies[i] != current->dependencies[i]) {
            return false;
        }
    }
    return true;
}

static void add_dependency(package_generation *generation, package_generation *dependency)
{
    int position = 0;
    while (position < acount(generation->dependencies) &&
           strcmp(
               generation->dependencies[position]->view.identity,
               dependency->view.identity
           ) < 0) {
        position++;
    }
    apush(generation->dependencies, retain(dependency));
    for (int i = acount(generation->dependencies) - 1; i > position; i--) {
        generation->dependencies[i] = generation->dependencies[i - 1];
    }
    generation->dependencies[position] = dependency;
}

static package_generation *generation_new(
    authoring_packages *owner,
    int index,
    const script_catalog *native,
    const script_lower_bindings *bindings,
    behavior_backend backend,
    script_package_document *documents,
    script_package_import **imports
)
{
    const source_packages *graph = source_package_snapshot_graph(owner->snapshot);
    const source_package *package = source_packages_at(graph, index);
    int document_count, import_count;
    const script_package_document
        *input = source_package_snapshot_documents(owner->snapshot, &document_count);
    const source_package_import
        *input_imports = source_package_snapshot_imports(owner->snapshot, &import_count);
    package_generation *generation = xcalloc(1, sizeof *generation);
    generation->references = 1;
    generation->bindings = *bindings;
    generation->backend = backend;
    generation->view.identity = copy(
        &generation->storage,
        source_package_snapshot_identity(owner->snapshot, index)
    );
    generation->view.generation = generation;
    for (int i = 0; i < acount(owner->requests); i++) {
        if (!strcmp(owner->requests[i].owner, generation->view.identity)) {
            apush(generation->requests, own_request(&generation->storage, &owner->requests[i]));
        }
    }

    generation->document_count = package->file_count;
    generation
        ->documents = xcalloc((size_t)package->file_count, sizeof *generation->documents);
    for (int i = 0; i < package->file_count; i++) {
        int file = package->files[i];
        assert(file >= 0 && file < document_count);
        documents[i] = input[file];
        generation->documents[i] = (script_source_document){
            .path = (char *)copy(&generation->storage, input[file].path),
            .key = (char *)copy(&generation->storage, input[file].key),
            .text = (char *)copy(&generation->storage, input[file].syntax->text),
        };
        for (int j = 0; j < import_count; j++) {
            const source_package_import *import = &input_imports[j];
            if (import->file != file) {
                continue;
            }
            int target = target_package(graph, import);
            const char *identity = copy(
                &generation->storage,
                source_package_snapshot_identity(owner->snapshot, target)
            );
            const char *alias = copy(&generation->storage, import_alias(import));
            script_range range = import->range;
            range.document = i;
            apush(generation->imports, ((package_import_key){ i, range, alias, identity }));
            apush(
                *imports,
                ((script_package_import){
                    .document = i,
                    .range = range,
                    .alias = alias,
                    .identity = identity,
                    .exports = target == index || !strcmp(alias, "_")
                                 ? NULL
                                 : owner->packages[target]->view.exports,
                })
            );
        }
    }
    for (int i = 0; i < owner->count; i++) {
        if (i != index && source_packages_dependency(graph, index, i) >= 0) {
            assert(owner->packages[i]);
            add_dependency(generation, owner->packages[i]);
        }
    }
    if (!script_catalog_snapshot(&generation->storage, native, &generation->native)) {
        diagnostic(generation, (script_range){ .document = -1 }, "Cannot retain native declaration defaults");
        return generation;
    }
    generation->defaults_known = source_catalog_default_key(
        &generation->native,
        &generation->defaults
    );
    return generation;
}

typedef struct
{
    script_catalog_type *types;
    script_catalog_capability *capabilities;
    script_catalog_global *globals;
    script_catalog_relationship *relationships;
} catalog_members;

static void catalog_append(catalog_members *members, const script_catalog *catalog)
{
    for (int i = 0; i < catalog->type_count; i++) {
        int j = 0;
        while (j < acount(members->types) &&
               members->types[j].id != catalog->types[i].id) {
            j++;
        }
        if (j == acount(members->types)) {
            apush(members->types, catalog->types[i]);
        }
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        int j = 0;
        while (j < acount(members->capabilities) &&
               members->capabilities[j].id != catalog->capabilities[i].id) {
            j++;
        }
        if (j == acount(members->capabilities)) {
            apush(members->capabilities, catalog->capabilities[i]);
        }
    }
    for (int i = 0; i < catalog->global_count; i++) {
        int j = 0;
        while (j < acount(members->globals) &&
               members->globals[j].id != catalog->globals[i].id) {
            j++;
        }
        if (j == acount(members->globals)) {
            apush(members->globals, catalog->globals[i]);
        }
    }
    for (int i = 0; i < catalog->relationship_count; i++) {
        int j = 0;
        while (j < acount(members->relationships) &&
               members->relationships[j].id != catalog->relationships[i].id) {
            j++;
        }
        if (j == acount(members->relationships)) {
            apush(members->relationships, catalog->relationships[i]);
        }
    }
}

static void dependency_closure(catalog_members *members, execution_module ***libraries, package_generation *generation)
{
    for (int i = 0; i < acount(*libraries); i++) {
        if ((*libraries)[i] == generation->view.module) {
            return;
        }
    }
    catalog_append(members, generation->view.exports->metadata);
    apush(*libraries, generation->view.module);
    for (int i = 0; i < acount(generation->dependencies); i++) {
        dependency_closure(members, libraries, generation->dependencies[i]);
    }
}
static void lower_generation(package_generation *generation, const script_package_import *imports, int import_count)
{
    catalog_members members = { 0 };
    execution_module **libraries = NULL;
    catalog_append(&members, &generation->native);
    for (int i = 0; i < acount(generation->dependencies); i++) {
        package_generation *dependency = generation->dependencies[i];
        dependency_closure(&members, &libraries, dependency);
    }
    script_catalog catalog = {
        .types = members.types,
        .type_count = acount(members.types),
        .capabilities = members.capabilities,
        .capability_count = acount(members.capabilities),
        .globals = members.globals,
        .global_count = acount(members.globals),
        .relationships = members.relationships,
        .relationship_count = acount(members.relationships),
        .entity_type = generation->native.entity_type,
        .tick_event = generation->native.tick_event,
    };
    authoring_source_link link = {
        .native = generation->bindings,
        .catalog = &catalog,
        .libraries = libraries,
        .library_count = acount(libraries),
    };
    script_lower_bindings bindings = authoring_source_bindings(&link);
    script_lower_package_result
        lowered = script_lower_package(generation->view.analysis, &catalog, &bindings);
    execution_module_plan *plan = lowered.plan;
    for (int i = 0; i < plan->diagnostic_count; i++) {
        const execution_module_diagnostic *item = &plan->diagnostics[i];
        diagnostic(
            generation,
            (script_range){
                .start = item->range.start,
                .end = item->range.end,
                .line = item->range.line,
                .column = item->range.column,
                .document = item->range.document,
            },
            item->message
        );
    }
    if (plan->valid) {
        source_package_exports_free(generation->export_owner);
        generation->export_owner = source_package_exports_new(generation->view.analysis, &catalog, &lowered);
        source_package_exports_bind_imports(generation->export_owner, imports, import_count);
        const char *error = source_package_exports_error(generation->export_owner);
        if (error) {
            diagnostic(generation, (script_range){ .document = -1 }, error);
        } else {
            generation->view.exports = source_package_exports_view(generation->export_owner);
            char message[192] = { 0 };
            generation->view.module = execution_module_finish(&plan, generation->backend, message, sizeof message);
            if (!generation->view.module) {
                diagnostic(generation, (script_range){ .document = -1 }, message);
            }
        }
    }
    execution_module_plan_free(plan);
    afree(libraries);
    afree(members.types);
    afree(members.capabilities);
    afree(members.globals);
    afree(members.relationships);
    generation->view.valid = generation->view.module &&
                             !generation->view.diagnostic_count;
}

static void prepare_generation(
    package_generation *generation,
    const script_package_document *documents,
    const script_package_import *imports,
    int import_count
)
{
    if (generation->view.diagnostic_count) {
        return;
    }
    for (int i = 0; i < acount(generation->dependencies); i++) {
        const package_generation *dependency = generation->dependencies[i];
        if (dependency->view.exports) {
            continue;
        }
        for (int j = 0; j < acount(generation->imports); j++) {
            const package_import_key *import = &generation->imports[j];
            if (!strcmp(import->identity, dependency->view.identity)) {
                diagnostic(generation, import->range, "Imported package is invalid");
                return;
            }
        }
        assert(!"dependency has no import");
    }
    script_package_options options = {
        .documents = documents,
        .document_count = generation->document_count,
        .identity = generation->view.identity,
        .native = &generation->native,
        .imports = imports,
        .import_count = import_count,
        .requests = generation->requests,
        .request_count = acount(generation->requests),
    };
    generation->view.analysis = script_analyze_package(&options);
    for (int i = 0; i < generation->view.analysis->diagnostic_count; i++) {
        const script_diagnostic *item = &generation->view.analysis->diagnostics[i];
        diagnostic(generation, item->range, item->message);
    }
    /* Publish declarations while requests are discovered. Only finalized valid
     * generations escape this preparation transaction. */
    catalog_members members = { 0 };
    catalog_append(&members, &generation->native);
    for (int i = 0; i < acount(generation->dependencies); i++) {
        const script_package_exports *exports = generation->dependencies[i]
                                                    ->view.exports;
        if (exports) {
            catalog_append(&members, exports->metadata);
        }
    }
    script_catalog metadata = { .types = members.types,
                                .type_count = acount(members.types),
                                .capabilities = members.capabilities,
                                .capability_count = acount(members.capabilities),
                                .entity_type = generation->native.entity_type,
                                .tick_event = generation->native.tick_event };
    generation->export_owner = source_package_exports_new(generation->view.analysis, &metadata, NULL);
    source_package_exports_bind_imports(generation->export_owner, imports, import_count);
    afree(members.types);
    afree(members.capabilities);
    afree(members.globals);
    afree(members.relationships);
    generation->view.exports = source_package_exports_view(generation->export_owner);
    bool ready = generation->view.analysis->valid && generation->view.exports;
    for (int i = 0; ready && i < generation->view.analysis->request_count; i++) {
        uint64_t id = generation->view.analysis->requests[i].id;
        bool found = false;
        for (int j = 0; j < acount(generation->dependencies); j++) {
            const script_package_exports *exports = generation->dependencies[j]
                                                        ->view.exports;
            for (int k = 0; exports && k < exports->metadata->specialization_count;
                 k++) {
                found |= exports->metadata->specializations[k].id == id;
            }
        }
        ready = found;
    }
    if (ready) {
        for (int i = 0; i < acount(generation->dependencies); i++) {
            ready &= generation->dependencies[i]->view.valid;
        }
    }
    if (ready) {
        lower_generation(generation, imports, import_count);
    }
}

/* Project packages are independent roots, including packages containing tests.
 * Without project files, the first input file selects the collection root.
 * Other collection packages contribute only through imports from those roots. */
static bool *needed_packages(const authoring_packages *owner, const source_packages *graph)
{
    int count = source_packages_count(graph);
    bool *needed = xcalloc((size_t)count, sizeof *needed);
    int *pending = NULL;
    for (int i = 0; i < count; i++) {
        const char *identity = source_package_snapshot_identity(owner->snapshot, i);
        if (!strncmp(identity, "project:", sizeof "project:" - 1)) {
            needed[i] = true;
            apush(pending, i);
        }
    }
    if (!acount(pending) && count) {
        int root = source_packages_file(graph, 0);
        needed[root] = true;
        apush(pending, root);
    }
    while (acount(pending)) {
        int from = pending[acount(pending) - 1];
        asetlen(pending, acount(pending) - 1);
        for (int to = 0; to < count; to++) {
            if (!needed[to] && source_packages_dependency(graph, from, to) >= 0) {
                needed[to] = true;
                apush(pending, to);
            }
        }
    }
    afree(pending);
    return needed;
}

authoring_packages *authoring_packages_prepare(
    source_package_snapshot *snapshot,
    const script_catalog *native,
    const script_lower_bindings *bindings,
    behavior_backend backend,
    const authoring_packages *previous
)
{
    assert(snapshot && native && bindings);
    authoring_packages *owner = xcalloc(1, sizeof *owner);
    owner->snapshot = snapshot;
    const source_packages *graph = source_package_snapshot_graph(snapshot);
    if (!graph) {
        const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
        assert(error);
        snprintf(owner->error, sizeof owner->error, "%s", error->diagnostic.message);
        return owner;
    }
    owner->count = source_packages_count(graph);
    owner->packages = xcalloc((size_t)owner->count, sizeof *owner->packages);
    bool *needed = needed_packages(owner, graph);
    for (int pass = 0; pass <= PACKAGE_SPECIALIZATIONS; pass++) {
        bool changed = false;
        owner->valid = true;
        for (int ordinal = 0; ordinal < owner->count; ordinal++) {
            int index = source_packages_order(graph, ordinal);
            if (!needed[index]) {
                continue;
            }
            script_package_document documents[SOURCE_PACKAGE_MAX_FILES];
            script_package_import *imports = NULL;
            package_generation *
                generation = generation_new(owner, index, native, bindings, backend, documents, &imports);
            package_generation
                *old = previous_package(previous, generation->view.identity);
            if (reusable(old, generation)) {
                release(generation);
                generation = retain(old);
            } else {
                prepare_generation(generation, documents, imports, acount(imports));
            }
            afree(imports);
            release(owner->packages[index]);
            owner->packages[index] = generation;
            owner->valid &= generation->view.valid;
            const script_analysis *analysis = generation->view.analysis;
            for (int i = 0; analysis && i < analysis->request_count; i++) {
                const script_specialization_request *input = &analysis->requests[i];
                bool known = false;
                for (int j = 0; j < acount(owner->requests); j++) {
                    if (owner->requests[j].id == input->id) {
                        known = true;
                        if (!same_request(&owner->requests[j], input)) {
                            snprintf(owner->error, sizeof owner->error, "Imported specialization identity collides with different arguments");
                        }
                        break;
                    }
                }
                if (known) {
                    continue;
                }
                if (acount(owner->requests) >= PACKAGE_SPECIALIZATIONS) {
                    snprintf(owner->error, sizeof owner->error, "Project polymorphic specialization budget exceeded");
                    continue;
                }
                apush(owner->requests, own_request(&owner->storage, input));
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    free(needed);
    owner->valid = !owner->error[0];
    for (int i = 0; i < owner->count; i++) {
        package_generation *generation = owner->packages[i];
        if (!generation) {
            continue;
        }
        const script_analysis *analysis = generation->view.analysis;
        if (!generation->view.valid && !generation->view.diagnostic_count) {
            bool reported = false;
            for (int j = 0; analysis && j < analysis->request_count; j++) {
                diagnostic(generation, analysis->requests[j].origin, "Imported template has no concrete specialization; inspect its defining package");
                reported = true;
            }
            if (!reported) {
                diagnostic(generation, (script_range){ .document = -1 }, "Imported package is invalid");
            }
        }
        owner->valid &= generation->view.valid;
        if (!generation->view.valid) {
            source_package_exports_free(generation->export_owner);
            generation->export_owner = NULL;
            generation->view.exports = NULL;
        }
        owner->reused += previous_package(previous, generation->view.identity) ==
                         generation;
    }
    /* Dependency construction above uses graph indices. Public enumeration
     * contains only prepared packages, in the graph's original index order. */
    int count = 0;
    for (int i = 0; i < owner->count; i++) {
        if (owner->packages[i]) {
            owner->packages[count++] = owner->packages[i];
        }
    }
    owner->count = count;
    return owner;
}

bool authoring_packages_valid(const authoring_packages *owner)
{
    return owner->valid;
}
int authoring_packages_count(const authoring_packages *owner)
{
    return owner->count;
}
int authoring_packages_reused(const authoring_packages *owner)
{
    return owner->reused;
}
const authoring_package *authoring_packages_at(const authoring_packages *owner, int package)
{
    return package >= 0 && package < owner->count && owner->packages[package]
             ? &owner->packages[package]->view
             : NULL;
}
const source_package_snapshot *authoring_packages_snapshot(const authoring_packages *owner)
{
    return owner->snapshot;
}
const char *authoring_packages_error(const authoring_packages *owner)
{
    return owner->error[0] ? owner->error : NULL;
}
void authoring_packages_free(authoring_packages *owner)
{
    if (!owner) {
        return;
    }
    for (int i = 0; i < owner->count; i++) {
        release(owner->packages[i]);
    }
    free(owner->packages);
    source_package_snapshot_free(owner->snapshot);
    afree(owner->requests);
    arena_free(&owner->storage);
    free(owner);
}
