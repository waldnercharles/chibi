#include "authoring/visual_authoring.h"

#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(shader_error *error, const char *message)
{
    *error = (shader_error){ 0 };
    snprintf(error->message, sizeof error->message, "%s", message);
    return false;
}
void visual_authoring_report_init(visual_authoring_report *report)
{
    *report = (visual_authoring_report){ 0 };
    shader_authoring_report_init(&report->shader);
}
void visual_authoring_report_free(visual_authoring_report *report)
{
    behavior_asset_free(report->source);
    shader_authoring_report_free(&report->shader);
    visual_authoring_report_init(report);
}
static const pin_spec *parameter_pin(const visual_authoring_report *report, int slot)
{
    if (!report || !report->valid || !report->source || slot < 0 ||
        slot >= SHADER_PARAMETER_MAX || !report->shader.source_nodes[slot]) {
        return NULL;
    }
    const node *found = NULL;
    for (int i = 0; i < acount(report->source->graphs); i++) {
        const node *n = graph_node_by_id(
            report->source->graphs[i],
            report->shader.source_nodes[slot]
        );
        if (!n || strcmp(n->desc->name, "Shader Parameter") ||
            n->reference_id != (uint64_t)slot || n->output_count != 1) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = n;
    }
    return found ? &found->output_pins[0] : NULL;
}
pin_type visual_authoring_parameter_storage(const visual_authoring_report *report, int slot)
{
    const pin_spec *pin = parameter_pin(report, slot);
    return pin ? pin->type : PIN_FLOW;
}
bool visual_authoring_parameter_values(const visual_authoring_report *report, int slot, double values[4])
{
    const pin_spec *pin = parameter_pin(report, slot);
    if (!pin || !values) {
        return false;
    }
    double exact[4] = { 0 };
    const pin_value *v = &pin->default_value;
    switch (pin->type) {
        case PIN_BOOL: exact[0] = v->b ? 1 : 0; break;
        case PIN_I32: exact[0] = v->i; break;
        case PIN_F32: exact[0] = v->f; break;
        case PIN_F64: exact[0] = v->d; break;
        case PIN_STRUCT:
            if (!v->ptr || report->shader.widths[slot] < 1 ||
                report->shader.widths[slot] > 4) {
                return false;
            }
            for (int i = 0; i < report->shader.widths[slot]; i++) {
                exact[i] = ((const float *)v->ptr)[i];
            }
            break;
        default: return false;
    }
    memcpy(values, exact, sizeof exact);
    return true;
}
static behavior_asset *read_source(
    const workspace_documents *documents,
    int handle,
    int revision,
    workspace_document_view *view,
    shader_error *error
)
{
    workspace_document_status status = workspace_document_read(documents, handle, view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        fail(error, workspace_document_status_text(status));
        return NULL;
    }
    if (view->revision != revision) {
        fail(error, "Document revision is stale");
        return NULL;
    }
    behavior_asset *source = behavior_asset_load(view->text);
    if (!source) {
        fail(error, graph_last_error());
    }
    return source;
}
bool visual_authoring_inspect_linked(
    const workspace_documents *documents,
    int handle,
    int revision,
    visual_source_linker link,
    void *userdata,
    visual_authoring_report *report
)
{
    visual_authoring_report fresh;
    visual_authoring_report_init(&fresh);
    fresh.document = handle;
    fresh.revision = revision;
    workspace_document_view view;
    fresh.source = read_source(documents, handle, revision, &view, &fresh.error);
    behavior_asset *original = NULL;
    if (fresh.source && link) {
        original = fresh.source;
        behavior_asset *linked = link(userdata, fresh.source);
        fresh.source = linked;
        if (!linked) {
            fail(&fresh.error, "Visual source dependencies could not be linked");
        }
    }
    fresh.valid = fresh.source &&
                  shader_authoring_inspect(fresh.source, &fresh.shader, &fresh.error);
    if (!fresh.valid && fresh.error.node && fresh.source) {
        bool proven = !link;
        if (original) {
            shader_authoring_report local;
            shader_authoring_report_init(&local);
            shader_error diagnostic = { 0 };
            proven = !shader_authoring_inspect(original, &local, &diagnostic) &&
                     diagnostic.node == fresh.error.node &&
                     !strcmp(diagnostic.message, fresh.error.message);
            shader_authoring_report_free(&local);
        }
        const behavior_asset *master = original ? original : fresh.source;
        int matches = 0;
        for (int i = 0; proven && i < acount(master->graphs); i++) {
            const graph *g = master->graphs[i];
            if (graph_node_by_id(g, fresh.error.node)) {
                fresh.diagnostic_graph = g->graph_id;
                fresh.diagnostic_node = fresh.error.node;
                matches++;
            }
        }
        if (matches != 1) {
            fresh.diagnostic_graph = fresh.diagnostic_node = 0;
        }
    }
    behavior_asset_free(original);
    visual_authoring_report_free(report);
    *report = fresh;
    return fresh.valid;
}
bool visual_authoring_inspect(
    const workspace_documents *documents,
    int document,
    int revision,
    visual_authoring_report *report
)
{
    return visual_authoring_inspect_linked(documents, document, revision, NULL, NULL, report);
}
char *visual_authoring_prepare_default_exact(
    workspace_documents *documents,
    int handle,
    int revision,
    int slot,
    int width,
    const double values[4],
    shader_error *error
)
{
    workspace_document_view view;
    behavior_asset *source = read_source(documents, handle, revision, &view, error);
    if (!source) {
        return NULL;
    }
    bool ok = shader_authoring_set_default_exact(source, slot, values, width, error);
    char *text = ok ? behavior_asset_save(source) : NULL;
    behavior_asset_free(source);
    if (!ok) {
        return NULL;
    }
    if (!text) {
        fail(error, "Could not serialize the edited visual document");
        return NULL;
    }
    return text;
}

bool visual_authoring_set_default_exact(
    workspace_documents *documents,
    int handle,
    int revision,
    int slot,
    int width,
    const double values[4],
    int *result_revision,
    shader_error *error
)
{
    char *text = visual_authoring_prepare_default_exact(documents, handle, revision, slot, width, values, error);
    if (!text) {
        return false;
    }
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, handle, &view);
    if (status == WORKSPACE_DOCUMENT_OK) {
        status = workspace_document_edit(
            documents,
            handle,
            revision,
            0,
            (int)view.bytes,
            text
        );
    }
    free(text);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(error, workspace_document_status_text(status));
    }
    workspace_document_read(documents, handle, &view);
    *result_revision = view.revision;
    return true;
}

bool visual_authoring_sample(
    const visual_authoring_report *report,
    float u,
    float v,
    float time,
    shader_image_resolver resolver,
    void *userdata,
    float outputs[SHADER_OUTPUT_MAX][4],
    shader_error *error
)
{
    if (!report->valid) {
        return fail(error, report->error.message);
    }
    if (!isfinite(u) || !isfinite(v) || !isfinite(time)) {
        return fail(error, "Sample coordinates and time must be finite");
    }
    shader_sample_context context = shader_authoring_center_context(report->source);
    context.uv[0] = u;
    context.uv[1] = v;
    context.time = time;
    context.resolve_image = resolver;
    context.image_userdata = userdata;
    return shader_authoring_sample(&report->shader, &context, NULL, outputs, error);
}
char *visual_authoring_preset_source(const char *name, int preset, shader_error *error)
{
    if (preset < 0 || preset >= MATERIAL_AUTHORING_PRESET_COUNT) {
        fail(error, "Unknown material preset");
        return NULL;
    }
    standard_material material = material_authoring_defaults(
        (material_authoring_preset)preset
    );
    return material_authoring_source(name, &material, error);
}

bool visual_material_preset_settings(int preset, visual_material_settings *out, shader_error *error)
{
    if (!out || preset < 0 || preset >= MATERIAL_AUTHORING_PRESET_COUNT) {
        return fail(error, "Unknown material preset");
    }
    standard_material m = material_authoring_defaults((material_authoring_preset)preset);
    *out = (visual_material_settings){ .opacity = m.base_color[3],
                                       .roughness = m.roughness,
                                       .metallic = m.metallic,
                                       .emission_gain = 1,
                                       .alpha_mode = m.alpha_mode,
                                       .alpha_cutoff = m.alpha_cutoff,
                                       .unlit = m.unlit };
    memcpy(out->base_rgb, m.base_color, sizeof out->base_rgb);
    memcpy(out->emission_rgb, m.emission, sizeof out->emission_rgb);
    return true;
}
static standard_material material_settings_value(const visual_material_settings *settings)
{
    standard_material material = material_authoring_defaults(MATERIAL_AUTHORING_MATTE);
    memcpy(material.base_color, settings->base_rgb, sizeof settings->base_rgb);
    material.base_color[3] = settings->opacity;
    for (int i = 0; i < 3; i++) {
        material.emission[i] = settings->emission_rgb[i] * settings->emission_gain;
    }
    material.roughness = settings->roughness;
    material.metallic = settings->metallic;
    material.alpha_mode = settings->alpha_mode;
    material.alpha_cutoff = settings->alpha_cutoff;
    material.unlit = settings->unlit;
    return material;
}
char *visual_material_source(const char *name, const visual_material_settings *settings, shader_error *error)
{
    if (!settings || !isfinite(settings->emission_gain) || settings->emission_gain < 0) {
        fail(error, "Material emission gain must be finite and nonnegative");
        return NULL;
    }
    standard_material material = material_settings_value(settings);
    return material_authoring_source(name, &material, error);
}

static bool material_open_namespace(
    const workspace_documents *documents,
    const char *name,
    const char *target,
    char *error,
    size_t size
)
{
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    if (count < 0 || count > WORKSPACE_DOCUMENT_COUNT_MAX) {
        snprintf(error, size, "Open document namespace is unavailable");
        return false;
    }
    for (int i = 0; i < count; i++) {
        workspace_document_view view;
        workspace_document_status status = workspace_document_read(documents, handles[i], &view);
        if (status != WORKSPACE_DOCUMENT_OK) {
            snprintf(error, size, "%s", workspace_document_status_text(status));
            return false;
        }
        if (view.path && !strcmp(view.path, target)) {
            snprintf(error, size, "Material path is already owned by an open document");
            return false;
        }
        const char *extension = view.path ? strrchr(view.path, '.') : NULL;
        if (extension && strcmp(extension, ".kdl")) {
            continue;
        }
        char problem[256];
        av_doc *doc = av_doc_read(view.text, problem, sizeof problem);
        if (!doc) {
            bool visual = view.path && (strstr(view.path, "/materials/") ||
                                        strstr(view.path, "/shapes/") ||
                                        strstr(view.path, "/models/") ||
                                        !strncmp(view.path, "materials/", 10) ||
                                        !strncmp(view.path, "shapes/", 7) ||
                                        !strncmp(view.path, "models/", 7));
            if (visual) {
                snprintf(error, size, "An open shader namespace draft cannot be inspected: %s", view.path);
                return false;
            }
            continue;
        }
        const av_value *root = av_doc_root(doc);
        const char *domain = av_get_str(av_obj_get(root, "domain"));
        const char *declared = av_get_str(av_obj_get(root, "name"));
        bool visual = domain &&
                      (!strcmp(domain, "material") ||
                       !strcmp(domain, "shape") || !strcmp(domain, "model"));
        bool collision = visual && declared && !strcmp(declared, name);
        av_doc_free(doc);
        if (collision) {
            snprintf(error, size, "Shader declaration %s is already owned by an open source or draft", name);
            return false;
        }
    }
    return true;
}
static bool canonical_root(const char *path)
{
    if (!path || !*path || strchr(path, '\\') ||
        (path[0] != '/' && !(strlen(path) > 2 && path[1] == ':' && path[2] == '/'))) {
        return false;
    }
    const char *part = path;
    while (*part) {
        while (*part == '/') {
            part++;
        }
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if ((length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.')) {
            return false;
        }
        if (!end) {
            break;
        }
        part = end + 1;
    }
    return true;
}
char *visual_material_create(
    const workspace_documents *documents,
    const char *content,
    const char *workspace,
    const char *name,
    const visual_material_settings *settings,
    char *error,
    size_t size
)
{
    if (!documents || !canonical_root(content) || !canonical_root(workspace) ||
        !name || !*name || !settings || !isfinite(settings->emission_gain) ||
        settings->emission_gain < 0) {
        snprintf(error, size, "Material creation requires host roots, a name and finite settings");
        return NULL;
    }
    char *root = xstrdup(workspace), *folder = xstrdup(content);
    size_t length = strlen(root), folder_length = strlen(folder);
    while (length > 1 && root[length - 1] == '/') {
        root[--length] = 0;
    }
    while (folder_length > 1 && folder[folder_length - 1] == '/') {
        folder[--folder_length] = 0;
    }
    bool prefix = !strncmp(folder, root, length) &&
                  ((length == 1 && root[0] == '/') || !folder[length] ||
                   folder[length] == '/');
    if (!prefix) {
        free(root);
        free(folder);
        snprintf(error, size, "Content root must be inside the configured workspace");
        return NULL;
    }
    const char *relative = folder + length;
    while (*relative == '/') {
        relative++;
    }
    size_t bytes = strlen(relative) + strlen(name) + 32;
    char *target = xmalloc(bytes);
    snprintf(target, bytes, "%s%smaterials/%s.kdl", relative, *relative ? "/" : "", name);
    if (!material_open_namespace(documents, name, target, error, size)) {
        free(root);
        free(folder);
        free(target);
        return NULL;
    }
    standard_material material = material_settings_value(settings);
    char *absolute = material_authoring_create(folder, name, &material, error, size);
    bool created = absolute != NULL;
    free(absolute);
    free(root);
    free(folder);
    if (!created) {
        free(target);
        return NULL;
    }
    return target;
}

char *visual_authoring_prepare_default(
    workspace_documents *documents,
    int handle,
    int revision,
    int slot,
    int width,
    const float values[4],
    shader_error *error
)
{
    if (!values || width < 1 || width > 4) {
        fail(error, "Invalid parameter default values");
        return NULL;
    }
    double exact[4] = { 0 };
    for (int i = 0; i < width; i++) {
        exact[i] = values[i];
    }
    return visual_authoring_prepare_default_exact(documents, handle, revision, slot, width, exact, error);
}
bool visual_authoring_set_default(
    workspace_documents *documents,
    int handle,
    int revision,
    int slot,
    int width,
    const float values[4],
    int *result_revision,
    shader_error *error
)
{
    if (!values || width < 1 || width > 4) {
        return fail(error, "Invalid parameter default values");
    }
    double exact[4] = { 0 };
    for (int i = 0; i < width; i++) {
        exact[i] = values[i];
    }
    return visual_authoring_set_default_exact(documents, handle, revision, slot, width, exact, result_revision, error);
}
