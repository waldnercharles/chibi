#include "authoring/project_ui_bindings.h"

#include "assets/ui_document.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_CONTEXTS = 4096,
    MAX_WRITES = 4096,
    MAX_VISITS = 16384
};
typedef struct
{
    const script_analysis *analysis;
    int expression;
} data_write;
typedef struct
{
    const source_catalog_asset *asset;
    authoring_ui_binding *bindings;
    bool dynamic;
    data_write *writes;
} ui_context;
typedef struct
{
    const source_catalog *sources;
    const script_catalog *catalog;
    const script_analysis *const *analyses;
    int count, visits, write_count;
    const prototype_registry *registry;
    arena *storage;
    ui_context *contexts;
    char *error;
    size_t size;
} builder;
static bool fail(builder *b, const char *message)
{
    if (b->size) {
        snprintf(b->error, b->size, "%s", message);
    }
    return false;
}
static const char *own(arena *a, const char *s)
{
    char *r = arena_alloc(a, strlen(s) + 1);
    strcpy(r, s);
    return r;
}
static const char *literal(const script_analysis *a, int index, int depth)
{
    if (index < 0 || index >= a->expression_count || depth > 64) {
        return NULL;
    }
    const script_expression *e = &a->expressions[index];
    if (e->kind == SCRIPT_EXPR_CONSTANT &&
        (e->constant.type == PIN_STR || e->constant.type == PIN_NAME)) {
        return e->constant.str;
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 && e->symbol < a->symbol_count) {
        const script_symbol *s = &a->symbols[e->symbol];
        if (s->kind == SCRIPT_CONSTANT) {
            return literal(a, s->initializer, depth + 1);
        }
    }
    return NULL;
}
static bool self(const script_analysis *a, int index)
{
    if (index < 0) {
        return true;
    }
    if (index >= a->expression_count) {
        return false;
    }
    const script_expression *e = &a->expressions[index];
    return e->kind == SCRIPT_EXPR_SYMBOL && e->symbol == a->owner_symbol;
}
static bool put(builder *b, ui_context *context, const char *name, script_type type)
{
    if (!name || !*name || strlen(name) > 1024) {
        context->dynamic = true;
        return true;
    }
    for (int i = 0; i < acount(context->bindings); i++) {
        authoring_ui_binding *binding = &context->bindings[i];
        if (!strcmp(binding->name, name)) {
            binding->ambiguous |= !script_type_equal(binding->type, type);
            return true;
        }
    }
    if (acount(context->bindings) >= MAX_WRITES || ++b->write_count > 65536) {
        return fail(b, "Typed UI data path limit exceeded");
    }
    apush(context->bindings, ((authoring_ui_binding){ own(b->storage, name), type, false }));
    return true;
}
static bool writes(builder *b, ui_context *context, const char *const *graphs, int count)
{
    int capability = script_catalog_find_capability(b->catalog, "set_ui_data", SCRIPT_CAPABILITY_ACTION);
    if (capability < 0) {
        capability = script_catalog_find_capability(b->catalog, "Set UI Data", SCRIPT_CAPABILITY_ACTION);
    }
    if (capability < 0) {
        return true;
    }
    uint64_t id = b->catalog->capabilities[capability].id;
    for (int i = 0; i < count; i++) {
        const script_analysis *a = NULL;
        for (int j = 0; j < b->count; j++) {
            if (!strcmp(b->analyses[j]->name, graphs[i])) {
                a = b->analyses[j];
                break;
            }
        }
        if (!a) {
            continue;
        }
        for (int j = 0; j < a->expression_count; j++) {
            const script_expression *e = &a->expressions[j];
            if (e->kind != SCRIPT_EXPR_CALL || e->capability != id) {
                continue;
            }
            int entity = -1, name = -1, value = -1;
            for (int k = 0; k < e->argument_count; k++) {
                const script_argument *argument = &e->arguments[k];
                if (argument->parameter == 0) {
                    entity = argument->value;
                }
                if (argument->parameter == 1) {
                    name = argument->value;
                }
                if (argument->parameter == 2) {
                    value = argument->value;
                }
            }
            if (!self(a, entity)) {
                context->dynamic = true;
                continue;
            }
            if (value < 0 || value >= a->expression_count) {
                context->dynamic = true;
                continue;
            }
            if (!put(b, context, literal(a, name, 0), a->expressions[value].type)) {
                return false;
            }
            if (literal(a, name, 0)) {
                bool seen = false;
                for (int k = 0; k < acount(context->writes); k++) {
                    seen |= context->writes[k].analysis == a &&
                            context->writes[k].expression == name;
                }
                if (!seen) {
                    if (++b->write_count > 65536) {
                        return fail(b, "Typed UI writer limit exceeded");
                    }
                    apush(context->writes, ((data_write){ a, name }));
                }
            }
        }
    }
    return true;
}
static int context_index(builder *b, const char *name)
{
    for (int i = 0; i < acount(b->contexts); i++) {
        const char *identity = b->contexts[i].asset->identity;
        size_t length = strlen(identity), size = strlen(name);
        if (length == size + 7 && !strncmp(identity, "ui/", 3) &&
            !strncmp(identity + 3, name, size) &&
            !strcmp(identity + 3 + size, ".rml")) {
            return i;
        }
    }
    return -1;
}
static bool associate(builder *b, const char *name, const char *const *graphs, int count, bool *visited)
{
    int index = context_index(b, name);
    if (index < 0 || visited[index]) {
        return true;
    }
    visited[index] = true;
    ui_context *context = &b->contexts[index];
    if (!writes(b, context, graphs, count)) {
        return false;
    }
    char **uses = NULL;
    ui_document_use_names(context->asset->text, &uses);
    bool ok = true;
    for (int i = 0; i < acount(uses); i++) {
        if (ok) {
            ok = associate(b, uses[i], graphs, count, visited);
        }
        free(uses[i]);
    }
    afree(uses);
    return ok;
}
static bool visit(builder *b, const prototype *p, const char *const *override, int count, bool replace, int depth)
{
    if (depth > 256 || ++b->visits > MAX_VISITS) {
        return fail(b, "Typed UI prototype association limit exceeded");
    }
    for (int i = 0; i < acount(p->components); i++) {
        const prototype_component *c = &p->components[i];
        if (!c->desc->name || strcmp(c->desc->name, "Ui") || !c->desc->type || !c->data) {
            continue;
        }
        for (int j = 0; j < c->desc->type->field_count; j++) {
            const type_field *field = &c->desc->type->fields[j];
            if (strcmp(field->name, "document") ||
                (field->type != PIN_STR && field->type != PIN_NAME)) {
                continue;
            }
            pin_value name = { 0 };
            pin_read_field(c->data, field, &name);
            if (name.str && *name.str) {
                bool *visited = calloc((size_t)acount(b->contexts), sizeof *visited);
                bool ok = associate(
                    b,
                    name.str,
                    replace ? override : (const char *const *)p->graphs,
                    replace ? count : acount(p->graphs),
                    visited
                );
                free(visited);
                if (!ok) {
                    return false;
                }
            }
        }
    }
    for (int i = 0; i < acount(p->child_prototypes); i++) {
        if (!visit(b, p->child_prototypes[i], NULL, 0, false, depth + 1)) {
            return false;
        }
    }
    return true;
}
static bool entities(builder *b, const scene_document *scene)
{
    for (int i = 0; i < acount(scene->entities); i++) {
        const scene_entity *entity = &scene->entities[i];
        prototype *p = NULL;
        if (entity->prototype && *entity->prototype) {
            const char *text = b->registry->include
                                 ? b->registry
                                       ->include(entity->prototype, b->registry->include_user)
                                 : NULL;
            if (!text) {
                return fail(b, "Typed UI association cannot resolve an entity prototype");
            }
            p = prototype_load(text, b->registry, b->error, b->size);
        } else {
            p = prototype_new(entity->name);
            p->registry_storage = *b->registry;
            p->registry = &p->registry_storage;
        }
        if (!p) {
            return false;
        }
        bool ok = !prototype_apply_entity_components(p, entity->components, b->error, b->size) &&
                  visit(
                      b,
                      p,
                      (const char *const *)entity->behaviors,
                      acount(entity->behaviors),
                      entity->own_behaviors,
                      0
                  );
        prototype_free(p);
        if (!ok) {
            return false;
        }
    }
    return true;
}

static int constant_index(const script_analysis *a, int index, int depth)
{
    if (index < 0 || index >= a->expression_count || depth > 64) {
        return -1;
    }
    const script_expression *e = &a->expressions[index];
    if (e->kind == SCRIPT_EXPR_CONSTANT) {
        return index;
    }
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 && e->symbol < a->symbol_count &&
        a->symbols[e->symbol].kind == SCRIPT_CONSTANT) {
        return constant_index(a, a->symbols[e->symbol].initializer, depth + 1);
    }
    return -1;
}
static bool path_references(
    builder *b,
    ui_context *context,
    source_catalog_reference **resolved,
    project_ui_unresolved **uncertain
)
{
    for (int i = 0; i < acount(context->writes); i++) {
        const script_analysis *a = context->writes[i].analysis;
        int expression = constant_index(a, context->writes[i].expression, 0);
        const char *name = literal(a, context->writes[i].expression, 0);
        if (expression < 0 || !name) {
            continue;
        }
        const source_catalog_source *source = NULL;
        for (int j = 0; j < source_catalog_source_count(b->sources); j++) {
            const source_catalog_source *candidate = source_catalog_source_at(b->sources, j);
            if (!strcmp(candidate->identity, a->identity)) {
                source = candidate;
                break;
            }
        }
        if (!source) {
            return fail(b, "Typed UI data source identity is unavailable");
        }
        script_range raw = a->expressions[expression].range;
        size_t length = strlen(name);
        if (length > 1024) {
            continue;
        }
        bool exact = raw.end - raw.start == (int)length + 2 &&
                     !strncmp(a->text + raw.start + 1, name, length);
        script_type receiver = { 0 };
        bool known = false, ambiguous = context->dynamic;
        for (size_t start = 0; start < length;) {
            const char *dot = strchr(name + start, '.');
            size_t end = dot ? (size_t)(dot - name) : length;
            char field[1025];
            memcpy(field, name + start, end - start);
            field[end - start] = 0;
            if (!known) {
                for (int j = 0; j < acount(context->bindings); j++) {
                    const authoring_ui_binding *binding = &context->bindings[j];
                    if (strlen(binding->name) == end &&
                        !strncmp(name, binding->name, end)) {
                        receiver = binding->type;
                        known = true;
                        ambiguous |= binding->ambiguous;
                        break;
                    }
                }
            } else {
                bool numeric = *field >= '0' && *field <= '9';
                for (const char *p = field; numeric && *p; p++) {
                    numeric = *p >= '0' && *p <= '9';
                }
                if (receiver.container != PIN_CONTAINER_SINGLE && numeric) {
                    receiver.container = PIN_CONTAINER_SINGLE;
                } else {
                    const script_catalog_type
                        *record = script_catalog_type_by_id(b->catalog, receiver.nominal);
                    const script_catalog_field *member = NULL;
                    for (int j = 0; record && j < record->field_count; j++) {
                        if (!strcmp(record->fields[j].name, field)) {
                            member = &record->fields[j];
                            break;
                        }
                    }
                    if (member) {
                        script_range range = raw;
                        range.start = raw.start + 1 + (int)start;
                        range.end = raw.start + 1 + (int)end;
                        range.column = raw.column + 1 + (int)start;
                        const char *owned = own(b->storage, field);
                        if (exact && !ambiguous) {
                            apush(
                                *resolved,
                                ((source_catalog_reference){
                                    .id = member->id,
                                    .owner = record->id,
                                    .kind = SOURCE_REFERENCE_FIELD,
                                    .name = owned,
                                    .path = source->path,
                                    .text = source->text,
                                    .range = range,
                                    .text_kind = SOURCE_TEXT_CHIBI })
                            );
                        } else {
                            apush(
                                *uncertain,
                                ((project_ui_unresolved){ source->path,
                                                          { .name = owned,
                                                            .owner = record->id,
                                                            .field = member->id,
                                                            .range = range,
                                                            .unresolved = true } })
                            );
                        }
                        receiver = member->type;
                    } else {
                        known = false;
                    }
                }
            }
            if (acount(*resolved) + acount(*uncertain) > 65536) {
                return fail(b, "Typed UI reference limit exceeded");
            }
            start = end + 1;
        }
    }
    return true;
}
static int reference_order(const void *left, const void *right)
{
    const source_catalog_reference *a = left, *b = right;
    int order = strcmp(a->path, b->path);
    if (order) {
        return order;
    }
    if (a->range.start != b->range.start) {
        return a->range.start < b->range.start ? -1 : 1;
    }
    if (a->id != b->id) {
        return a->id < b->id ? -1 : 1;
    }
    return 0;
}
bool project_ui_bindings(
    const source_catalog *sources,
    const script_analysis *const *analyses,
    int count,
    const prototype_registry *registry,
    arena *storage,
    source_catalog_reference **references,
    int *reference_count,
    project_ui_unresolved **uncertain,
    int *uncertain_count,
    char *error,
    size_t size
)
{
    *references = NULL;
    *uncertain = NULL;
    *reference_count = *uncertain_count = 0;
    if (size) {
        *error = 0;
    }
    builder b = { .sources = sources,
                  .catalog = source_catalog_view(sources),
                  .analyses = analyses,
                  .count = count,
                  .registry = registry,
                  .storage = storage,
                  .error = error,
                  .size = size };
    for (int i = 0; i < source_catalog_asset_count(sources); i++) {
        const source_catalog_asset *asset = source_catalog_asset_at(sources, i);
        if (!strncmp(asset->identity, "ui/", 3)) {
            apush(b.contexts, ((ui_context){ .asset = asset }));
        }
    }
    bool ok = acount(b.contexts) <= MAX_CONTEXTS;
    if (!ok) {
        fail(&b, "Typed UI document limit exceeded");
    }
    for (int i = 0; ok && registry && i < source_catalog_asset_count(sources); i++) {
        const source_catalog_asset *asset = source_catalog_asset_at(sources, i);
        if (!strncmp(asset->identity, "prototypes/", 11)) {
            prototype *p = prototype_load(asset->text, registry, error, size);
            ok = p && visit(&b, p, NULL, 0, false, 0);
            prototype_free(p);
        } else if (!strncmp(asset->identity, "scenes/", 7)) {
            scene_document *scene = scene_document_load(asset->text, error, size);
            ok = scene && entities(&b, scene);
            scene_document_free(scene);
        }
    }
    source_catalog_reference *resolved = NULL;
    project_ui_unresolved *unresolved = NULL;
    for (int i = 0; ok && i < acount(b.contexts); i++) {
        ui_context *context = &b.contexts[i];
        if (context->dynamic) {
            for (int j = 0; j < acount(context->bindings); j++) {
                context->bindings[j].ambiguous = true;
            }
        }
        if (!path_references(&b, context, &resolved, &unresolved)) {
            ok = false;
            break;
        }
        ui_source *source = ui_source_parse(context->asset->text, UI_SOURCE_RML);
        authoring_ui_binding_reference *rows = NULL;
        int rows_count = 0;
        ok = source && authoring_ui_binding_references(
                           source,
                           b.catalog,
                           context->bindings,
                           acount(context->bindings),
                           storage,
                           &rows,
                           &rows_count,
                           error,
                           size
                       );
        if (acount(resolved) + acount(unresolved) + rows_count > 65536) {
            ok = fail(&b, "Typed UI reference limit exceeded");
        }
        for (int j = 0; ok && j < rows_count; j++) {
            if (rows[j].unresolved) {
                apush(unresolved, ((project_ui_unresolved){ context->asset->path, rows[j] }));
            } else {
                apush(
                    resolved,
                    ((source_catalog_reference){ .id = rows[j].field,
                                                 .owner = rows[j].owner,
                                                 .kind = SOURCE_REFERENCE_FIELD,
                                                 .name = rows[j].name,
                                                 .path = context->asset->path,
                                                 .text = context->asset->text,
                                                 .range = rows[j].range,
                                                 .text_kind = SOURCE_TEXT_RML })
                );
            }
        }
        ui_source_free(source);
    }
    if (ok) {
        qsort(resolved, (size_t)acount(resolved), sizeof *resolved, reference_order);
        int unique = 0;
        for (int i = 0; i < acount(resolved); i++) {
            if (!unique || reference_order(&resolved[unique - 1], &resolved[i])) {
                resolved[unique++] = resolved[i];
            }
        }
        *reference_count = unique;
        *uncertain_count = acount(unresolved);
        *references = arena_alloc(storage, (size_t)*reference_count * sizeof **references);
        *uncertain = arena_alloc(storage, (size_t)*uncertain_count * sizeof **uncertain);
        memcpy(*references, resolved, (size_t)*reference_count * sizeof **references);
        memcpy(*uncertain, unresolved, (size_t)*uncertain_count * sizeof **uncertain);
    }
    for (int i = 0; i < acount(b.contexts); i++) {
        afree(b.contexts[i].bindings);
        afree(b.contexts[i].writes);
    }
    afree(b.contexts);
    afree(resolved);
    afree(unresolved);
    return ok;
}
