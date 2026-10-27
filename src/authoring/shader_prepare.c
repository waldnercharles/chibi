#include "authoring/shader_prepare.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/value_copy.h"
#include "graph/shader/shader_graph.h"
#include "render/shader_catalog_records.h"
#include "render/shader_values.h"
#include "runtime/component_catalog.h"
#include "runtime/images/catalog.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool authoring_binding_parameter_values_from(
    const shader_asset *,
    const shader_asset *,
    const shader_asset *const *,
    int,
    const char *,
    float[32][4],
    float[32][4],
    float[MODEL_MATERIAL_SLOT_MAX + 1][4],
    shader_error *
);

typedef struct
{
    av_value *value;
    behavior_asset *model;
    char *library;
} shader_source;
static shader_source *source_records;
static void source_free(shader_source *source)
{
    av_value_free(source->value);
    behavior_asset_free(source->model);
    free(source->library);
    *source = (shader_source){ 0 };
}
static shader_source *source_at(int index)
{
    assert(index >= 0 && index < acount(shader_catalog_assets));
    while (acount(source_records) < acount(shader_catalog_assets)) {
        apush(source_records, ((shader_source){ 0 }));
    }
    return &source_records[index];
}
static void source_set(int index, shader_source source)
{
    shader_source *before = source_at(index);
    source_free(before);
    *before = source;
}
void authoring_shader_catalog_clear()
{
    for (int i = 0; i < acount(source_records); i++) {
        source_free(&source_records[i]);
    }
    afree(source_records);
    render_shader_catalog_clear();
}

static void append(char **text, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    size_t old = *text ? strlen(*text) : 0;
    *text = xrealloc(*text, old + (size_t)count + 1);
    vsnprintf(*text + old, (size_t)count + 1, format, args);
    va_end(args);
}
static bool particle_function(const graph *g)
{
    if (g->kind != GRAPH_FUNCTION || g->source_asset) {
        return false;
    }
    const node *ret = graph_node_by_id(g, g->return_id);
    unsigned names = 0;
    for (int i = 0; ret && i < ret->input_count; i++) {
        const pin_spec *pin = &ret->input_pins[i];
        if (pin->kind != PIN_DATA || !pin->name) {
            continue;
        }
        names |= !strcmp(pin->name, "Position") ? 1u
               : !strcmp(pin->name, "Size")     ? 2u
               : !strcmp(pin->name, "Color")    ? 4u
                                                : 0;
    }
    return names == 7;
}
static int particle_pin_width(const pin_spec *pin)
{
    if (pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE) {
        return 0;
    }
    if (pin->type == PIN_F32) {
        return 1;
    }
    if (pin->type != PIN_STRUCT || !pin->type_name) {
        return 0;
    }
    return !strcmp(pin->type_name, "ShaderVec2") ? 2
         : !strcmp(pin->type_name, "ShaderVec3") ? 3
         : !strcmp(pin->type_name, "ShaderVec4") ? 4
                                                 : 0;
}
static bool particle_asset_prepare(const graph *g, shader_asset *fresh, shader_error *error)
{
    const node *entry = graph_node_by_id(g, g->entry_id);
    const node *ret = graph_node_by_id(g, g->return_id);
    static const char *const outputs[] = { "Position", "Size", "Color" };
    static const int widths[] = { 3, 1, 4 };
    bool signature = entry && ret && ret->input_count == 3 &&
                     entry->output_count >= 3 &&
                     entry->output_count <= SHADER_PARAMETER_MAX;
    for (int i = 0; signature && i < 3; i++) {
        const pin_spec *pin = &ret->input_pins[i];
        signature = pin->name && !strcmp(pin->name, outputs[i]) &&
                    particle_pin_width(pin) == widths[i];
    }
    for (int i = 0; signature && i < entry->output_count; i++) {
        signature = particle_pin_width(&entry->output_pins[i]) > 0;
    }
    if (!signature) {
        if (error) {
            error->node = ret ? ret->id : g->return_id;
            snprintf(error->message, sizeof error->message, "Particle function needs 3..32 number/vector arguments and Position:ShaderVec3, Size:number, Color:ShaderVec4 results");
        }
        return false;
    }
    if (!shader_graph_from_function(g, &fresh->graph, error) ||
        !shader_graph_particle_validate(&fresh->graph, error)) {
        return false;
    }
    append(&fresh->name, "%s.%s", g->owner->name, g->name);
    for (int i = 0; i < entry->output_count; i++) {
        const pin_spec *pin = &entry->output_pins[i];
        fresh->names[i] = pin->name ? xstrdup(pin->name) : NULL;
        fresh->widths[i] = particle_pin_width(pin);
    }
    for (int i = 0; i < acount(fresh->graph.nodes); i++) {
        const shader_node *n = &fresh->graph.nodes[i];
        if (n->op == SHADER_PARAMETER) {
            memcpy(fresh->defaults[n->parameter], n->value, sizeof n->value);
        }
    }
    return shader_particle_parameters_validate(fresh->names, fresh->widths, error);
}
static bool library_prepare(const behavior_asset *source, bool publish, shader_error *error)
{
    if (!source || source->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return false;
    }
    shader_asset *pending = NULL;
    int *ids = NULL;
    int next = acount(shader_catalog_assets) + 1;
    bool valid = false;
    for (int i = 1; i < acount(source->graphs); i++) {
        const graph *g = source->graphs[i];
        if (!particle_function(g)) {
            continue;
        }
        shader_asset fresh = { 0 };
        if (!particle_asset_prepare(g, &fresh, error)) {
            shader_catalog_free_asset(&fresh);
            goto done;
        }
        fresh.material_inspection = shader_graph_inspect(&fresh.graph);
        int id = render_shader_find(fresh.name);
        if (id && !render_shader_is_particle(id)) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Particle and shader asset names must be unique: %s",
                    fresh.name
                );
            }
            shader_catalog_free_asset(&fresh);
            goto done;
        }
        id = id ? id : next++;
        char name[64];
        snprintf(name, sizeof name, "sg_asset_%d", id);
        fresh.glsl = shader_graph_glsl_named(&fresh.graph, name, error);
        if (!fresh.glsl) {
            shader_catalog_free_asset(&fresh);
            goto done;
        }
        apush(pending, fresh);
        apush(ids, id);
    }
    if (!publish) {
        valid = true;
        goto done;
    }
    for (int i = 0; i < acount(pending); i++) {
        if (ids[i] <= acount(shader_catalog_assets)) {
            shader_catalog_free_asset(&shader_catalog_assets[ids[i] - 1]);
            shader_catalog_assets[ids[i] - 1] = pending[i];
        } else {
            apush(shader_catalog_assets, pending[i]);
        }
        pending[i] = (shader_asset){ 0 };
        source_set(ids[i] - 1, (shader_source){ .library = xstrdup(source->name) });
    }
    bool removed = false;
    for (int i = 0; i < acount(source_records); i++) {
        if (!source_records[i].library ||
            strcmp(source_records[i].library, source->name)) {
            continue;
        }
        bool retained = false;
        for (int j = 0; j < acount(ids); j++) {
            retained |= ids[j] == i + 1;
        }
        if (!retained) {
            shader_catalog_free_asset(&shader_catalog_assets[i]);
            shader_catalog_assets[i] = (shader_asset){ .name = xstrdup("") };
            source_free(&source_records[i]);
            removed = true;
        }
    }
    if (acount(ids) || removed) {
        shader_catalog_revision++;
        shader_catalog_parameter_revision++;
    }
    valid = true;
done:
    for (int i = 0; i < acount(pending); i++) {
        shader_catalog_free_asset(&pending[i]);
    }
    afree(pending);
    afree(ids);
    return valid;
}
bool render_shader_library_validate(const behavior_asset *source, shader_error *error)
{
    return library_prepare(source, false, error);
}
bool render_shader_library_upsert(const behavior_asset *source, shader_error *error)
{
    return library_prepare(source, true, error);
}
void render_shader_library_remove(const char *name)
{
    bool removed = false;
    for (int i = 0; i < acount(source_records); i++) {
        if (!source_records[i].library || strcmp(source_records[i].library, name)) {
            continue;
        }
        shader_catalog_free_asset(&shader_catalog_assets[i]);
        shader_catalog_assets[i] = (shader_asset){ .name = xstrdup("") };
        source_free(&source_records[i]);
        removed = true;
    }
    if (removed) {
        shader_catalog_revision++;
        shader_catalog_parameter_revision++;
    }
}
bool render_shader_upsert(const behavior_asset *source, shader_error *error)
{
    if (source && source->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        return render_shader_library_upsert(source, error);
    }
    if (source && source->domain == BEHAVIOR_DOMAIN_MODEL) {
        return render_shader_models_upsert(&source, 1, error);
    }
    if (!source || (source->domain != BEHAVIOR_DOMAIN_MATERIAL &&
                    source->domain != BEHAVIOR_DOMAIN_SHAPE)) {
        return false;
    }
    if (source->domain == BEHAVIOR_DOMAIN_SHAPE &&
        (!(source->bounds[0] > 0) || !(source->bounds[1] > 0) ||
         !isfinite(source->bounds[0]) || !isfinite(source->bounds[1]))) {
        if (error) {
            error->node = 0;
            snprintf(error->message, sizeof error->message, "Shape bounds must be finite positive half extents.");
        }
        return false;
    }
    av_doc *document = av_doc_new();
    av_value *value = behavior_asset_to_value(document, source);
    av_doc_set_root(document, value);
    int existing = render_shader_find(source->name);
    if (existing && (shader_catalog_assets[existing - 1].is_model ||
                     render_shader_is_particle(existing))) {
        if (error) {
            snprintf(
                error->message,
                sizeof error->message,
                "Model and shader asset names must be unique: %s",
                source->name
            );
        }
        av_value_free(value);
        return false;
    }
    if (existing && av_equals(value, source_at(existing - 1)->value)) {
        av_value_free(value);
        return true;
    }
    shader_asset fresh = { .shape = source->domain == BEHAVIOR_DOMAIN_SHAPE };
    model_program_init(&fresh.model);
    if (!shader_graph_from_graph(source->graph, &fresh.graph, error)) {
        shader_catalog_free_asset(&fresh);
        av_value_free(value);
        return false;
    }
    fresh.alpha_mode = shader_graph_alpha_mode(source->graph);
    fresh.material_inspection = shader_graph_inspect(&fresh.graph);
    fresh.name = xstrdup(source->name);
    for (int i = 0; i < acount(fresh.graph.images); i++) {
        int variant = render_image_variant(&fresh.graph.images[i], error);
        if (!variant) {
            if (error) {
                for (int n = 0; n < acount(fresh.graph.nodes); n++) {
                    const shader_node *node = &fresh.graph.nodes[n];
                    if ((node->op == SHADER_IMAGE_TEXTURE ||
                         node->op == SHADER_IMAGE_TEXTURE_ALPHA ||
                         node->op == SHADER_TRIPLANAR_TEXTURE ||
                         node->op == SHADER_TRIPLANAR_TEXTURE_ALPHA) &&
                        node->image == i) {
                        error->node = node->source_id;
                        break;
                    }
                }
            }
            shader_catalog_free_asset(&fresh);
            av_value_free(value);
            return false;
        }
        apush(fresh.images, variant);
    }
    if (acount(fresh.images) > RENDER_SHADER_IMAGE_MAX) {
        if (error) {
            snprintf(
                error->message,
                sizeof error->message,
                "Material %s exceeds %d image variants per program.",
                source->name,
                RENDER_SHADER_IMAGE_MAX
            );
        }
        shader_catalog_free_asset(&fresh);
        av_value_free(value);
        return false;
    }
    memcpy(fresh.bounds, source->bounds, sizeof fresh.bounds);
    int id = render_shader_find(source->name);
    if (!id) {
        id = acount(shader_catalog_assets) + 1;
    }
    char name[64];
    snprintf(name, sizeof name, "sg_asset_%d", id);
    fresh.glsl = shader_graph_glsl_named_ramps(&fresh.graph, name, 0, error);
    if (!fresh.glsl) {
        shader_catalog_free_asset(&fresh);
        av_value_free(value);
        return false;
    }
    for (int i = 0; i < acount(fresh.graph.nodes); i++) {
        shader_node *n = &fresh.graph.nodes[i];
        if (n->op != SHADER_PARAMETER) {
            continue;
        }
        node *authored = graph_node_by_id(source->graph, n->source_id);
        const char *label = authored && authored->reference_name ? authored->reference_name
                                                                 : "";
        free(fresh.names[n->parameter]);
        fresh.names[n->parameter] = xstrdup(label);
        fresh.widths[n->parameter] = n->width;
        memcpy(fresh.defaults[n->parameter], n->value, sizeof n->value);
    }
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (shader_catalog_assets[i].is_model &&
            !shader_catalog_model_material_contract_validate(&shader_catalog_assets[i], id, &fresh, error)) {
            shader_catalog_free_asset(&fresh);
            av_value_free(value);
            return false;
        }
    }
    if (id <= acount(shader_catalog_assets)) {
        /* Avoid recompiling an unchanged graph on every editor frame. */
        shader_asset *old = &shader_catalog_assets[id - 1];
        bool names_equal = true;
        for (int slot = 0; slot < 32; slot++) {
            const char *a = old->names[slot], *b = fresh.names[slot];
            names_equal &= (!a && !b) || (a && b && !strcmp(a, b));
        }
        bool images_equal = acount(old->images) == acount(fresh.images) &&
                            (!acount(fresh.images) ||
                             !memcmp(
                                 old->images,
                                 fresh.images,
                                 (size_t)acount(fresh.images) * sizeof *fresh.images
                             ));
        if (old->shape == fresh.shape && names_equal && images_equal &&
            !strcmp(old->glsl, fresh.glsl) &&
            !memcmp(old->defaults, fresh.defaults, sizeof old->defaults) &&
            !memcmp(old->bounds, fresh.bounds, sizeof old->bounds)) {
            /* Ramp stop values are uploaded as a live payload. Keep the
             * compiled program shader_catalog_revision when only those values changed. */
            shader_catalog_free_asset(old);
            *old = fresh;
            source_set(id - 1, (shader_source){ .value = value });
            shader_catalog_ramp_revision++;
            return true;
        }
        shader_catalog_free_asset(old);
        *old = fresh;
    } else {
        apush(shader_catalog_assets, fresh);
    }
    source_set(id - 1, (shader_source){ .value = value });
    shader_catalog_revision++;
    shader_catalog_ramp_revision++;
    shader_catalog_parameter_revision++;
    return true;
}

static bool model_asset_prepare(
    const behavior_asset *source,
    int id,
    const behavior_asset *const *catalog,
    int count,
    shader_asset *fresh,
    shader_error *error
)
{
    *fresh = (shader_asset){ .shape = true, .is_model = true };
    model_program_init(&fresh->model);
    fresh->name = xstrdup(source->name);
    if (!model_compile_with_catalog(source, catalog, count, &fresh->model, error)) {
        shader_catalog_free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    model_program_inspect(&fresh->model, &fresh->inspection);
    fresh->inspection.image_limit = RENDER_SHADER_IMAGE_MAX;
    fresh->inspection.ramp_limit = RENDER_SHADER_RAMP_VEC4_MAX;
    if (!render_image_validate_volumes(&fresh->model.shader, error)) {
        shader_catalog_free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    fresh->model.resolve_image = render_image_resolve;
    for (int i = 0; i < acount(fresh->model.shader.images); i++) {
        int variant = render_image_variant(&fresh->model.shader.images[i], error);
        if (!variant || acount(fresh->images) >= RENDER_SHADER_IMAGE_MAX) {
            shader_catalog_free_asset(fresh);
            *fresh = (shader_asset){ 0 };
            return false;
        }
        apush(fresh->images, variant);
    }
    fresh->graph = fresh->model.shader;
    shader_graph_init(&fresh->model.shader);
    model_bounds bounds;
    if (!model_program_bounds(&fresh->model, NULL, &bounds, error)) {
        shader_catalog_free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    fresh->bounds[0] = fmaxf(fabsf(bounds.min[0]), fabsf(bounds.max[0]));
    fresh->bounds[1] = fmaxf(fabsf(bounds.min[2]), fabsf(bounds.max[2]));
    char name[64];
    snprintf(name, sizeof name, "sg_asset_%d", id);
    fresh->glsl = shader_graph_glsl_named_ramps(&fresh->graph, name, 0, error);
    if (!fresh->glsl) {
        shader_catalog_free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    snprintf(name, sizeof name, "sg_asset_%d_hit", id);
    char *hit = shader_graph_glsl_named(&fresh->model.hit_shader, name, error);
    if (!hit) {
        shader_catalog_free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    append(&fresh->glsl, "%s", hit);
    free(hit);
    for (int i = 0; i < acount(fresh->model.parameters); i++) {
        const model_parameter *parameter = &fresh->model.parameters[i];
        int slot = parameter->slot;
        memcpy(fresh->defaults[slot], parameter->defaults, sizeof parameter->defaults);
        if (parameter->hidden) {
            continue;
        }
        fresh->names[slot] = xstrdup(parameter->name);
        fresh->widths[slot] = parameter->width;
    }
    return true;
}

static void model_catalog_materials(const behavior_asset ***catalog, behavior_asset **stubs)
{
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        const shader_asset *asset = &shader_catalog_assets[i];
        if (asset->name[0] && !asset->shape &&
            asset->graph.output_count == SHADER_OUTPUT_MAX) {
            apush(*stubs, ((behavior_asset){ .name = asset->name, .domain = BEHAVIOR_DOMAIN_MATERIAL }));
        }
    }
    for (int i = 0; i < acount(*stubs); i++) {
        apush(*catalog, &(*stubs)[i]);
    }
}

typedef struct
{
    int id;
    bool bindings_only;
    shader_asset asset;
    shader_source source;
    model_material_bindings bindings;
} model_pending;

static void model_pending_free(model_pending *pending)
{
    source_free(&pending->source);
    if (pending->bindings_only) {
        free(pending->asset.name);
        model_material_bindings_free(&pending->bindings);
        return;
    }
    shader_catalog_free_asset(&pending->asset);
}

static bool model_generation_publish(
    const behavior_asset *const *catalog,
    int count,
    int removed,
    const char *renamed,
    int rename_id,
    shader_error *error
)
{
    model_pending *prepared = NULL;
    const behavior_asset **mixed = NULL;
    behavior_asset *material_stubs = NULL;
    for (int i = 0; i < count; i++) {
        apush(mixed, catalog[i]);
    }
    model_catalog_materials(&mixed, &material_stubs);
    int next = acount(shader_catalog_assets) + 1, compilations = 0;
    bool ok = true, changed = removed > 0;
    for (int i = 0; i < count; i++) {
        int id = renamed && !strcmp(catalog[i]->name, renamed)
                   ? rename_id
                   : render_shader_find(catalog[i]->name);
        if (id && !render_shader_is_model(id)) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Model and shader asset names must be unique: %s",
                    catalog[i]->name
                );
            }
            ok = false;
            break;
        }
        model_pending pending = { .id = id ? id : next++ };
        model_material_bindings_init(&pending.bindings);
        model_bindings_result bindings = id && !renamed
                                           ? model_program_compile_bindings(
                                                 &shader_catalog_assets[id - 1].model,
                                                 catalog[i],
                                                 mixed,
                                                 acount(mixed),
                                                 &pending.bindings,
                                                 error
                                             )
                                           : MODEL_BINDINGS_GEOMETRY_CHANGED;
        if (bindings == MODEL_BINDINGS_INVALID) {
            model_material_bindings_free(&pending.bindings);
            ok = false;
            break;
        }
        if (bindings == MODEL_BINDINGS_READY) {
            pending.bindings_only = true;
            pending.asset = shader_catalog_assets[id - 1];
            pending.asset.name = xstrdup(catalog[i]->name);
            pending.asset.model.material_slots = pending.bindings.slots;
            pending.asset.model.material_dependencies = pending.bindings.dependencies;
            for (int j = 0; j < acount(shader_catalog_assets[id - 1].model.parts); j++) {
                changed |= shader_catalog_assets[id - 1].model.parts[j].material !=
                           pending.bindings.part_slots[j];
            }
        } else {
            if (!model_asset_prepare(
                    catalog[i],
                    pending.id,
                    mixed,
                    acount(mixed),
                    &pending.asset,
                    error
                )) {
                ok = false;
                break;
            }
            compilations++;
            changed |= !id ||
                       strcmp(shader_catalog_assets[id - 1].glsl, pending.asset.glsl) != 0;
        }
        if (!shader_catalog_model_material_contract_validate(&pending.asset, 0, NULL, error)) {
            model_pending_free(&pending);
            ok = false;
            break;
        }
        pending.source.model = behavior_asset_clone(catalog[i]);
        av_doc *doc = av_doc_new();
        pending.source.value = behavior_asset_to_value(doc, catalog[i]);
        av_doc_set_root(doc, pending.source.value);
        apush(prepared, pending);
    }
    if (!ok) {
        for (int i = 0; i < acount(prepared); i++) {
            model_pending_free(&prepared[i]);
        }
        afree(prepared);
        afree(mixed);
        afree(material_stubs);
        return false;
    }
    shader_model_migration *migrations = NULL;
    const char *renamed_from = rename_id ? shader_catalog_assets[rename_id - 1].name
                                         : NULL;
    for (int i = 0; i < count; i++) {
        if (prepared[i].id <= acount(shader_catalog_assets) &&
            !prepared[i].bindings_only) {
            apush(
                migrations,
                ((shader_model_migration){ prepared[i].id, &prepared[i].asset })
            );
        }
    }
    shader_catalog_model_overrides_migrate(migrations, acount(migrations), renamed_from, renamed);
    afree(migrations);
    for (int i = 0; i < count; i++) {
        model_pending *pending = &prepared[i];
        if (pending->bindings_only) {
            shader_asset *old = &shader_catalog_assets[pending->id - 1];
            model_material_bindings discarded = {
                .slots = old->model.material_slots,
                .dependencies = old->model.material_dependencies
            };
            model_material_bindings_free(&discarded);
            free(old->name);
            old->name = pending->asset.name;
            old->model.material_slots = pending->bindings.slots;
            old->model.material_dependencies = pending->bindings.dependencies;
            old->inspection.material_slots = acount(old->model.material_slots);
            for (int j = 0; j < acount(old->model.parts); j++) {
                old->model.parts[j].material = pending->bindings.part_slots[j];
                old->inspection.parts[j].material = pending->bindings.part_slots[j];
            }
            pending->bindings.slots = NULL;
            pending->bindings.dependencies = NULL;
            model_material_bindings_free(&pending->bindings);
        } else if (pending->id <= acount(shader_catalog_assets)) {
            shader_catalog_free_asset(&shader_catalog_assets[pending->id - 1]);
            shader_catalog_assets[pending->id - 1] = pending->asset;
        } else {
            assert(pending->id == acount(shader_catalog_assets) + 1);
            apush(shader_catalog_assets, pending->asset);
        }
        source_set(pending->id - 1, pending->source);
    }
    if (removed) {
        source_free(source_at(removed - 1));
        shader_catalog_free_asset(&shader_catalog_assets[removed - 1]);
        shader_catalog_assets[removed - 1] = (shader_asset){ .name = xstrdup("") };
    }
    if (changed) {
        shader_catalog_revision++;
        shader_catalog_ramp_revision++;
    }
    shader_catalog_parameter_revision++;
    shader_catalog_model_stats.compilations += compilations;
    afree(prepared);
    afree(mixed);
    afree(material_stubs);
    return true;
}

bool render_shader_models_upsert(const behavior_asset *const *sources, int count, shader_error *error)
{
    const behavior_asset **catalog = NULL;
    bool unchanged = true;
    for (int i = 0; i < count; i++) {
        if (!sources[i] || sources[i]->domain != BEHAVIOR_DOMAIN_MODEL) {
            if (error) {
                snprintf(error->message, sizeof error->message, "Model catalog requires model shader_catalog_assets.");
            }
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(sources[i]->name, sources[j]->name)) {
                if (error) {
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "Ambiguous model name: %s",
                        sources[i]->name
                    );
                }
                return false;
            }
        }
        int id = render_shader_find(sources[i]->name);
        unchanged &= id && render_shader_is_model(id) &&
                     behavior_asset_equal(sources[i], source_at(id - 1)->model);
    }
    if (unchanged) {
        return true;
    }
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (!shader_catalog_assets[i].is_model) {
            continue;
        }
        const behavior_asset *source = source_at(i)->model;
        for (int j = 0; j < count; j++) {
            if (!strcmp(source->name, sources[j]->name)) {
                source = sources[j];
                break;
            }
        }
        apush(catalog, source);
    }
    for (int i = 0; i < count; i++) {
        if (!render_shader_find(sources[i]->name)) {
            apush(catalog, sources[i]);
        } else if (!render_shader_is_model(render_shader_find(sources[i]->name))) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Model and shader asset names must be unique: %s",
                    sources[i]->name
                );
            }
            afree(catalog);
            return false;
        }
    }
    bool ok = model_generation_publish(catalog, acount(catalog), 0, NULL, 0, error);
    afree(catalog);
    return ok;
}

bool render_shader_model_remove(const char *name, shader_error *error)
{
    int id = render_shader_find(name);
    if (!render_shader_is_model(id)) {
        return false;
    }
    const behavior_asset **catalog = NULL;
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (shader_catalog_assets[i].is_model && i + 1 != id) {
            apush(catalog, source_at(i)->model);
        }
    }
    bool ok = model_generation_publish(catalog, acount(catalog), id, NULL, 0, error);
    afree(catalog);
    return ok;
}

bool render_shader_model_rename(const char *from, const char *to, shader_error *error)
{
    int id = render_shader_find(from);
    if (!render_shader_is_model(id) || !to || !*to || render_shader_find(to)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Model rename requires an available name.");
        }
        return false;
    }
    behavior_asset **copies = NULL;
    const behavior_asset **catalog = NULL;
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (!shader_catalog_assets[i].is_model) {
            continue;
        }
        behavior_asset *copy = behavior_asset_clone(source_at(i)->model);
        assert(copy);
        if (i + 1 == id) {
            int renamed = behavior_asset_set_name(copy, to);
            assert(renamed == 0);
            (void)renamed;
        }
        for (int j = 0; j < acount(copy->graphs); j++) {
            graph *g = copy->graphs[j];
            for (int k = 0; k < acount(g->nodes); k++) {
                node *n = g->nodes[k];
                if (n->desc == &NODE_ModelInstance && n->reference_asset &&
                    !strcmp(n->reference_asset, from)) {
                    n->reference_asset = graph_own_str(g, to, strlen(to));
                }
            }
        }
        apush(copies, copy);
        apush(catalog, copy);
    }
    bool ok = model_generation_publish(catalog, acount(catalog), 0, to, id, error);
    for (int i = 0; i < acount(copies); i++) {
        behavior_asset_free(copies[i]);
    }
    afree(copies);
    afree(catalog);
    return ok;
}

bool render_shader_material_remove(const char *name, shader_error *error)
{
    int id = render_shader_find(name);
    if (!id || shader_catalog_assets[id - 1].shape) {
        return false;
    }
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        for (int j = 0; shader_catalog_assets[i].is_model &&
                        j < acount(shader_catalog_assets[i].model.material_slots);
             j++) {
            const model_material_slot *slot = &shader_catalog_assets[i].model.material_slots[j];
            if (!strcmp(slot->material, name)) {
                if (error) {
                    error->node = slot->node;
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "Material %s is referenced by %s slot %s.",
                        name,
                        shader_catalog_assets[i].name,
                        slot->name
                    );
                }
                return false;
            }
        }
        for (int j = 0;
             shader_catalog_assets[i].is_model &&
             j < acount(shader_catalog_assets[i].model.material_dependencies);
             j++) {
            if (!strcmp(shader_catalog_assets[i].model.material_dependencies[j], name)) {
                if (error) {
                    error->node = 0;
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "Material %s is referenced by model %s.",
                        name,
                        shader_catalog_assets[i].name
                    );
                }
                return false;
            }
        }
    }
    source_free(source_at(id - 1));
    shader_catalog_free_asset(&shader_catalog_assets[id - 1]);
    shader_catalog_assets[id - 1] = (shader_asset){ .name = xstrdup("") };
    shader_catalog_revision++;
    shader_catalog_ramp_revision++;
    shader_catalog_parameter_revision++;
    return true;
}

static void model_source_material_rename(behavior_asset *source, const char *from, const char *to)
{
    for (int i = 0; i < acount(source->graphs); i++) {
        graph *g = source->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (n->desc == &NODE_ModelMaterialSlot && n->reference_asset &&
                !strcmp(n->reference_asset, from)) {
                n->reference_asset = graph_own_str(g, to, strlen(to));
            }
            for (int k = 0; n->desc == &NODE_ModelInstance && k < n->input_count; k++) {
                pin_spec *pin = &n->input_pins[k];
                if (pin->type == PIN_STR && pin->type_name &&
                    !strcmp(pin->type_name, "MaterialAsset") &&
                    graph_text_cstring(g, pin->default_value) &&
                    !strcmp(graph_text_cstring(g, pin->default_value), from)) {
                    pin_value value = graph_text_value(g, to, strlen(to));
                    value.type_name = "MaterialAsset";
                    int updated = graph_set_pin_default(g, n, false, k, &value);
                    assert(updated == 0);
                    (void)updated;
                }
            }
        }
    }
}

bool render_shader_material_rename(const char *from, const char *to, shader_error *error)
{
    int id = render_shader_find(from);
    if (!id || shader_catalog_assets[id - 1].shape || !to || !*to ||
        render_shader_find(to)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Material rename requires an available name.");
        }
        return false;
    }
    /* Identity and shader code stay fixed. Prepare all owned replacement source
       strings before publishing the renamed material and its model bindings. */
    behavior_asset *material = behavior_asset_from_value(source_at(id - 1)->value);
    assert(material);
    int renamed = behavior_asset_set_name(material, to);
    assert(renamed == 0);
    (void)renamed;
    av_doc *doc = av_doc_new();
    av_value *material_value = behavior_asset_to_value(doc, material);
    av_doc_set_root(doc, material_value);
    behavior_asset_free(material);
    behavior_asset **sources = NULL;
    av_value **values = NULL;
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        behavior_asset *source = shader_catalog_assets[i].is_model
                                   ? behavior_asset_clone(source_at(i)->model)
                                   : NULL;
        av_value *value = NULL;
        if (source) {
            model_source_material_rename(source, from, to);
            av_doc *source_doc = av_doc_new();
            value = behavior_asset_to_value(source_doc, source);
            av_doc_set_root(source_doc, value);
        }
        apush(sources, source);
        apush(values, value);
    }
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        shader_asset *asset = &shader_catalog_assets[i];
        if (!asset->is_model) {
            continue;
        }
        source_set(i, (shader_source){ .model = sources[i], .value = values[i] });
        for (int j = 0; j < acount(asset->model.material_slots); j++) {
            model_material_slot *slot = &asset->model.material_slots[j];
            if (!strcmp(slot->material, from)) {
                free(slot->material);
                slot->material = xstrdup(to);
            }
        }
        for (int j = 0; j < acount(asset->model.material_dependencies); j++) {
            char **dependency = &asset->model.material_dependencies[j];
            if (!strcmp(*dependency, from)) {
                free(*dependency);
                *dependency = xstrdup(to);
            }
        }
    }
    free(shader_catalog_assets[id - 1].name);
    shader_catalog_assets[id - 1].name = xstrdup(to);
    av_value_free(source_at(id - 1)->value);
    source_at(id - 1)->value = material_value;
    shader_catalog_parameter_revision++;
    afree(sources);
    afree(values);
    return true;
}

static bool source_parameter_asset(
    const behavior_asset *source,
    const behavior_asset *const *catalog,
    int count,
    shader_asset *result,
    shader_error *error
)
{
    *result = (shader_asset){ .shape = source->domain != BEHAVIOR_DOMAIN_MATERIAL,
                              .is_model = source->domain == BEHAVIOR_DOMAIN_MODEL };
    model_program_init(&result->model);
    result->name = xstrdup(source->name);
    if (result->is_model) {
        if (!model_compile_with_catalog(source, catalog, count, &result->model, error)) {
            return false;
        }
        if (!render_image_validate_volumes(&result->model.shader, error)) {
            return false;
        }
        result->model.resolve_image = render_image_resolve;
        result->graph = result->model.shader;
        shader_graph_init(&result->model.shader);
        for (int i = 0; i < acount(result->model.parameters); i++) {
            const model_parameter *p = &result->model.parameters[i];
            memcpy(result->defaults[p->slot], p->defaults, sizeof p->defaults);
            if (p->hidden) {
                continue;
            }
            result->names[p->slot] = xstrdup(p->name);
            result->widths[p->slot] = p->width;
        }
        return true;
    }
    if (!shader_graph_from_graph(source->graph, &result->graph, error)) {
        return false;
    }
    for (int i = 0; i < acount(result->graph.nodes); i++) {
        const shader_node *n = &result->graph.nodes[i];
        if (n->op != SHADER_PARAMETER) {
            continue;
        }
        const node *authored = graph_node_by_id(source->graph, n->source_id);
        free(result->names[n->parameter]);
        result->names[n->parameter] = xstrdup(
            authored && authored->reference_name ? authored->reference_name : ""
        );
        result->widths[n->parameter] = n->width;
        memcpy(result->defaults[n->parameter], n->value, sizeof n->value);
    }
    return true;
}

bool render_shader_binding_sources_validate(
    const Shader *binding,
    const char *text,
    const behavior_asset *const *sources,
    int count,
    shader_error *error
)
{
    const behavior_asset *shape = NULL, *material = NULL;
    const graph *particle = NULL;
    for (int i = 0; binding && i < count; i++) {
        if (binding->shape && !strcmp(binding->shape, sources[i]->name)) {
            shape = sources[i];
        }
        if (binding->material && !strcmp(binding->material, sources[i]->name)) {
            material = sources[i];
        }
        if (binding->material && sources[i]->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
            size_t length = strlen(sources[i]->name);
            if (!strncmp(binding->material, sources[i]->name, length) &&
                binding->material[length] == '.') {
                const graph *g = behavior_asset_find_graph_by_name(
                    sources[i],
                    binding->material + length + 1
                );
                if (g && particle_function(g)) {
                    material = sources[i];
                    particle = g;
                }
            }
        }
    }
    if (binding && ((binding->shape && *binding->shape && !shape) ||
                    (binding->material && *binding->material && !material))) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader parameter binding references a missing asset.");
        }
        return false;
    }
    if ((shape && shape->domain != BEHAVIOR_DOMAIN_SHAPE &&
         shape->domain != BEHAVIOR_DOMAIN_MODEL) ||
        (material && material->domain != BEHAVIOR_DOMAIN_MATERIAL && !particle)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader parameter binding uses the wrong asset family.");
        }
        return false;
    }
    shader_asset s = { 0 };
    shader_asset *material_assets = NULL;
    const shader_asset **material_catalog = NULL;
    bool ok = !shape || source_parameter_asset(shape, sources, count, &s, error);
    shader_asset particle_asset = { 0 };
    if (ok && particle) {
        char message[128] = { 0 };
        behavior_asset *linked = behavior_asset_link_libraries(material, sources, count, message, sizeof message);
        if (!linked) {
            if (error) {
                snprintf(error->message, sizeof error->message, "%s", message);
            }
            ok = false;
        } else {
            ok = particle_asset_prepare(
                behavior_asset_find_graph(linked, particle->graph_id),
                &particle_asset,
                error
            );
            behavior_asset_free(linked);
        }
    }
    for (int i = 0; ok && i < count; i++) {
        if (sources[i]->domain != BEHAVIOR_DOMAIN_MATERIAL) {
            continue;
        }
        bool used = material && !strcmp(material->name, sources[i]->name);
        for (int j = 0; shape && j < acount(s.model.material_slots); j++) {
            used |= !strcmp(s.model.material_slots[j].material, sources[i]->name);
        }
        if (!used) {
            continue;
        }
        shader_asset temporary = { 0 };
        ok = source_parameter_asset(sources[i], sources, count, &temporary, error);
        apush(material_assets, temporary);
    }
    for (int i = 0; i < acount(material_assets); i++) {
        apush(material_catalog, &material_assets[i]);
    }
    ok = ok && shader_catalog_material_source_resources_validate(
                   material_catalog,
                   acount(material_catalog),
                   error
               );
    float sv[32][4], mv[32][4];
    if (ok) {
        const shader_asset *fallback = particle ? &particle_asset
                                     : material ? shader_catalog_material_asset_find(
                                                      material->name,
                                                      material_catalog,
                                                      acount(material_catalog)
                                                  )
                                                : NULL;
        ok = authoring_binding_parameter_values_from(
                 shape ? &s : NULL,
                 fallback,
                 material_catalog,
                 acount(material_catalog),
                 text,
                 sv,
                 mv,
                 NULL,
                 error
             ) &&
             shader_catalog_validate_model_values(shape ? &s : NULL, sv, error);
    }
    shader_catalog_free_asset(&s);
    shader_catalog_free_asset(&particle_asset);
    for (int i = 0; i < acount(material_assets); i++) {
        shader_catalog_free_asset(&material_assets[i]);
    }
    afree(material_assets);
    afree(material_catalog);
    return ok;
}

static bool authoring_plain_validate(int index, const char *text, shader_error *error)
{
    if (render_shader_is_model(index)) {
        Shader binding = { .shape = shader_catalog_assets[index - 1].name };
        return render_shader_binding_parameters_validate(&binding, text, error);
    }
    if (error) {
        *error = (shader_error){ 0 };
    }
    if (!text || !*text) {
        return true;
    }
    if (index <= 0 || index > acount(shader_catalog_assets)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader asset is not registered.");
        }
        return false;
    }
    av_doc *doc = av_value_read(text, NULL, 0);
    av_value *root = doc ? av_doc_root(doc) : NULL;
    if (!root || !av_is_obj(root)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Preview parameters must be code named values.");
        }
        av_doc_free(doc);
        return false;
    }
    shader_asset *asset = &shader_catalog_assets[index - 1];
    av_obj_iter iterator = av_obj_iter_with(root);
    av_value *key;
    int count = 0;
    while ((key = av_obj_iter_next(&iterator))) {
        count++;
        if (count > SHADER_PARAMETER_MAX) {
            if (error) {
                snprintf(error->message, sizeof error->message, "Shader preview accepts at most %d parameter shader_catalog_overrides.", SHADER_PARAMETER_MAX);
            }
            av_doc_free(doc);
            return false;
        }
        const char *name = av_get_str(key);
        int slot = shader_catalog_parameter_slot(asset, name);
        if (slot < 0) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Unknown shader preview parameter '%s'.",
                    name ? name : ""
                );
            }
            av_doc_free(doc);
            return false;
        }
        av_value *value = av_obj_iter_get_val(key);
        int width = asset->widths[slot];
        bool valid = width == 1 &&
                     ((av_is_num(value) && isfinite(av_get_num(value))) ||
                      av_is_bool(value));
        if (!valid && width > 1 && av_is_arr(value) && (int)av_arr_size(value) == width) {
            valid = true;
            for (int i = 0; i < width; i++) {
                av_value *component = av_arr_get(value, (size_t)i);
                valid &= av_is_num(component) && isfinite(av_get_num(component));
            }
        }
        if (!valid) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    width == 1 ? "Shader preview parameter '%s' must be a "
                                 "finite number or boolean."
                               : "Shader preview parameter '%s' must be an "
                                 "array of %d finite numbers.",
                    name ? name : "",
                    width
                );
            }
            av_doc_free(doc);
            return false;
        }
    }
    av_doc_free(doc);
    return true;
}

static void parse_parameters(const shader_asset *asset, const char *text, float values[32][4])
{
    memcpy(values, asset->defaults, sizeof asset->defaults);
    av_doc *doc = text && *text ? av_value_read(text, NULL, 0) : NULL;
    av_value *root = doc ? av_doc_root(doc) : NULL;
    for (int i = 0; i < 32; i++) {
        if (!asset->names[i]) {
            continue;
        }
        av_value *value = root ? av_obj_get(root, asset->names[i]) : NULL;
        if (root) {
            const char *scope = asset->shape ? "shape" : "material";
            size_t size = strlen(scope) + strlen(asset->names[i]) + 2;
            char *scoped = xmalloc(size);
            snprintf(scoped, size, "%s.%s", scope, asset->names[i]);
            av_value *explicit_value = av_obj_get(root, scoped);
            free(scoped);
            if (explicit_value) {
                value = explicit_value;
            }
        }
        float parsed[4] = { 0 };
        bool valid = false;
        if (av_is_num(value) && asset->widths[i] == 1) {
            parsed[0] = (float)av_get_num(value);
            valid = isfinite(parsed[0]);
        }
        if (av_is_bool(value) && asset->widths[i] == 1) {
            parsed[0] = av_get_bool(value);
            valid = true;
        }
        if (av_is_arr(value) && (int)av_arr_size(value) == asset->widths[i]) {
            valid = true;
            for (int j = 0; j < asset->widths[i]; j++) {
                av_value *v = av_arr_get(value, (size_t)j);
                parsed[j] = (float)av_get_num(v);
                valid &= av_is_num(v) && isfinite(parsed[j]);
            }
        }
        if (valid) {
            memcpy(values[i], parsed, sizeof parsed);
        }
    }
    av_doc_free(doc);
}

static bool read_parameter_value(const shader_asset *asset, int slot, av_value *value, float result[4])
{
    int width = asset->widths[slot];
    memset(result, 0, 4 * sizeof(float));
    if (width == 1 && av_is_num(value)) {
        result[0] = (float)av_get_num(value);
        return isfinite(result[0]);
    }
    if (width == 1 && !asset->is_model && av_is_bool(value)) {
        result[0] = av_get_bool(value);
        return true;
    }
    if (!av_is_arr(value) || (int)av_arr_size(value) != width) {
        return false;
    }
    for (int i = 0; i < width; i++) {
        av_value *component = av_arr_get(value, (size_t)i);
        if (!av_is_num(component)) {
            return false;
        }
        result[i] = (float)av_get_num(component);
        if (!isfinite(result[i])) {
            return false;
        }
    }
    return true;
}

static int material_parameter_slot(const model_material_entry *entry, const char *name)
{
    if (!entry->asset) {
        return -1;
    }
    int direct = shader_catalog_parameter_slot(entry->asset, name);
    if (direct >= 0 || !*entry->scope) {
        return direct;
    }
    size_t length = strlen(entry->scope);
    return !strncmp(name, entry->scope, length) && name[length] == '.'
             ? shader_catalog_parameter_slot(entry->asset, name + length + 1)
             : -1;
}

static bool authoring_binding_parameter_values_from(
    const shader_asset *shape,
    const shader_asset *material,
    const shader_asset *const *catalog,
    int count,
    const char *text,
    float shape_values[32][4],
    float material_values[32][4],
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4],
    shader_error *error
)
{
    memset(shape_values, 0, 32 * 4 * sizeof(float));
    if (shape) {
        memcpy(shape_values, shape->defaults, sizeof shape->defaults);
    }
    model_material_entry entries[MODEL_MATERIAL_SLOT_MAX + 1];
    int length;
    if (!shader_catalog_model_material_entries(shape, material, catalog, count, entries, &length, material_values, error)) {
        return false;
    }
    if (bindings) {
        memset(bindings, 0, (MODEL_MATERIAL_SLOT_MAX + 1) * 4 * sizeof(float));
        for (int i = 0; i < length; i++) {
            bindings[i][0] = entries[i].asset
                               ? render_shader_find(entries[i].asset->name)
                               : 0;
            bindings[i][1] = entries[i].offset;
            bindings[i][2] = entries[i].count;
        }
    }
    if (!text || !*text) {
        return true;
    }
    av_doc *doc = av_value_read(text, NULL, 0);
    av_value *root = doc ? av_doc_root(doc) : NULL;
    if (!root || !av_is_obj(root)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader parameters require named code values.");
        }
        av_doc_free(doc);
        return false;
    }
    bool seen_shape[32] = { 0 }, seen_material[32] = { 0 }, ok = true;
    av_obj_iter iterator = av_obj_iter_with(root);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const char *name = av_get_str(key), *local = name;
        bool shape_only = name && !strncmp(name, "shape.", 6);
        bool material_only = name && !strncmp(name, "material.", 9);
        local += shape_only ? 6 : material_only ? 9 : 0;
        int s = shape && !material_only ? shader_catalog_parameter_slot(shape, local)
                                        : -1;
        int m = -1, entry = -1, matches = s >= 0;
        for (int i = 0; !shape_only && i < length; i++) {
            int found = material_parameter_slot(&entries[i], local);
            if (found >= 0) {
                m = found;
                entry = i;
                matches++;
            }
        }
        if (matches != 1) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "%s shader parameter '%s'; use shape.Name or "
                    "material.Slot.Name to disambiguate.",
                    matches ? "Ambiguous" : "Unknown",
                    name
                );
            }
            ok = false;
            break;
        }
        const shader_asset *asset = s >= 0 ? shape : entries[entry].asset;
        int slot = s >= 0 ? s : m;
        int flat = s >= 0 ? slot : entries[entry].offset + slot;
        bool *seen = s >= 0 ? seen_shape : seen_material;
        float *destination = s >= 0 ? shape_values[flat] : material_values[flat];
        if (seen[flat] ||
            !read_parameter_value(asset, slot, av_obj_iter_get_val(key), destination)) {
            if (error) {
                error->node = asset->is_model ? asset->model.parameters[slot].node : 0;
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Parameter '%s' requires one override with %d finite "
                    "numeric components.",
                    name,
                    asset->widths[slot]
                );
            }
            ok = false;
            break;
        }
        seen[flat] = true;
    }
    av_doc_free(doc);
    return ok;
}

static bool parse_material_parameters_scoped(
    const shader_asset *asset,
    const char *text,
    const char *scope,
    float values[32][4],
    shader_error *error
)
{
    memcpy(values, asset->defaults, sizeof asset->defaults);
    if (!text || !*text) {
        return true;
    }
    av_doc *doc = av_value_read(text, NULL, 0);
    av_value *root = doc ? av_doc_root(doc) : NULL;
    if (!root || !av_is_obj(root)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader parameters require named code values.");
        }
        av_doc_free(doc);
        return false;
    }
    bool seen[32] = { 0 }, ok = true;
    av_obj_iter iterator = av_obj_iter_with(root);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const char *name = av_get_str(key);
        if (!strncmp(name, "shape.", 6)) {
            continue;
        }
        const char *local = !strncmp(name, "material.", 9) ? name + 9 : name;
        int slot = shader_catalog_parameter_slot(asset, local);
        bool selected = false;
        size_t length = strlen(scope);
        if (slot < 0 && length && !strncmp(local, scope, length) &&
            local[length] == '.') {
            selected = true;
            slot = shader_catalog_parameter_slot(asset, local + length + 1);
        }
        if (slot < 0 && !selected) {
            continue; /* Geometry or another material slot. */
        }
        if (slot < 0 || seen[slot] ||
            !read_parameter_value(asset, slot, av_obj_iter_get_val(key), values[slot])) {
            if (error) {
                snprintf(error->message, sizeof error->message, "Selected material parameter '%s' requires a known parameter and one override with the declared finite numeric width.", name);
            }
            ok = false;
            break;
        }
        seen[slot] = true;
    }
    av_doc_free(doc);
    return ok;
}

/* Publishes the asset's values from `text`, as renderers that draw the asset
 * alone read them. Names the asset lacks are left to the asset they belong to. */
static bool publish_plain(int index, const char *text, shader_error *error)
{
    if (!index) {
        return true;
    }
    shader_values v = { .key = text,
                        .shape = index,
                        .shape_generation = render_shader_generation(index),
                        .plain = true };
    parse_parameters(&shader_catalog_assets[index - 1], text, v.shape_values);
    return shader_values_publish(&v, error);
}
bool authoring_shader_parameters_prepare(int index, const char *text, shader_error *error)
{
    if (!text || !*text) {
        return true;
    }
    return authoring_plain_validate(index, text, error) &&
           publish_plain(index, text, error);
}
bool authoring_shader_binding_prepare(const Shader *binding, shader_error *error)
{
    const char *text = binding->parameters;
    if (!text || !*text) {
        return true;
    }
    int shape = render_shader_find(binding->shape),
        material = render_shader_find(binding->material);
    const shader_asset *a = shape ? &shader_catalog_assets[shape - 1] : NULL,
                       *b = material ? &shader_catalog_assets[material - 1] : NULL;
    shader_values v = { .key = text,
                        .shape = shape,
                        .material = material,
                        .shape_generation = render_shader_generation(shape),
                        .material_generation = render_shader_generation(material) };
    if (!authoring_binding_parameter_values_from(a, b, NULL, 0, text, v.shape_values, v.material_values, v.bindings, error) ||
        !shader_catalog_validate_model_values(a, v.shape_values, error)) {
        return false;
    }
    for (int i = 0; i <= MODEL_MATERIAL_SLOT_MAX; i++) {
        v.binding_generations[i] = render_shader_generation((int)v.bindings[i][0]);
    }
    if (!shader_values_publish(&v, error) || !publish_plain(shape, text, error) ||
        !publish_plain(material, text, error)) {
        return false;
    }
    model_material_entry entries[MODEL_MATERIAL_SLOT_MAX + 1];
    int count;
    float defaults[32][4];
    if (!shader_catalog_model_material_entries(a, b, NULL, 0, entries, &count, defaults, error)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!entries[i].asset) {
            continue;
        }
        int id = render_shader_find(entries[i].asset->name);
        shader_values scoped = { .key = text,
                                 .scope = entries[i].scope,
                                 .material = id,
                                 .material_generation = render_shader_generation(id),
                                 .scoped = true };
        if (!parse_material_parameters_scoped(
                entries[i].asset,
                text,
                entries[i].scope,
                scoped.material_values,
                error
            ) ||
            !shader_values_publish(&scoped, error)) {
            return false;
        }
    }
    return true;
}
bool render_shader_parameters_validate(int index, const char *text, shader_error *error)
{
    if (render_shader_is_model(index)) {
        Shader b = { .shape = shader_catalog_assets[index - 1].name,
                     .parameters = (char *)text };
        return authoring_shader_binding_prepare(&b, error);
    }
    return authoring_shader_parameters_prepare(index, text, error);
}
