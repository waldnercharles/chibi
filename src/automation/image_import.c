#include "automation/image_import.h"

#include "authoring/image_import_commit.h"
#include "automation/controller.h"

#include <stdio.h>
#include <string.h>

static int invalid(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return CHIBI_AUTOMATION_INVALID_REQUEST;
}
static const char *string(yyjson_val *request, const char *key)
{
    yyjson_val *value = yyjson_obj_get(request, key);
    const char *text = yyjson_get_str(value);
    return text && strlen(text) == yyjson_get_len(value) ? text : NULL;
}
static bool hash_read(const char *text, uint64_t *hash)
{
    if (!text || strlen(text) != 16) {
        return false;
    }
    *hash = 0;
    for (int i = 0; i < 16; i++) {
        char c = text[i];
        int digit = c >= '0' && c <= '9' ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                         : -1;
        if (digit < 0) {
            return false;
        }
        *hash = (*hash << 4) | (unsigned)digit;
    }
    return true;
}
static void hash_add(yyjson_mut_doc *doc, yyjson_mut_val *out, const char *key, uint64_t value)
{
    char text[17];
    snprintf(text, sizeof text, "%016llx", (unsigned long long)value);
    yyjson_mut_obj_add_strcpy(doc, out, key, text);
}
static bool measure(
    void *context,
    const image_import_proposal *proposals,
    int count,
    authoring_visual_budget *budget,
    char *error,
    size_t size
)
{
    return authoring_project_visual_measure(context, proposals, count, budget, error, size);
}
static void report(yyjson_mut_doc *doc, yyjson_mut_val *out, const image_import_file_plan *plan)
{
    hash_add(doc, out, "plan_hash", plan->hash);
    yyjson_mut_obj_add_strcpy(doc, out, "source", plan->source);
    yyjson_mut_obj_add_strcpy(doc, out, "destination", plan->destination);
    yyjson_mut_obj_add_str(
        doc,
        out,
        "semantic",
        plan->options.semantic == IMAGE_IMPORT_COLOR    ? "color"
        : plan->options.semantic == IMAGE_IMPORT_SCALAR ? "scalar"
                                                        : "normal"
    );
    const image_import_plan *image = &plan->image;
    yyjson_mut_val *source = yyjson_mut_obj_add_obj(doc, out, "source_image");
    hash_add(doc, source, "hash", image->source_hash);
    yyjson_mut_obj_add_str(doc, source, "format", image->source_format);
    yyjson_mut_obj_add_int(doc, source, "width", image->source_width);
    yyjson_mut_obj_add_int(doc, source, "height", image->source_height);
    yyjson_mut_obj_add_uint(doc, source, "encoded_bytes", image->source_bytes);
    yyjson_mut_val *output = yyjson_mut_obj_add_obj(doc, out, "output_image");
    hash_add(doc, output, "hash", image->output_hash);
    yyjson_mut_obj_add_str(doc, output, "format", "png");
    yyjson_mut_obj_add_int(doc, output, "width", image->width);
    yyjson_mut_obj_add_int(doc, output, "height", image->height);
    yyjson_mut_obj_add_uint(doc, output, "encoded_bytes", image->png_bytes);
    yyjson_mut_obj_add_uint(doc, output, "pixel_bytes", image->pixel_bytes);
    yyjson_mut_obj_add_uint(doc, output, "prepared_bytes", image->prepared_bytes);
    const authoring_visual_budget *budget = &plan->budget;
    yyjson_mut_val *visual = yyjson_mut_obj_add_obj(doc, out, "visual_budget");
    yyjson_mut_obj_add_str(doc, visual, "scope", "prepared visual codec payloads; excludes complete player envelope, retained, peak and GPU memory");
    hash_add(doc, visual, "current_hash", budget->current_hash);
    hash_add(doc, visual, "proposed_hash", budget->proposed_hash);
    yyjson_mut_obj_add_uint(doc, visual, "current_bytes", budget->current_bytes);
    yyjson_mut_obj_add_uint(doc, visual, "proposed_bytes", budget->proposed_bytes);
    yyjson_mut_obj_add_uint(doc, visual, "current_pixel_bytes", budget->current_pixel_bytes);
    yyjson_mut_obj_add_uint(doc, visual, "proposed_pixel_bytes", budget->proposed_pixel_bytes);
    yyjson_mut_obj_add_uint(doc, visual, "limit_bytes", budget->limit_bytes);
    yyjson_mut_obj_add_uint(
        doc,
        visual,
        "headroom_bytes",
        budget->proposed_bytes < budget->limit_bytes
            ? budget->limit_bytes - budget->proposed_bytes
            : 0
    );
    yyjson_mut_obj_add_int(doc, visual, "current_count", budget->current_count);
    yyjson_mut_obj_add_int(doc, visual, "proposed_count", budget->proposed_count);
    yyjson_mut_obj_add_int(doc, visual, "limit_count", budget->limit_count);
    yyjson_mut_obj_add_bool(doc, visual, "fits", budget->fits);
    yyjson_mut_val *costs = yyjson_mut_obj_add_arr(doc, visual, "kinds");
    for (int i = 0; i < PLAYER_ASSET_COUNT; i++) {
        yyjson_mut_val *cost = yyjson_mut_arr_add_obj(doc, costs);
        yyjson_mut_obj_add_int(doc, cost, "kind", i);
        yyjson_mut_obj_add_int(doc, cost, "current_count", budget->current[i].count);
        yyjson_mut_obj_add_uint(doc, cost, "current_bytes", budget->current[i].bytes);
        yyjson_mut_obj_add_int(doc, cost, "proposed_count", budget->proposed[i].count);
        yyjson_mut_obj_add_uint(doc, cost, "proposed_bytes", budget->proposed[i].bytes);
    }
}

int automation_image_import(
    const authoring_project_options *options,
    yyjson_val *request,
    bool apply,
    yyjson_mut_doc *doc,
    yyjson_mut_val *out,
    char *error,
    size_t size
)
{
    const char *source = string(request, "source"),
               *destination = string(request, "destination");
    const char *resolution = string(request, "size"),
               *semantic = string(request, "semantic");
    if (!source || !source[0] || !destination || !destination[0] ||
        (yyjson_obj_get(request, "size") && !resolution) ||
        (yyjson_obj_get(request, "semantic") && !semantic)) {
        return invalid(error, size, "Image import requires source and destination strings");
    }
    image_import_file_options selection = { .max_side = 1024,
                                            .semantic = IMAGE_IMPORT_COLOR };
    if (resolution) {
        if (!strcmp(resolution, "source")) {
            selection.max_side = 0;
        } else if (!strcmp(resolution, "2k")) {
            selection.max_side = 2048;
        } else if (strcmp(resolution, "1k")) {
            return invalid(error, size, "Image import size must be source, 1k or 2k");
        }
    }
    if (semantic) {
        if (!strcmp(semantic, "scalar")) {
            selection.semantic = IMAGE_IMPORT_SCALAR;
        } else if (!strcmp(semantic, "normal")) {
            selection.semantic = IMAGE_IMPORT_NORMAL;
        } else if (strcmp(semantic, "color")) {
            return invalid(error, size, "Image import semantic must be color, scalar or normal");
        }
    }
    uint64_t expected = 0;
    if (apply && !hash_read(string(request, "plan_hash"), &expected)) {
        return invalid(error, size, "import_image requires the 16-digit plan_hash returned by plan_image_import");
    }
    chibi_project project = { 0 };
    if (chibi_project_read(&project, options->project, error, size)) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    image_import_budget_host host = { (void *)options, measure };
    image_import_file_plan plan = { 0 };
    bool valid = image_import_file_prepare(
        project.content,
        source,
        destination,
        selection,
        host,
        &plan,
        error,
        size
    );
    if (!valid) {
        chibi_project_free(&project);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    report(doc, out, &plan);
    int status = CHIBI_AUTOMATION_OK;
    if (apply && expected != plan.hash) {
        if (size) {
            snprintf(error, size, "Image import plan is stale; inspect a fresh plan before applying");
        }
        status = CHIBI_AUTOMATION_CONFLICT;
    } else if (apply && !image_import_file_commit(project.content, &plan, host, error, size)) {
        status = CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    yyjson_mut_obj_add_bool(doc, out, "applied", apply && status == CHIBI_AUTOMATION_OK);
    image_import_file_plan_free(&plan);
    chibi_project_free(&project);
    return status;
}
