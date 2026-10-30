#ifndef CHIBI_SHADER_CATALOG_RECORDS_H
#define CHIBI_SHADER_CATALOG_RECORDS_H
#include "render/shader_catalog.h"
#include "render/shader_values.h"

/* Compiled CPU records contain owning canonical IR, typed parameter metadata
 * and inspections. Names, arrays and IR retire together through free_asset.
 * Runtime cache storage is opaque and belongs exclusively to this owner.
 * Record/table mutation and publication require a quiescent catalog. Borrowed
 * record pointers survive until the next record replacement or table exchange;
 * resource generations must exchange the complete table before publishing their
 * matching token through catalog_publication.h. No source document is retained. */
typedef struct
{
    char *name;
    shader_graph graph;
    char *glsl; /* Structural GLSL used to detect topology changes. */
    int *images;
    char *names[SHADER_PARAMETER_MAX];
    int widths[SHADER_PARAMETER_MAX];
    float defaults[SHADER_PARAMETER_MAX][4], bounds[2];
    bool shape;
    shader_alpha_mode alpha_mode;
    bool is_model;
    model_program model;
    model_inspection inspection;
    shader_inspection material_inspection;
    struct shader_asset_runtime *runtime; /* Private CPU cache; never persisted. */
} shader_asset;
typedef struct
{
    const shader_asset *asset;
    const char *scope;
    int offset, count;
    uint64_t node;
} model_material_entry;
extern shader_asset *shader_catalog_assets;
extern unsigned shader_catalog_revision, shader_catalog_ramp_revision,
    shader_catalog_parameter_revision;
extern render_model_stats shader_catalog_model_stats;
void shader_catalog_free_asset(shader_asset *);
int shader_catalog_parameter_slot(const shader_asset *, const char *);
bool shader_catalog_model_material_contract_validate(
    const shader_asset *,
    int,
    const shader_asset *,
    shader_error *
);
const shader_asset *shader_catalog_material_asset_find(const char *, const shader_asset *const *, int);
bool shader_catalog_material_source_resources_validate(const shader_asset *const *, int, shader_error *);
bool shader_catalog_model_material_entries(
    const shader_asset *,
    const shader_asset *,
    const shader_asset *const *,
    int,
    model_material_entry[MODEL_MATERIAL_SLOT_MAX + 1],
    int *,
    float[32][4],
    shader_error *
);
bool shader_catalog_binding_parameter_values_from(
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
bool shader_catalog_validate_model_values(const shader_asset *, const float[32][4], shader_error *);
uint64_t shader_values_generation(const shader_asset *, int count, int index);
bool shader_values_prepare(
    const void *,
    size_t,
    const shader_asset *,
    int count,
    shader_values **,
    size_t *,
    shader_error *
);
void shader_values_exchange(shader_values **);
void shader_values_release(shader_values *);
typedef struct
{
    int id;
    const shader_asset *replacement;
} shader_model_migration;
/* Applies the complete model override migration under one owner lock. */
void shader_catalog_model_overrides_migrate(
    const shader_model_migration *,
    int count,
    const char *renamed_from,
    const char *renamed_to
);
#endif
