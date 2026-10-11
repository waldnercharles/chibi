#ifndef CHIBI_AUTHORING_SHADER_PREPARE_H
#define CHIBI_AUTHORING_SHADER_PREPARE_H
#include "graph/model/behavior_asset.h"
#include "graph/modeling/model_compile.h"
#include "render/shader_catalog.h"
bool render_shader_upsert(const behavior_asset *asset, shader_error *error);
bool render_shader_models_upsert(const behavior_asset *const *sources, int count, shader_error *error);
bool render_shader_model_remove(const char *name, shader_error *error);
bool render_shader_model_rename(const char *from, const char *to, shader_error *error);
bool render_shader_material_remove(const char *name, shader_error *error);
bool render_shader_material_rename(const char *from, const char *to, shader_error *error);
bool render_shader_binding_sources_validate(
    const Shader *binding,
    const char *text,
    const behavior_asset *const *sources,
    int count,
    shader_error *error
);
bool render_shader_parameters_validate(int index, const char *, shader_error *);
bool authoring_shader_parameters_prepare(int index, const char *, shader_error *);
bool authoring_shader_binding_prepare(const Shader *, shader_error *);
void authoring_shader_catalog_clear();
#endif
