#include "automation/render_preview.h"

#include "automation/asset_write.h"
#include "automation/controller.h"
#include "graph/compiler/compile.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "render/shader_preview.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
    PREVIEW_OK = CHIBI_AUTOMATION_OK,
    PREVIEW_INVALID_REQUEST = CHIBI_AUTOMATION_INVALID_REQUEST,
    PREVIEW_IO_ERROR = CHIBI_AUTOMATION_IO_ERROR,
    PREVIEW_VALIDATION_ERROR = CHIBI_AUTOMATION_VALIDATION_ERROR,
    PREVIEW_EXECUTION_ERROR = CHIBI_AUTOMATION_EXECUTION_ERROR,
    PREVIEW_NOT_FOUND = CHIBI_AUTOMATION_NOT_FOUND,
    PREVIEW_DEFAULT_SIDE = 256,
    PREVIEW_MIN_SIDE = 16,
    PREVIEW_MAX_SIDE = 512,
    PREVIEW_MAX_SOURCE = 64 * 1024,
    PREVIEW_MAX_ERROR = 256,
    PREVIEW_MAX_SAMPLE_SIDE = 16,
};

typedef struct
{
    const char *kind;
    behavior_domain domain;
    const char *folder;
} preview_family;

static const preview_family PREVIEW_FAMILIES[] = {
    { "material", BEHAVIOR_DOMAIN_MATERIAL, "materials" },
    { "shape", BEHAVIOR_DOMAIN_SHAPE, "shapes" },
    { "shader-library", BEHAVIOR_DOMAIN_SHADER_LIBRARY, "shader-libraries" },
};

typedef struct
{
    const preview_family *family;
    char filename[256];
    char relative[512];
    char path[PATH_MAX];
    size_t bytes;
    uint64_t hash;
} preview_ref;

typedef struct
{
    bool enabled;
    float center_x, center_y;
    float span_width, span_height;
} preview_camera;

typedef struct
{
    bool enabled;
    int columns, rows;
    float uv_min[2], uv_max[2];
} preview_sample_grid;

typedef struct
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    yyjson_mut_val *result;
    yyjson_mut_val *diagnostics;
    int status;
} preview_response;

static void set_error(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message ? message : "unknown error");
    }
}

static void set_errorf(char *error, size_t size, const char *format, const char *value)
{
    if (size) {
        snprintf(error, size, format, value ? value : "");
    }
}

static bool read_preview_pair(yyjson_val *object, const char *key, float pair[2], char *error, size_t error_size)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    if (!value || !yyjson_is_arr(value) || yyjson_arr_size(value) != 2) {
        snprintf(error, error_size, "%s must be an array of two finite numbers", key);
        return false;
    }
    for (int i = 0; i < 2; i++) {
        yyjson_val *component = yyjson_arr_get(value, (size_t)i);
        if (!yyjson_is_num(component) || !isfinite(yyjson_get_num(component)) ||
            !isfinite((float)yyjson_get_num(component))) {
            snprintf(error, error_size, "%s must be an array of two finite numbers", key);
            return false;
        }
        pair[i] = (float)yyjson_get_num(component);
    }
    return true;
}

static bool parse_preview_camera(yyjson_val *value, preview_camera *camera, char *error, size_t error_size)
{
    *camera = (preview_camera){ 0 };
    if (!value) {
        return true;
    }
    float center[2], span[2];
    if (!yyjson_is_obj(value) ||
        !read_preview_pair(value, "center", center, error, error_size) ||
        !read_preview_pair(value, "span", span, error, error_size)) {
        if (!error[0]) {
            set_error(error, error_size, "camera must be an object with center and span arrays");
        }
        return false;
    }
    camera->center_x = center[0];
    camera->center_y = center[1];
    camera->span_width = span[0];
    camera->span_height = span[1];
    if (!(camera->span_width > 0) || !(camera->span_height > 0) ||
        fabsf(camera->center_x) > 1000000.0f || fabsf(camera->center_y) > 1000000.0f ||
        camera->span_width > 1000000.0f || camera->span_height > 1000000.0f) {
        set_error(error, error_size, "camera center must be bounded and span must be positive and bounded");
        return false;
    }
    camera->enabled = true;
    return true;
}

static bool parse_sample_count(yyjson_val *value, int *out)
{
    if (!value || !yyjson_is_int(value) || yyjson_get_sint(value) < 1 ||
        yyjson_get_sint(value) > PREVIEW_MAX_SAMPLE_SIDE) {
        return false;
    }
    *out = (int)yyjson_get_sint(value);
    return true;
}

static bool parse_preview_sample_grid(yyjson_val *value, preview_sample_grid *grid, char *error, size_t error_size)
{
    *grid = (preview_sample_grid){ .columns = 5, .rows = 5, .uv_max = { 1, 1 } };
    if (!value) {
        return true;
    }
    if (!yyjson_is_obj(value)) {
        set_error(error, error_size, "sample_grid must be an object");
        return false;
    }
    yyjson_val *columns = yyjson_obj_get(value, "columns");
    yyjson_val *rows = yyjson_obj_get(value, "rows");
    if ((columns && !parse_sample_count(columns, &grid->columns)) ||
        (rows && !parse_sample_count(rows, &grid->rows))) {
        snprintf(error, error_size, "sample_grid columns and rows must be integers from 1 to %d", PREVIEW_MAX_SAMPLE_SIDE);
        return false;
    }
    yyjson_val *uv_min = yyjson_obj_get(value, "uv_min");
    yyjson_val *uv_max = yyjson_obj_get(value, "uv_max");
    if ((uv_min && !read_preview_pair(value, "uv_min", grid->uv_min, error, error_size)) ||
        (uv_max && !read_preview_pair(value, "uv_max", grid->uv_max, error, error_size))) {
        return false;
    }
    if (fabsf(grid->uv_min[0]) > 1000000.0f || fabsf(grid->uv_min[1]) > 1000000.0f ||
        fabsf(grid->uv_max[0]) > 1000000.0f || fabsf(grid->uv_max[1]) > 1000000.0f) {
        set_error(error, error_size, "sample_grid UV bounds are too large");
        return false;
    }
    grid->enabled = true;
    return true;
}

static const preview_family *family_by_kind(const char *kind)
{
    for (int i = 0; i < (int)(sizeof PREVIEW_FAMILIES / sizeof *PREVIEW_FAMILIES); i++) {
        if (!strcmp(kind, PREVIEW_FAMILIES[i].kind)) {
            return &PREVIEW_FAMILIES[i];
        }
    }
    return NULL;
}

static const preview_family *family_by_folder(const char *folder)
{
    for (int i = 0; i < (int)(sizeof PREVIEW_FAMILIES / sizeof *PREVIEW_FAMILIES); i++) {
        if (!strcmp(folder, PREVIEW_FAMILIES[i].folder)) {
            return &PREVIEW_FAMILIES[i];
        }
    }
    return NULL;
}

static bool safe_component(const char *value, size_t max)
{
    return value && value[0] && strlen(value) < max && value[0] != '/' &&
           !strchr(value, '/') && !strchr(value, '\\') &&
           !strstr(value, "..") && strcmp(value, ".") && strcmp(value, "..");
}

static bool valid_filename(const char *name)
{
    size_t length = name ? strlen(name) : 0;
    return length > sizeof ".json" - 1 && length < 256 &&
           !strcmp(name + length - (sizeof ".json" - 1), ".json") &&
           safe_component(name, 256);
}

static int resolve_preview_ref(yyjson_val *request, preview_ref *out, char *error, size_t error_size)
{
    yyjson_val *path_value = yyjson_obj_get(request, "path");
    yyjson_val *kind_value = yyjson_obj_get(request, "kind");
    yyjson_val *name_value = yyjson_obj_get(request, "name");
    if (path_value && (!yyjson_is_str(path_value) || !yyjson_get_len(path_value))) {
        set_error(error, error_size, "path must be a non-empty string");
        return PREVIEW_INVALID_REQUEST;
    }
    if (kind_value && (!yyjson_is_str(kind_value) || !yyjson_get_len(kind_value))) {
        set_error(error, error_size, "kind must be a non-empty string");
        return PREVIEW_INVALID_REQUEST;
    }
    if (name_value && (!yyjson_is_str(name_value) || !yyjson_get_len(name_value))) {
        set_error(error, error_size, "name must be a non-empty string");
        return PREVIEW_INVALID_REQUEST;
    }
    if (path_value && (kind_value || name_value)) {
        set_error(error, error_size, "choose path or kind and name, not both");
        return PREVIEW_INVALID_REQUEST;
    }
    memset(out, 0, sizeof *out);
    if (path_value) {
        const char *path = yyjson_get_str(path_value);
        const char prefix[] = "content/";
        if (strncmp(path, prefix, sizeof prefix - 1) ||
            strlen(path) >= sizeof out->relative || strchr(path, '\\') ||
            strstr(path, "..") || path[0] == '/') {
            set_error(error, error_size, "path must stay under content and cannot contain ..");
            return PREVIEW_INVALID_REQUEST;
        }
        char copy[512];
        snprintf(copy, sizeof copy, "%s", path + sizeof prefix - 1);
        char *slash = strchr(copy, '/');
        if (!slash || slash == copy || strchr(slash + 1, '/')) {
            set_error(error, error_size, "path must name one content shader asset");
            return PREVIEW_INVALID_REQUEST;
        }
        *slash = 0;
        out->family = family_by_folder(copy);
        if (!out->family || !valid_filename(slash + 1)) {
            set_error(error, error_size, "path must name a material, shape, or shader library JSON asset");
            return PREVIEW_INVALID_REQUEST;
        }
        snprintf(out->filename, sizeof out->filename, "%s", slash + 1);
        snprintf(out->relative, sizeof out->relative, "%s", path);
        return PREVIEW_OK;
    }
    if (!kind_value || !name_value) {
        set_error(error, error_size, "preview_shader requires path or kind and name");
        return PREVIEW_INVALID_REQUEST;
    }
    out->family = family_by_kind(yyjson_get_str(kind_value));
    if (!out->family || !safe_component(yyjson_get_str(name_value), 240)) {
        set_error(error, error_size, "kind or name is invalid");
        return PREVIEW_INVALID_REQUEST;
    }
    const char *name = yyjson_get_str(name_value);
    if (strstr(name, ".json")) {
        if (!valid_filename(name)) {
            set_error(error, error_size, "name is not a valid JSON asset filename");
            return PREVIEW_INVALID_REQUEST;
        }
        snprintf(out->filename, sizeof out->filename, "%s", name);
    } else {
        if (strlen(name) + sizeof ".json" >= sizeof out->filename) {
            set_error(error, error_size, "name is too long");
            return PREVIEW_INVALID_REQUEST;
        }
        snprintf(out->filename, sizeof out->filename, "%s.json", name);
    }
    snprintf(out->relative, sizeof out->relative, "content/%s/%s", out->family->folder, out->filename);
    return PREVIEW_OK;
}

static bool under_root(const char *root, const char *path)
{
    size_t length = strlen(root);
    return !strncmp(root, path, length) && path[length] == '/';
}

static char *read_ref(const char *root, preview_ref *ref, char *error, size_t error_size)
{
    char root_path[PATH_MAX];
    if (!realpath(root, root_path)) {
        set_errorf(error, error_size, "project root cannot be resolved: %s", strerror(errno));
        return NULL;
    }
    int length = snprintf(ref->path, sizeof ref->path, "%s/%s", root_path, ref->relative);
    if (length < 0 || (size_t)length >= sizeof ref->path) {
        set_error(error, error_size, "asset path is too long");
        return NULL;
    }
    char resolved[PATH_MAX];
    if (!realpath(ref->path, resolved) || !under_root(root_path, resolved)) {
        set_errorf(error, error_size, "asset not found: %s", ref->relative);
        return NULL;
    }
    struct stat info;
    if (stat(resolved, &info) || !S_ISREG(info.st_mode)) {
        set_errorf(error, error_size, "asset not found: %s", ref->relative);
        return NULL;
    }
    FILE *file = fopen(resolved, "rb");
    if (!file) {
        set_errorf(error, error_size, "asset cannot be read: %s", ref->relative);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        set_error(error, error_size, "asset size cannot be read");
        return NULL;
    }
    long size = ftell(file);
    rewind(file);
    if (size < 0 || (unsigned long)size > SIZE_MAX - 1) {
        fclose(file);
        set_error(error, error_size, "asset size is invalid");
        return NULL;
    }
    char *json = xmalloc((size_t)size + 1);
    size_t read = fread(json, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(json);
        set_error(error, error_size, "asset could not be read completely");
        return NULL;
    }
    json[read] = 0;
    ref->bytes = read;
    ref->hash = automation_content_hash(json, read);
    snprintf(ref->path, sizeof ref->path, "%s", resolved);
    return json;
}

static void response_init(preview_response *response)
{
    response->doc = yyjson_mut_doc_new(NULL);
    response->root = yyjson_mut_obj(response->doc);
    yyjson_mut_obj_add_uint(response->doc, response->root, "contract", CHIBI_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_strcpy(response->doc, response->root, "operation", "preview_shader");
    response->result = yyjson_mut_obj_add_obj(response->doc, response->root, "result");
    response->diagnostics = yyjson_mut_obj_add_arr(response->doc, response->result, "diagnostics");
    response->status = PREVIEW_OK;
    yyjson_mut_doc_set_root(response->doc, response->root);
}

static void response_diagnostic(
    preview_response *response,
    const char *severity,
    uint64_t node,
    const char *message
)
{
    yyjson_mut_val *item = yyjson_mut_arr_add_obj(response->doc, response->diagnostics);
    yyjson_mut_obj_add_strcpy(response->doc, item, "severity", severity);
    if (node) {
        yyjson_mut_obj_add_uint(response->doc, item, "node", node);
    }
    yyjson_mut_obj_add_strcpy(response->doc, item, "message", message ? message : "");
}

static void response_error(preview_response *response, int status, const char *message)
{
    response->status = status;
    yyjson_mut_obj_add_bool(response->doc, response->root, "ok", false);
    yyjson_mut_val *error = yyjson_mut_obj_add_obj(response->doc, response->root, "error");
    const char *code = status == CHIBI_AUTOMATION_INVALID_REQUEST
                         ? "invalid_request"
                     : status == CHIBI_AUTOMATION_IO_ERROR ? "io_error"
                     : status == CHIBI_AUTOMATION_VALIDATION_ERROR
                         ? "validation_failed"
                     : status == CHIBI_AUTOMATION_NOT_FOUND ? "asset_not_found"
                     : status == CHIBI_AUTOMATION_CONFLICT ? "asset_conflict"
                                                           : "execution_failed";
    yyjson_mut_obj_add_strcpy(response->doc, error, "code", code);
    yyjson_mut_obj_add_strcpy(response->doc, error, "message", message ? message : "unknown error");
}

static void response_identity(preview_response *response, const preview_ref *ref)
{
    char hash[32], name[256];
    snprintf(hash, sizeof hash, "%016llx", (unsigned long long)ref->hash);
    snprintf(name, sizeof name, "%s", ref->filename);
    char *extension = strrchr(name, '.');
    if (extension) {
        *extension = 0;
    }
    yyjson_mut_obj_add_strcpy(
        response->doc,
        response->result,
        "kind",
        ref->family->kind
    );
    yyjson_mut_obj_add_strcpy(response->doc, response->result, "name", name);
    yyjson_mut_obj_add_strcpy(response->doc, response->result, "path", ref->relative);
    yyjson_mut_obj_add_uint(response->doc, response->result, "bytes", ref->bytes);
    yyjson_mut_obj_add_strcpy(response->doc, response->result, "hash", hash);
}

static char *load_library_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END)) {
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    long size = ftell(file);
    rewind(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    char *json = xmalloc((size_t)size + 1);
    size_t read = fread(json, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) {
        free(json);
        return NULL;
    }
    json[read] = 0;
    return json;
}

static behavior_asset **load_libraries(const char *root)
{
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/content/shader-libraries", root) >=
        (int)sizeof path) {
        return NULL;
    }
    DIR *directory = opendir(path);
    if (!directory) {
        return NULL;
    }
    behavior_asset **libraries = NULL;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        size_t name_length = strlen(entry->d_name);
        if (name_length < sizeof ".json" - 1 ||
            strcmp(entry->d_name + name_length - (sizeof ".json" - 1), ".json")) {
            continue;
        }
        char file_path[PATH_MAX];
        if (snprintf(file_path, sizeof file_path, "%s/%s", path, entry->d_name) >=
            (int)sizeof file_path) {
            continue;
        }
        char resolved[PATH_MAX];
        struct stat info;
        if (!realpath(file_path, resolved) || !under_root(root, resolved) ||
            stat(resolved, &info) || !S_ISREG(info.st_mode)) {
            continue;
        }
        char *json = load_library_file(resolved);
        behavior_asset *asset = json ? behavior_asset_load_json(json) : NULL;
        free(json);
        if (asset && asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
            apush(libraries, asset);
        } else {
            behavior_asset_free(asset);
        }
    }
    closedir(directory);
    return libraries;
}

static void free_libraries(behavior_asset **libraries)
{
    for (int i = 0; i < acount(libraries); i++) {
        behavior_asset_free(libraries[i]);
    }
    afree(libraries);
}

static void add_compile_diagnostics(preview_response *response, const compile_report *report)
{
    for (int i = 0; i < acount(report->items); i++) {
        const compile_diagnostic *item = &report->items[i];
        response_diagnostic(
            response,
            item->severity == CMPL_WARNING ? "warning" : "error",
            item->node_id,
            item->message
        );
    }
}

static char *generated_source(const behavior_asset *asset, preview_response *response)
{
    shader_graph graph;
    shader_graph_init(&graph);
    shader_error error = { 0 };
    if (!shader_graph_from_graph(asset->graph, &graph, &error)) {
        shader_graph_free(&graph);
        response_diagnostic(response, "error", error.node, error.message);
        return NULL;
    }
    char *source = shader_graph_glsl_named_ramps(&graph, "chibi_preview", 0, &error);
    if (!source && error.message[0]) {
        response_diagnostic(response, "error", error.node, error.message);
    }
    shader_graph_free(&graph);
    return source;
}

static char *base64(const void *data, size_t size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopq"
                                   "rstuvwxyz0123456789+/";
    if (size > (SIZE_MAX - 3) / 4 * 3) {
        return NULL;
    }
    size_t output_size = ((size + 2) / 3) * 4;
    char *output = xmalloc(output_size + 1);
    const unsigned char *bytes = data;
    size_t input = 0, output_index = 0;
    while (input < size) {
        uint32_t value = (uint32_t)bytes[input++] << 16;
        bool second = input < size;
        if (second) {
            value |= (uint32_t)bytes[input++] << 8;
        }
        bool third = input < size;
        if (third) {
            value |= bytes[input++];
        }
        output[output_index++] = alphabet[(value >> 18) & 63];
        output[output_index++] = alphabet[(value >> 12) & 63];
        output[output_index++] = second ? alphabet[(value >> 6) & 63] : '=';
        output[output_index++] = third ? alphabet[value & 63] : '=';
    }
    output[output_index] = 0;
    return output;
}

bool chibi_preview_readback(int width, int height, char **encoded, char *error, size_t error_size)
{
    CF_Readback readback = cf_canvas_readback(cf_app_get_canvas());
    if (!readback.id) {
        set_error(error, error_size, "GPU readback could not be started");
        return false;
    }
    int size = cf_readback_size(readback);
    size_t expected = (size_t)width * (size_t)height * sizeof(CF_Pixel);
    if (size <= 0 || (size_t)size != expected) {
        cf_destroy_readback(readback);
        if (error_size) {
            snprintf(error, error_size, "GPU readback returned %d bytes; expected %zu", size, expected);
        }
        return false;
    }
    CF_Pixel *pixels = xmalloc((size_t)size);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        if (cf_get_ticks() - start > cf_get_tick_frequency() * 10) {
            free(pixels);
            cf_destroy_readback(readback);
            set_error(error, error_size, "GPU readback timed out");
            return false;
        }
        cf_sleep(1);
    }
    bool ok = cf_readback_data(readback, pixels, size) == size;
    if (ok) {
        CF_Image image = { width, height, pixels };
        void *png = NULL;
        int png_size = 0;
        ok = !cf_is_error(cf_image_save_png_to_memory(&image, &png, &png_size)) &&
             png && png_size > 0;
        if (ok) {
            *encoded = base64(png, (size_t)png_size);
            ok = *encoded != NULL;
        }
        cf_free(png);
    }
    free(pixels);
    cf_destroy_readback(readback);
    if (!ok) {
        set_error(error, error_size, "GPU readback could not be encoded as PNG");
    }
    return ok;
}

static void add_sample_vector(yyjson_mut_doc *doc, yyjson_mut_val *array, const float value[4])
{
    yyjson_mut_val *entry = yyjson_mut_arr_add_arr(doc, array);
    for (int i = 0; i < 4; i++) {
        yyjson_mut_arr_add_real(doc, entry, value[i]);
    }
}

static bool add_sample_grid(
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    const behavior_asset *asset,
    const char *parameters_json,
    float time,
    const preview_sample_grid *grid,
    char *error,
    size_t error_size
)
{
    int shader = render_shader_find(asset->name);
    if (!shader) {
        set_error(error, error_size, "Shader asset is not registered.");
        return false;
    }
    shader_error parameter_error = { 0 };
    if (!render_shader_parameters_validate(shader, parameters_json, &parameter_error)) {
        set_error(error, error_size, parameter_error.message);
        return false;
    }
    float parameters[SHADER_PARAMETER_MAX][4];
    render_shader_parameters(shader, NULL, (ecs_entity){ 0 }, parameters_json, parameters);
    float scale[2] = { 100, 100 };
    if (render_shader_is_shape(shader) && !render_shader_bounds(shader, scale)) {
        set_error(error, error_size, "Shape bounds are unavailable for shader samples.");
        return false;
    }
    yyjson_mut_val *sampled = yyjson_mut_obj_add_obj(doc, result, "sample_grid");
    yyjson_mut_obj_add_int(doc, sampled, "columns", grid->columns);
    yyjson_mut_obj_add_int(doc, sampled, "rows", grid->rows);
    yyjson_mut_val *uv_min = yyjson_mut_obj_add_arr(doc, sampled, "uv_min");
    yyjson_mut_val *uv_max = yyjson_mut_obj_add_arr(doc, sampled, "uv_max");
    for (int i = 0; i < 2; i++) {
        yyjson_mut_arr_add_real(doc, uv_min, grid->uv_min[i]);
        yyjson_mut_arr_add_real(doc, uv_max, grid->uv_max[i]);
    }
    yyjson_mut_obj_add_strcpy(doc, sampled, "evaluation", "CPU shader graph evaluation at the returned UV coordinates");
    yyjson_mut_val *samples = yyjson_mut_obj_add_arr(doc, sampled, "samples");
    for (int y = 0; y < grid->rows; y++) {
        float row = grid->rows == 1 ? 0.5f : (float)y / (float)(grid->rows - 1);
        for (int x = 0; x < grid->columns; x++) {
            float column = grid->columns == 1
                             ? 0.5f
                             : (float)x / (float)(grid->columns - 1);
            float u = grid->uv_min[0] + (grid->uv_max[0] - grid->uv_min[0]) * column;
            float v = grid->uv_min[1] + (grid->uv_max[1] - grid->uv_min[1]) * row;
            float local_x = (u - 0.5f) * 2.0f * scale[0];
            float local_y = (0.5f - v) * 2.0f * scale[1];
            shader_sample_context context = {
                .position = { local_x, local_y },
                .uv = { u, v },
                .object_position = { local_x, 0, local_y },
                .world_position = { local_x, 0, local_y },
                .normal = { 0, 1, 0 },
                .view_direction = { 0, 1, 0 },
                .time = time,
            };
            float output[SHADER_OUTPUT_MAX][4] = { 0 };
            shader_error sample_error = { 0 };
            if (!render_shader_sample(shader, &context, parameters, output, &sample_error)) {
                set_error(
                    error,
                    error_size,
                    sample_error.message[0] ? sample_error.message : "Shader sample failed."
                );
                return false;
            }
            for (int output_index = 0; output_index < SHADER_OUTPUT_MAX;
                 output_index++) {
                for (int component = 0; component < 4; component++) {
                    if (!isfinite(output[output_index][component])) {
                        set_error(error, error_size, "Shader sample produced a nonfinite output.");
                        return false;
                    }
                }
            }
            yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, samples);
            yyjson_mut_val *uv = yyjson_mut_obj_add_arr(doc, entry, "uv");
            yyjson_mut_arr_add_real(doc, uv, u);
            yyjson_mut_arr_add_real(doc, uv, v);
            yyjson_mut_val *outputs = yyjson_mut_obj_add_arr(doc, entry, "outputs");
            for (int output_index = 0; output_index < SHADER_OUTPUT_MAX;
                 output_index++) {
                add_sample_vector(doc, outputs, output[output_index]);
            }
        }
    }
    return true;
}

static bool render_asset(
    const behavior_asset *asset,
    float time,
    int width,
    int height,
    const char *parameters_json,
    const render_shader_preview_options *options,
    const preview_sample_grid *sample_grid,
    preview_response *response,
    char **png,
    char *error,
    size_t error_size,
    const char *program
)
{
    CF_Result made = cf_make_app(
        "Chibi shader preview",
        0,
        0,
        0,
        width,
        height,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(made)) {
        set_error(error, error_size, made.details);
        return false;
    }
    char mount_error[PREVIEW_MAX_ERROR] = { 0 };
    if (!render_image_mount_content("content", mount_error, sizeof mount_error)) {
        set_error(error, error_size, mount_error);
        cf_destroy_app();
        return false;
    }
    cf_fs_mount(CHIBI_RENDER_SHADER_DIR, "/chibi-render", true);
    cf_shader_directory("/chibi-render");
    shader_error shader_error = { 0 };
    render_shader_preview *renderer = render_shader_preview_new(asset, width, height, options, &shader_error);
    if (!renderer) {
        set_error(error, error_size, shader_error.message[0] ? shader_error.message : "could not create shader preview");
        cf_destroy_app();
        return false;
    }
    cf_app_update(NULL);
    cf_app_set_canvas_size(width, height);
    if (sample_grid && sample_grid->enabled &&
        !add_sample_grid(response->doc, response->result, asset, parameters_json, time, sample_grid, error, error_size)) {
        render_shader_preview_free(renderer);
        render_shader_catalog_clear();
        cf_destroy_app();
        return false;
    }
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, width, height));
    bool rendered = render_shader_preview_draw(renderer, time, parameters_json);
    cf_draw_pop();
    if (rendered) {
        cf_app_draw_onto_screen(true);
        rendered = chibi_preview_readback(width, height, png, error, error_size);
    } else {
        set_error(error, error_size, render_shader_preview_error(renderer));
    }
    render_shader_preview_free(renderer);
    render_shader_catalog_clear();
    cf_destroy_app();
    return rendered;
}

static char *response_finish(preview_response *response)
{
    char *json = yyjson_mut_write(response->doc, 0, NULL);
    yyjson_mut_doc_free(response->doc);
    return json;
}

static bool validate_shader_images(const behavior_asset *asset, const char *root, preview_response *response)
{
    shader_graph graph;
    shader_graph_init(&graph);
    shader_error error = { 0 };
    if (!shader_graph_from_graph(asset->graph, &graph, &error)) {
        shader_graph_free(&graph);
        return false;
    }
    bool valid = true;
    for (int i = 0; i < acount(graph.images); i++) {
        const char *key = graph.images[i].key;
        char path[PATH_MAX], resolved[PATH_MAX];
        struct stat info;
        if (!key || strncmp(key, "content/", sizeof "content/" - 1) ||
            strchr(key, '\\') || strstr(key, "..") || key[0] == '/' ||
            snprintf(path, sizeof path, "%s/%s", root, key) >= (int)sizeof path ||
            !realpath(path, resolved) || !under_root(root, resolved) ||
            stat(resolved, &info) || !S_ISREG(info.st_mode)) {
            response_diagnostic(response, "error", 0, "shader image keys must resolve inside the project content directory");
            valid = false;
        }
    }
    shader_graph_free(&graph);
    return valid;
}

char *chibi_render_preview_handle(const char *request, const char *default_root, int *exit_code)
{
    if (exit_code) {
        *exit_code = PREVIEW_OK;
    }
    preview_response response;
    response_init(&response);
    yyjson_doc *document = request ? yyjson_read(request, strlen(request), 0) : NULL;
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    char error[PREVIEW_MAX_ERROR] = { 0 };
    if (!root || !yyjson_is_obj(root)) {
        response_error(&response, PREVIEW_INVALID_REQUEST, "request must be a JSON object");
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    yyjson_val *operation = yyjson_obj_get(root, "operation");
    if (yyjson_is_str(operation) &&
        !strcmp(yyjson_get_str(operation), "preview_scene")) {
        yyjson_doc_free(document);
        yyjson_mut_doc_free(response.doc);
        return chibi_scene_preview_handle(request, default_root, exit_code);
    }
    if (!yyjson_is_str(operation) ||
        strcmp(yyjson_get_str(operation), "preview_shader")) {
        response_error(&response, PREVIEW_INVALID_REQUEST, "operation must be preview_shader");
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    const char *project_root = default_root && default_root[0] ? default_root : ".";
    yyjson_val *project_value = yyjson_obj_get(root, "project_root");
    if (project_value) {
        if (!yyjson_is_str(project_value) || !yyjson_get_len(project_value)) {
            response_error(&response, PREVIEW_INVALID_REQUEST, "project_root must be a non-empty string");
            if (exit_code) {
                *exit_code = response.status;
            }
            yyjson_doc_free(document);
            return response_finish(&response);
        }
        project_root = yyjson_get_str(project_value);
    }
    int width = PREVIEW_DEFAULT_SIDE, height = PREVIEW_DEFAULT_SIDE;
    yyjson_val *value = yyjson_obj_get(root, "width");
    if (value) {
        if (!yyjson_is_int(value) || yyjson_get_sint(value) < PREVIEW_MIN_SIDE ||
            yyjson_get_sint(value) > PREVIEW_MAX_SIDE) {
            response_error(&response, PREVIEW_INVALID_REQUEST, "width must be between 16 and 512");
            if (exit_code) {
                *exit_code = response.status;
            }
            yyjson_doc_free(document);
            return response_finish(&response);
        }
        width = (int)yyjson_get_sint(value);
    }
    value = yyjson_obj_get(root, "height");
    if (value) {
        if (!yyjson_is_int(value) || yyjson_get_sint(value) < PREVIEW_MIN_SIDE ||
            yyjson_get_sint(value) > PREVIEW_MAX_SIDE) {
            response_error(&response, PREVIEW_INVALID_REQUEST, "height must be between 16 and 512");
            if (exit_code) {
                *exit_code = response.status;
            }
            yyjson_doc_free(document);
            return response_finish(&response);
        }
        height = (int)yyjson_get_sint(value);
    }
    float time = 0;
    value = yyjson_obj_get(root, "time");
    if (value) {
        if (!yyjson_is_num(value) || !isfinite(yyjson_get_num(value)) ||
            fabs(yyjson_get_num(value)) > 3600) {
            response_error(&response, PREVIEW_INVALID_REQUEST, "time must be finite and within +/-3600 seconds");
            if (exit_code) {
                *exit_code = response.status;
            }
            yyjson_doc_free(document);
            return response_finish(&response);
        }
        time = (float)yyjson_get_num(value);
    }
    yyjson_val *parameters = yyjson_obj_get(root, "parameters");
    if (parameters) {
        if (!yyjson_is_obj(parameters) ||
            yyjson_obj_size(parameters) > SHADER_PARAMETER_MAX) {
            response_error(&response, PREVIEW_INVALID_REQUEST, "parameters must be an object with at most 32 entries");
            if (exit_code) {
                *exit_code = response.status;
            }
            yyjson_doc_free(document);
            return response_finish(&response);
        }
    }
    preview_camera camera;
    if (!parse_preview_camera(yyjson_obj_get(root, "camera"), &camera, error, sizeof error)) {
        response_error(&response, PREVIEW_INVALID_REQUEST, error);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    preview_sample_grid sample_grid;
    if (!parse_preview_sample_grid(yyjson_obj_get(root, "sample_grid"), &sample_grid, error, sizeof error)) {
        response_error(&response, PREVIEW_INVALID_REQUEST, error);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    bool include_source = false;
    value = yyjson_obj_get(root, "include_source");
    if (value) {
        if (!yyjson_is_bool(value)) {
            response_error(&response, PREVIEW_INVALID_REQUEST, "include_source must be a boolean");
            if (exit_code) {
                *exit_code = response.status;
            }
            yyjson_doc_free(document);
            return response_finish(&response);
        }
        include_source = yyjson_get_bool(value);
    }

    preview_ref ref;
    int status = resolve_preview_ref(root, &ref, error, sizeof error);
    if (status != PREVIEW_OK) {
        response_error(&response, status, error);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    char *json = read_ref(project_root, &ref, error, sizeof error);
    if (!json) {
        response_error(&response, PREVIEW_NOT_FOUND, error);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    response_identity(&response, &ref);
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    if (!asset) {
        response_diagnostic(&response, "error", 0, graph_last_error());
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_VALIDATION_ERROR, "asset is not a valid behavior asset");
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    if (asset->domain != ref.family->domain) {
        behavior_asset_free(asset);
        response_diagnostic(&response, "error", 0, "asset domain does not match its requested kind");
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_VALIDATION_ERROR, "asset domain does not match its path");
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    if (asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        behavior_asset_free(asset);
        response_diagnostic(&response, "error", 0, "shader libraries are reusable functions; preview a material or shape that consumes one");
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_INVALID_REQUEST, "shader-library assets have no standalone image preview");
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }

    char root_abs[PATH_MAX];
    if (!realpath(project_root, root_abs)) {
        behavior_asset_free(asset);
        response_error(&response, PREVIEW_IO_ERROR, "project root cannot be resolved");
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    behavior_asset **libraries = load_libraries(root_abs);
    char link_error[PREVIEW_MAX_ERROR] = { 0 };
    behavior_asset *linked = behavior_asset_link_libraries(
        asset,
        (const behavior_asset *const *)libraries,
        acount(libraries),
        link_error,
        sizeof link_error
    );
    free_libraries(libraries);
    if (!linked) {
        response_diagnostic(&response, "error", 0, link_error[0] ? link_error : "shader library linking failed");
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_VALIDATION_ERROR, "shader library linking failed");
        behavior_asset_free(asset);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    behavior_asset_free(asset);
    compile_report report;
    compile_report_init(&report);
    behavior_asset_compile(linked, &report);
    add_compile_diagnostics(&response, &report);
    if (report.error_count) {
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_VALIDATION_ERROR, "shader graph validation failed");
        compile_report_free(&report);
        behavior_asset_free(linked);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    if (!validate_shader_images(linked, root_abs, &response)) {
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_VALIDATION_ERROR, "shader image paths must remain inside the project");
        compile_report_free(&report);
        behavior_asset_free(linked);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    if (include_source) {
        char *source = generated_source(linked, &response);
        if (source) {
            if (strlen(source) > PREVIEW_MAX_SOURCE) {
                source[PREVIEW_MAX_SOURCE] = 0;
                yyjson_mut_obj_add_bool(response.doc, response.result, "source_truncated", true);
                response_diagnostic(&response, "warning", 0, "generated shader source truncated to 65536 bytes");
            }
            yyjson_mut_obj_add_strcpy(response.doc, response.result, "source", source);
        }
        free(source);
    }
    compile_report_free(&report);
    char render_error[PREVIEW_MAX_ERROR] = { 0 };
    char *png = NULL;
    char *parameters_json = parameters ? yyjson_val_write(parameters, 0, NULL) : NULL;
    if (parameters && !parameters_json) {
        response_diagnostic(&response, "error", 0, "parameters could not be serialized");
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_INVALID_REQUEST, "parameters could not be serialized");
        behavior_asset_free(linked);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    render_shader_preview_options preview_options = {
        .has_camera = camera.enabled,
        .center_x = camera.center_x,
        .center_y = camera.center_y,
        .span_width = camera.span_width,
        .span_height = camera.span_height,
    };
    char original_directory[PATH_MAX];
    if (!getcwd(original_directory, sizeof original_directory) || chdir(root_abs)) {
        response_diagnostic(&response, "error", 0, "project root could not be opened for GPU preview");
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_IO_ERROR, "project root could not be opened");
        free(parameters_json);
        behavior_asset_free(linked);
        if (exit_code) {
            *exit_code = response.status;
        }
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    bool rendered = render_asset(
        linked,
        time,
        width,
        height,
        parameters_json,
        &preview_options,
        &sample_grid,
        &response,
        &png,
        render_error,
        sizeof render_error,
        "chibi_render_ctl"
    );
    free(parameters_json);
    chdir(original_directory);
    behavior_asset_free(linked);
    if (!rendered) {
        response_diagnostic(&response, "error", 0, render_error[0] ? render_error : "shader preview failed");
        yyjson_mut_obj_add_bool(response.doc, response.result, "valid", false);
        response_error(&response, PREVIEW_EXECUTION_ERROR, "shader preview failed");
        if (exit_code) {
            *exit_code = response.status;
        }
        free(png);
        yyjson_doc_free(document);
        return response_finish(&response);
    }
    yyjson_mut_obj_add_bool(response.doc, response.result, "valid", true);
    yyjson_mut_val *preview = yyjson_mut_obj_add_obj(response.doc, response.result, "preview");
    yyjson_mut_obj_add_strcpy(response.doc, preview, "mime_type", "image/png");
    yyjson_mut_obj_add_int(response.doc, preview, "width", width);
    yyjson_mut_obj_add_int(response.doc, preview, "height", height);
    yyjson_mut_obj_add_strcpy(response.doc, preview, "data", png);
    free(png);
    yyjson_mut_obj_add_bool(response.doc, response.root, "ok", true);
    if (exit_code) {
        *exit_code = PREVIEW_OK;
    }
    yyjson_doc_free(document);
    return response_finish(&response);
}
