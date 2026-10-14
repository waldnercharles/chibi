#ifndef CHIBI_VISUAL_AUTHORING_H
#define CHIBI_VISUAL_AUTHORING_H
#include "authoring/material_create.h"
#include "authoring/shader_report.h"
#include "authoring/workspace_documents.h"

/* CPU-only document operations. The canonical text store owns history and
 * transactions. Reports own their parsed source and compilation result; neither
 * a document edit nor closing its handle invalidates an existing report. */
typedef struct
{
    float base_rgb[3], opacity, roughness, metallic;
    float emission_rgb[3], emission_gain;
    int alpha_mode;
    float alpha_cutoff;
    bool unlit;
} visual_material_settings;
bool visual_material_preset_settings(int preset, visual_material_settings *, shader_error *);
char *visual_material_source(const char *name, const visual_material_settings *, shader_error *);
/* Exclusive disk factory with shared shader namespace checks, including owned
 * open source/drafts. Returns an owned workspace-relative canonical path. */
char *visual_material_create(
    const workspace_documents *,
    const char *content_root,
    const char *workspace_root,
    const char *name,
    const visual_material_settings *,
    char *error,
    size_t
);
typedef struct
{
    int document, revision;
    behavior_asset *source;
    shader_authoring_report shader;
    shader_error error;
    /* Exact source-local diagnostic; zero when linker provenance is unproved. */
    uint64_t diagnostic_graph, diagnostic_node;
    bool valid;
} visual_authoring_report;
typedef behavior_asset *(*visual_source_linker)(void *, const behavior_asset *);
/* Optional linker returns an owned source snapshot; called synchronously before
 * compilation, so dependency graphs are compiled only once per request. */
bool visual_authoring_inspect_linked(
    const workspace_documents *,
    int document,
    int revision,
    visual_source_linker,
    void *link_userdata,
    visual_authoring_report *
);
void visual_authoring_report_init(visual_authoring_report *);
void visual_authoring_report_free(visual_authoring_report *);
bool visual_authoring_inspect(const workspace_documents *, int document, int revision, visual_authoring_report *);
/* Exact declared storage for an inspected parameter's retained source node.
 * PIN_FLOW means the declaration is absent or ambiguous; never infer by name. */
pin_type visual_authoring_parameter_storage(const visual_authoring_report *, int slot);
bool visual_authoring_parameter_values(const visual_authoring_report *, int slot, double values[4]);
/* Expected revision is mandatory. Invalid values/source leave document, history
 * and revision unchanged. An existing document transaction groups this edit. */
/* Returns an owned canonical candidate without changing document/history. */
char *visual_authoring_prepare_default(
    workspace_documents *,
    int document,
    int revision,
    int slot,
    int width,
    const float values[4],
    shader_error *
);
bool visual_authoring_set_default(
    workspace_documents *,
    int document,
    int revision,
    int slot,
    int width,
    const float values[4],
    int *result_revision,
    shader_error *
);
/* Exact scalar ingress preserves I32 defaults throughout source editing. */
char *visual_authoring_prepare_default_exact(
    workspace_documents *,
    int document,
    int revision,
    int slot,
    int width,
    const double values[4],
    shader_error *
);
bool visual_authoring_set_default_exact(
    workspace_documents *,
    int document,
    int revision,
    int slot,
    int width,
    const double values[4],
    int *result_revision,
    shader_error *
);
bool visual_authoring_sample(
    const visual_authoring_report *,
    float u,
    float v,
    float time,
    shader_image_resolver,
    void *image_userdata,
    float outputs[SHADER_OUTPUT_MAX][4],
    shader_error *
);
char *visual_authoring_preset_source(const char *name, int preset, shader_error *);
#endif
