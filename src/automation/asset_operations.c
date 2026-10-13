#include "automation/asset_operations.h"

#include "assets/asset_schema.h"
#include "assets/model_import_recipe.h"
#include "assets/new_asset.h"
#include "authoring/material_create.h"
#include "authoring/resource_catalog.h"
#include "authoring/shader_report.h"
#include "automation/controller.h"
#include "runtime/images/catalog.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/text_search.h"
#include "support/xalloc.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

yyjson_mut_val *automation_asset_value(yyjson_mut_doc *doc, const av_value *value)
{
    char *json = value ? av_json_write(value) : NULL;
    yyjson_doc *parsed = json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_mut_val *result = parsed
                               ? yyjson_val_mut_copy(doc, yyjson_doc_get_root(parsed))
                               : NULL;
    yyjson_doc_free(parsed);
    free(json);
    return result;
}

static int invalid(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return CHIBI_AUTOMATION_INVALID_REQUEST;
}

static bool optional_bool(yyjson_val *request, const char *key, bool *out)
{
    yyjson_val *value = yyjson_obj_get(request, key);
    *out = value && yyjson_get_bool(value);
    return !value || yyjson_is_bool(value);
}

static void add_vector(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *key,
    const float *values,
    int count
)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, key);
    for (int i = 0; i < count; i++) {
        yyjson_mut_arr_add_real(doc, array, values[i]);
    }
}

void automation_model_report(yyjson_mut_doc *doc, yyjson_mut_val *object, const model_import_report *report)
{
    yyjson_mut_val *metrics = yyjson_mut_obj_add_obj(doc, object, "metrics");
    yyjson_mut_obj_add_str(doc, metrics, "units", "meters");
    yyjson_mut_obj_add_int(doc, metrics, "vertices", report->vertices);
    yyjson_mut_obj_add_int(doc, metrics, "triangles", report->triangles);
    yyjson_mut_obj_add_int(doc, metrics, "materials", report->materials);
    yyjson_mut_obj_add_int(doc, metrics, "images", report->images);
    yyjson_mut_obj_add_uint(doc, metrics, "source_bytes", report->source_bytes);
    yyjson_mut_obj_add_uint(doc, metrics, "output_bytes", report->output_bytes);
    add_vector(doc, metrics, "bounds_min", report->bounds_min, 3);
    add_vector(doc, metrics, "bounds_max", report->bounds_max, 3);
    yyjson_mut_val *ignored = yyjson_mut_obj_add_obj(doc, object, "ignored_features");
    yyjson_mut_obj_add_int(doc, ignored, "skins", report->skins);
    yyjson_mut_obj_add_int(doc, ignored, "animations", report->animations);
    yyjson_mut_obj_add_int(doc, ignored, "cameras", report->cameras);
    yyjson_mut_obj_add_int(doc, ignored, "lights", report->lights);
}

static int describe_schema(
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
)
{
    yyjson_val *kind_value = yyjson_obj_get(request, "kind");
    const char *kind = yyjson_get_str(kind_value);
    if (kind_value && !kind) {
        return invalid(error, size, "kind must be an asset family string");
    }
    const asset_schema *selected = kind ? asset_schema_find(kind) : NULL;
    if (kind && !selected) {
        return invalid(error, size, "unknown schema kind; omit kind to discover supported schemas");
    }
    av_doc *description = av_doc_new();
    av_value *root = av_obj(description);
    av_doc_set_root(description, root);
    av_obj_add_int(description, root, "schema_version", 1);
    av_value *schemas = av_obj_add_arr(description, root, "schemas");
    for (int i = 0; i < asset_schema_count(); i++) {
        const asset_schema *schema = asset_schema_at(i);
        if (!selected || selected == schema) {
            av_arr_append(schemas, asset_schema_describe(description, schema, true));
        }
    }
    yyjson_mut_val *value = automation_asset_value(doc, root);
    av_doc_free(description);
    if (!value) {
        return invalid(error, size, "could not serialize asset schemas");
    }
    yyjson_mut_obj_add_val(doc, result, "result", value);
    return CHIBI_AUTOMATION_OK;
}

static bool resource_filters_valid(yyjson_val *request, char *error, size_t size)
{
    yyjson_val *query = yyjson_obj_get(request, "query");
    yyjson_val *kinds = yyjson_obj_get(request, "kinds");
    yyjson_val *cursor = yyjson_obj_get(request, "cursor");
    yyjson_val *limit = yyjson_obj_get(request, "limit");
    if ((query && !yyjson_is_str(query)) || (kinds && !yyjson_is_arr(kinds)) ||
        (cursor && !yyjson_is_uint(cursor)) ||
        (limit && (!yyjson_is_uint(limit) || !yyjson_get_uint(limit)))) {
        snprintf(error, size, "resource filters require string query, kinds array and nonnegative cursor/positive limit");
        return false;
    }
    yyjson_arr_iter iterator = yyjson_arr_iter_with(kinds);
    for (yyjson_val *kind; (kind = yyjson_arr_iter_next(&iterator));) {
        if (!yyjson_is_str(kind)) {
            snprintf(error, size, "resource kinds must contain strings");
            return false;
        }
    }
    return true;
}

static av_doc *resource_description(const char *project, yyjson_val *request, char *error, size_t size)
{
    av_doc *catalog = authoring_resource_catalog(chibi_automation_content(project), error, size);
    if (!catalog) {
        return NULL;
    }
    av_value *root = av_doc_root(catalog);
    av_value *resources = av_obj_get(root, "resources");
    av_value *filtered = av_arr(catalog);
    const char *query = yyjson_get_str(yyjson_obj_get(request, "query"));
    yyjson_val *kinds = yyjson_obj_get(request, "kinds");
    yyjson_val *cursor_value = yyjson_obj_get(request, "cursor");
    yyjson_val *limit_value = yyjson_obj_get(request, "limit");
    size_t cursor = (size_t)yyjson_get_uint(cursor_value);
    size_t limit = limit_value ? (size_t)yyjson_get_uint(limit_value) : SIZE_MAX;
    size_t total = 0;
    for (size_t i = 0; i < av_arr_size(resources); i++) {
        av_value *item = av_arr_get(resources, i);
        const char *key = av_get_str(av_obj_get(item, "key"));
        const char *kind = av_get_str(av_obj_get(item, "kind"));
        bool matched = !kinds || !yyjson_arr_size(kinds);
        yyjson_arr_iter iterator = yyjson_arr_iter_with(kinds);
        for (yyjson_val *candidate; (candidate = yyjson_arr_iter_next(&iterator));) {
            const char *name = yyjson_get_str(candidate);
            matched |= !strcmp(name, kind);
        }
        if (!matched ||
            (query && *query && text_search_word_score(query, key, kind) < 0)) {
            continue;
        }
        if (total >= cursor && av_arr_size(filtered) < limit) {
            av_arr_append(filtered, av_clone(catalog, item));
        }
        total++;
    }
    av_obj_set(catalog, root, "resources", filtered);
    av_obj_add_uint(catalog, root, "total", total);
    av_obj_add_uint(catalog, root, "count", av_arr_size(filtered));
    bool more = cursor < total && av_arr_size(filtered) < total - cursor;
    av_obj_add_bool(catalog, root, "has_more", more);
    if (more) {
        av_obj_add_uint(catalog, root, "next_cursor", cursor + av_arr_size(filtered));
    } else {
        av_obj_add_null(catalog, root, "next_cursor");
    }
    return catalog;
}

int automation_resources_attach(
    const char *project,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
)
{
    if (!resource_filters_valid(request, error, size)) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    av_doc *catalog = resource_description(project, request, error, size);
    if (!catalog) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    const char *hash = av_get_str(av_obj_get(av_doc_root(catalog), "content_hash"));
    if (hash) {
        yyjson_mut_obj_add_strcpy(doc, result, "project_content_hash", hash);
    }
    yyjson_mut_obj_add_val(
        doc,
        result,
        "resource_catalog",
        automation_asset_value(doc, av_doc_root(catalog))
    );
    av_doc_free(catalog);
    return CHIBI_AUTOMATION_OK;
}

static int resource_catalog(
    const char *project,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
)
{
    if (!resource_filters_valid(request, error, size)) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    av_doc *catalog = resource_description(project, request, error, size);
    if (!catalog) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    yyjson_mut_val *value = automation_asset_value(doc, av_doc_root(catalog));
    av_doc_free(catalog);
    if (!value) {
        return invalid(error, size, "could not serialize resource catalog");
    }
    yyjson_mut_obj_add_val(doc, result, "result", value);
    return CHIBI_AUTOMATION_OK;
}

static char *source_path(const char *project, const char *source)
{
    if (source[0] == '/') {
        return xstrdup(source);
    }
    size_t size = strlen(project) + strlen(source) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", project, source);
    return path;
}

static bool import_name(const char *source, const char *requested, char *name, size_t size)
{
    if (requested) {
        snprintf(name, size, "%s", requested);
        return strlen(requested) < size;
    }
    const char *stem = strrchr(source, '/');
    stem = stem ? stem + 1 : source;
    const char *dot = strrchr(stem, '.');
    size_t length = dot ? (size_t)(dot - stem) : strlen(stem);
    if (length >= size) {
        return false;
    }
    memcpy(name, stem, length);
    name[length] = '\0';
    return true;
}

static void import_warnings(yyjson_mut_doc *doc, yyjson_mut_val *result, const model_import_report *report)
{
    yyjson_mut_val *warnings = yyjson_mut_obj_add_arr(doc, result, "warnings");
    yyjson_mut_val *details = yyjson_mut_obj_add_arr(doc, result, "warning_details");
    for (int i = 0; i < acount(report->warnings); i++) {
        yyjson_mut_arr_add_strcpy(doc, warnings, report->warnings[i]);
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, details);
        yyjson_mut_obj_add_strcpy(doc, item, "code", report->warning_codes[i]);
        yyjson_mut_obj_add_strcpy(doc, item, "message", report->warnings[i]);
    }
}

static int preflight_model(
    const char *project,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *response,
    char *error,
    size_t size
)
{
    const char *source = yyjson_get_str(yyjson_obj_get(request, "source"));
    const char *requested = yyjson_get_str(yyjson_obj_get(request, "name"));
    bool strict = false;
    if (!source || !*source || !optional_bool(request, "strict", &strict) ||
        (yyjson_obj_get(request, "name") && !requested)) {
        return invalid(error, size, "preflight_model requires source, optional name and boolean strict");
    }
    char name[NEW_ASSET_NAME_MAX + 1], reason[128];
    if (!import_name(source, requested, name, sizeof name) ||
        !new_asset_name_valid(name, reason, sizeof reason)) {
        return invalid(error, size, "name must be a valid bounded asset name");
    }
    char *path = source_path(project, source);
    model_import_plan *plan = NULL;
    bool prepared = model_import_prepare(chibi_automation_content(project), path, name, false, &plan, error, size);
    free(path);
    if (!prepared) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    const model_import_report *report = model_import_plan_report(plan);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, response, "result");
    yyjson_mut_obj_add_strcpy(doc, value, "prototype", name);
    yyjson_mut_obj_add_bool(doc, value, "read_only", true);
    automation_model_report(doc, value, report);
    import_warnings(doc, value, report);
    int count = 0;
    const model_import_output *outputs = model_import_plan_outputs(plan, &count);
    yyjson_mut_val *files = yyjson_mut_obj_add_arr(doc, value, "outputs");
    for (int i = 0; i < count; i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, files);
        yyjson_mut_obj_add_strcpy(doc, item, "path", outputs[i].key);
        yyjson_mut_obj_add_strcpy(doc, item, "identity", outputs[i].identity);
        yyjson_mut_obj_add_uint(doc, item, "bytes", outputs[i].size);
        yyjson_mut_obj_add_bool(doc, item, "editable", outputs[i].editable);
    }
    const model_import_dependency *dependencies = model_import_plan_dependencies(plan, &count);
    yyjson_mut_val *inputs = yyjson_mut_obj_add_arr(doc, value, "dependencies");
    yyjson_mut_obj_add_str(doc, value, "hash_algorithm", "fnv1a64");
    for (int i = 0; i < count; i++) {
        char hash[17];
        snprintf(
            hash,
            sizeof hash,
            "%016llx",
            (unsigned long long)dependencies[i].hash
        );
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, inputs);
        yyjson_mut_obj_add_strcpy(doc, item, "path", dependencies[i].path);
        yyjson_mut_obj_add_strcpy(doc, item, "content_hash", hash);
        yyjson_mut_obj_add_uint(doc, item, "bytes", dependencies[i].size);
    }
    bool rejected = strict && acount(report->warnings);
    model_import_plan_free(plan);
    if (rejected) {
        snprintf(error, size, "strict preflight rejects import warnings; no files were written");
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
}

static int recipe_model(
    const char *project,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *response,
    char *error,
    size_t size
)
{
    bool reimport = !strcmp(yyjson_get_str(yyjson_obj_get(request, "operation")), "reimport_model");
    bool strict = false, check = false;
    const char *source = yyjson_get_str(yyjson_obj_get(request, "source"));
    const char *requested = yyjson_get_str(yyjson_obj_get(request, "name"));
    if ((!reimport && (!source || !*source)) ||
        (reimport && (!requested || !*requested)) ||
        (yyjson_obj_get(request, "source") && !source) ||
        (yyjson_obj_get(request, "name") && !requested) ||
        !optional_bool(request, "strict", &strict) ||
        !optional_bool(request, "check", &check)) {
        return invalid(error, size, "import recipe requires a valid source/name and boolean strict/check");
    }
    char name[NEW_ASSET_NAME_MAX + 1], reason[128];
    if (!import_name(source ? source : "", requested, name, sizeof name) ||
        !new_asset_name_valid(name, reason, sizeof reason)) {
        return invalid(error, size, "name must be a valid bounded asset name");
    }
    model_import_recipe_plan *plan = NULL;
    errno = 0;
    if (!model_import_recipe_prepare(project, source, name, reimport, strict, &plan, error, size)) {
        return errno == EEXIST || errno == ESTALE ? CHIBI_AUTOMATION_CONFLICT
             : errno == EIO                       ? CHIBI_AUTOMATION_IO_ERROR
             : errno == EINVAL ? CHIBI_AUTOMATION_INVALID_REQUEST
                               : CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, response, "result");
    yyjson_mut_obj_add_strcpy(doc, value, "prototype", name);
    yyjson_mut_obj_add_bool(doc, value, "read_only", check);
    automation_model_report(doc, value, model_import_recipe_report(plan));
    import_warnings(doc, value, model_import_recipe_report(plan));
    int count = 0;
    const model_import_action *actions = model_import_recipe_actions(plan, &count);
    static const char *const labels[] = { "create", "update", "remove", "preserved-authored" };
    yyjson_mut_val *items = yyjson_mut_obj_add_arr(doc, value, "actions");
    for (int i = 0; i < count; i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, items);
        yyjson_mut_obj_add_strcpy(doc, item, "path", actions[i].key);
        yyjson_mut_obj_add_str(doc, item, "action", labels[actions[i].action]);
        if (actions[i].action == MODEL_IMPORT_PRESERVE) {
            yyjson_mut_obj_add_str(doc, item, "source_updates", "deferred; authored file preserved byte-for-byte");
        }
    }
    bool applied = check || model_import_recipe_apply(plan, error, size);
    yyjson_mut_obj_add_bool(doc, value, "applied", model_import_recipe_committed(plan));
    model_import_recipe_free(plan);
    if (!applied) {
        return errno == EEXIST || errno == ESTALE ? CHIBI_AUTOMATION_CONFLICT
                                                  : CHIBI_AUTOMATION_IO_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
}

static bool number(yyjson_val *request, const char *key, float *out)
{
    yyjson_val *value = yyjson_obj_get(request, key);
    if (!value) {
        return true;
    }
    double n = yyjson_get_num(value);
    if (!yyjson_is_num(value) || !isfinite(n) || fabs(n) > FLT_MAX) {
        return false;
    }
    *out = (float)n;
    return true;
}

static bool vector(yyjson_val *request, const char *key, float *out, int count)
{
    yyjson_val *value = yyjson_obj_get(request, key);
    if (!value) {
        return true;
    }
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != (size_t)count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        yyjson_val *item = yyjson_arr_get(value, (size_t)i);
        double n = yyjson_get_num(item);
        if (!yyjson_is_num(item) || !isfinite(n) || fabs(n) > FLT_MAX) {
            return false;
        }
        out[i] = (float)n;
    }
    return true;
}

static bool texture(yyjson_val *value, standard_texture *out)
{
    if (!yyjson_is_obj(value)) {
        return false;
    }
    out->image = yyjson_get_str(yyjson_obj_get(value, "image"));
    if (!out->image || !out->image[0] || !vector(value, "offset", out->offset, 2) ||
        !vector(value, "scale", out->scale, 2) ||
        !number(value, "rotation", &out->rotation)) {
        return false;
    }
    yyjson_val *filter_value = yyjson_obj_get(value, "filter");
    const char *filter = yyjson_get_str(filter_value);
    if (filter_value) {
        if (!filter || (strcmp(filter, "nearest") && strcmp(filter, "bilinear"))) {
            return false;
        }
        out->filter = !strcmp(filter, "nearest") ? SHADER_IMAGE_NEAREST
                                                 : SHADER_IMAGE_BILINEAR;
    }
    yyjson_val *wrap_value = yyjson_obj_get(value, "wrap");
    const char *wrap = yyjson_get_str(wrap_value);
    if (wrap_value) {
        if (!wrap || (strcmp(wrap, "repeat") && strcmp(wrap, "clamp"))) {
            return false;
        }
        out->wrap = !strcmp(wrap, "clamp") ? SHADER_IMAGE_CLAMP : SHADER_IMAGE_REPEAT;
    }
    return true;
}

static int create_material(
    const char *project,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *response,
    char *error,
    size_t size
)
{
    const char *name = yyjson_get_str(yyjson_obj_get(request, "name"));
    yyjson_val *preset_value = yyjson_obj_get(request, "preset");
    const char *preset = yyjson_get_str(preset_value);
    static const char *const presets[] = { "matte", "metal", "emissive", "unlit" };
    int selected = 0;
    if (preset_value) {
        for (selected = 0; preset && selected < MATERIAL_AUTHORING_PRESET_COUNT;
             selected++) {
            if (!strcmp(preset, presets[selected])) {
                break;
            }
        }
        if (!preset || selected == MATERIAL_AUTHORING_PRESET_COUNT) {
            return invalid(error, size, "preset must be matte, metal, emissive or unlit");
        }
    }
    standard_material material = material_authoring_defaults(
        (material_authoring_preset)selected
    );
    bool unlit = material.unlit;
    if (!name || !*name || !vector(request, "base_color", material.base_color, 4) ||
        !vector(request, "emission", material.emission, 3) ||
        !number(request, "roughness", &material.roughness) ||
        !number(request, "metallic", &material.metallic) ||
        !number(request, "normal_scale", &material.normal_scale) ||
        !number(request, "occlusion_strength", &material.occlusion_strength) ||
        !number(request, "alpha_cutoff", &material.alpha_cutoff) ||
        (yyjson_obj_get(request, "unlit") && !optional_bool(request, "unlit", &unlit))) {
        return invalid(error, size, "material factors must have their declared numeric/vector/boolean types");
    }
    material.unlit = unlit;
    yyjson_val *alpha_value = yyjson_obj_get(request, "alpha_mode");
    const char *alpha = yyjson_get_str(alpha_value);
    if (alpha_value) {
        if (!alpha || (strcmp(alpha, "opaque") && strcmp(alpha, "blend") &&
                       strcmp(alpha, "mask"))) {
            return invalid(error, size, "alpha_mode must be opaque, blend or mask");
        }
        material.alpha_mode = !strcmp(alpha, "opaque") ? SHADER_ALPHA_OPAQUE
                            : !strcmp(alpha, "mask")   ? SHADER_ALPHA_MASK
                                                       : SHADER_ALPHA_BLEND;
    }
    yyjson_val *textures = yyjson_obj_get(request, "textures");
    if (textures) {
        if (!yyjson_is_obj(textures)) {
            return invalid(error, size, "textures must be an object of named texture descriptors");
        }
        static const char *const names[] = { "base_color",
                                             "metallic_roughness",
                                             "normal",
                                             "occlusion",
                                             "emission" };
        yyjson_obj_iter iterator = yyjson_obj_iter_with(textures);
        for (yyjson_val *key; (key = yyjson_obj_iter_next(&iterator));) {
            const char *label = yyjson_get_str(key);
            int slot = 0;
            for (; slot < STANDARD_TEXTURE_COUNT; slot++) {
                if (!strcmp(label, names[slot])) {
                    break;
                }
            }
            if (slot == STANDARD_TEXTURE_COUNT ||
                !texture(yyjson_obj_iter_get_val(key), &material.textures[slot])) {
                return invalid(error, size, "unknown texture slot or invalid image/filter/wrap/UV descriptor");
            }
        }
    }
    char *path = material_authoring_create(chibi_automation_content(project), name, &material, error, size);
    if (!path) {
        return errno == EEXIST ? CHIBI_AUTOMATION_CONFLICT
             : errno == EINVAL ? CHIBI_AUTOMATION_INVALID_REQUEST
                               : CHIBI_AUTOMATION_IO_ERROR;
    }
    yyjson_mut_val *result = yyjson_mut_obj_add_obj(doc, response, "result");
    char relative[NEW_ASSET_NAME_MAX + sizeof "content/materials/.kdl"];
    snprintf(relative, sizeof relative, "content/materials/%s.kdl", name);
    yyjson_mut_obj_add_strcpy(doc, result, "path", relative);
    yyjson_mut_obj_add_strcpy(doc, result, "name", name);
    yyjson_mut_obj_add_str(doc, result, "preset", presets[selected]);
    yyjson_mut_obj_add_str(doc, result, "color_space", "linear RGB factors; color images sRGB; data images linear");
    yyjson_mut_obj_add_bool(doc, result, "editable", true);
    free(path);
    return CHIBI_AUTOMATION_OK;
}

void automation_shader_report(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const behavior_asset *asset,
    bool cpu_sample
)
{
    if (asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
        asset->domain != BEHAVIOR_DOMAIN_SHAPE) {
        return;
    }
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    shader_error error = { 0 };
    bool valid = shader_authoring_inspect(asset, &report, &error);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "shader_report");
    yyjson_mut_obj_add_bool(doc, value, "valid", valid);
    yyjson_mut_obj_add_bool(doc, value, "gpu_executed", false);
    if (!valid) {
        yyjson_mut_obj_add_uint(doc, value, "node", error.node);
        yyjson_mut_obj_add_strcpy(doc, value, "message", error.message);
        shader_authoring_report_free(&report);
        return;
    }
    yyjson_mut_val *costs = yyjson_mut_obj_add_obj(doc, value, "static_costs");
    yyjson_mut_obj_add_str(doc, costs, "basis", "compiled expressions; conservative samples per shading evaluation; excludes GPU time and raymarch iterations");
    yyjson_mut_obj_add_int(doc, costs, "nodes", report.costs.nodes);
    yyjson_mut_obj_add_int(doc, costs, "uniforms", report.costs.uniforms);
    yyjson_mut_obj_add_int(doc, costs, "images", report.costs.images);
    yyjson_mut_obj_add_int(doc, costs, "ramp_vec4s", report.costs.ramp_vec4s);
    yyjson_mut_obj_add_int(doc, costs, "texture_samples", report.costs.texture_samples);
    yyjson_mut_obj_add_int(doc, costs, "detail_expressions", report.costs.detail_expressions);
    static const char *const alpha_modes[] = { "blend", "opaque", "mask" };
    yyjson_mut_obj_add_str(doc, value, "alpha_mode", alpha_modes[report.alpha_mode]);
    yyjson_mut_val *parameters = yyjson_mut_obj_add_arr(doc, value, "parameters");
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        if (!report.names[i]) {
            continue;
        }
        yyjson_mut_val *parameter = yyjson_mut_arr_add_obj(doc, parameters);
        yyjson_mut_obj_add_int(doc, parameter, "slot", i);
        yyjson_mut_obj_add_strcpy(doc, parameter, "name", report.names[i]);
        yyjson_mut_obj_add_int(doc, parameter, "width", report.widths[i]);
        static const char *const presentations[] = { "scalar", "vector", "color" };
        yyjson_mut_obj_add_str(
            doc,
            parameter,
            "presentation",
            presentations[report.presentation[i]]
        );
        add_vector(doc, parameter, "default", report.defaults[i], report.widths[i]);
    }
    yyjson_mut_val *images = yyjson_mut_obj_add_arr(doc, value, "images");
    for (int i = 0; i < acount(report.program.images); i++) {
        const shader_image_source *image = &report.program.images[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, images);
        yyjson_mut_obj_add_strcpy(doc, item, "key", image->key);
        yyjson_mut_obj_add_str(
            doc,
            item,
            "color_space",
            image->color_space == SHADER_IMAGE_SRGB ? "srgb" : "linear"
        );
        yyjson_mut_obj_add_str(doc, item, "filter", image->filter == SHADER_IMAGE_NEAREST ? "nearest" : "bilinear");
        yyjson_mut_obj_add_str(doc, item, "wrap", image->wrap == SHADER_IMAGE_CLAMP ? "clamp" : "repeat");
    }
    if (cpu_sample) {
        shader_sample_context context = shader_authoring_center_context(asset);
        context.resolve_image = render_image_resolve;
        float outputs[SHADER_OUTPUT_MAX][4] = { 0 };
        bool sampled = shader_authoring_sample(&report, &context, NULL, outputs, &error);
        yyjson_mut_val *sample = yyjson_mut_obj_add_obj(doc, value, "cpu_sample");
        yyjson_mut_obj_add_bool(doc, sample, "valid", sampled);
        yyjson_mut_obj_add_str(doc, sample, "context", "UV .5,.5; time/beat/positions zero; Y normal/view; authored defaults; shared center-context gradients");
        yyjson_mut_obj_add_str(doc, sample, "coverage", "CPU shader values; not rendered pixels");
        if (sampled) {
            static const char *const names[] = {
                "base_color", "emission", "roughness", "metallic", "normal",
                "opacity",    "unlit",    "height",    "occlusion"
            };
            yyjson_mut_val *values = yyjson_mut_obj_add_obj(doc, sample, "outputs");
            for (int i = 0; i < report.program.output_count; i++) {
                add_vector(
                    doc,
                    values,
                    asset->domain == BEHAVIOR_DOMAIN_SHAPE && i == 0
                        ? "distance"
                        : names[i],
                    outputs[i],
                    4
                );
            }
        } else {
            yyjson_mut_obj_add_uint(doc, sample, "node", error.node);
            yyjson_mut_obj_add_strcpy(doc, sample, "message", error.message);
        }
    }
    shader_authoring_report_free(&report);
}

bool automation_asset_operation(const char *operation)
{
    return !strcmp(operation, "describe_asset_schema") ||
           !strcmp(operation, "resource_catalog") ||
           !strcmp(operation, "preflight_model") ||
           !strcmp(operation, "reimport_model") ||
           !strcmp(operation, "create_material");
}

int automation_asset_handle(
    const char *project,
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *response,
    char *error,
    size_t size
)
{
    const char *operation = yyjson_get_str(yyjson_obj_get(request, "operation"));
    if (!strcmp(operation, "describe_asset_schema")) {
        return describe_schema(request, doc, response, error, size);
    }
    if (!strcmp(operation, "resource_catalog")) {
        return resource_catalog(project, request, doc, response, error, size);
    }
    if (!strcmp(operation, "preflight_model")) {
        return preflight_model(project, request, doc, response, error, size);
    }
    if (!strcmp(operation, "reimport_model") || !strcmp(operation, "import_model")) {
        return recipe_model(project, request, doc, response, error, size);
    }
    if (!strcmp(operation, "create_material")) {
        return create_material(project, request, doc, response, error, size);
    }
    return invalid(error, size, "asset operation is not implemented");
}
