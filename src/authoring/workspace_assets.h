#ifndef CHIBI_WORKSPACE_ASSETS_H
#define CHIBI_WORKSPACE_ASSETS_H
#include "assets/asset_schema.h"
#include "assets/new_asset.h"
#include "assets/prototype.h"
#include "authoring/workspace_documents.h"
#include "support/arena.h"

typedef struct
{
    char *path, *text;
} workspace_asset_template_result;
/* Canonical singular kind names, independent of menu labels and enum values.
 * No file is created. Results own both strings until free. */
int workspace_asset_kind_count();
const char *workspace_asset_kind_name(int);
const char *workspace_asset_kind_folder(int);
const char *workspace_asset_kind_extension(int);
bool workspace_asset_template(
    const char *kind,
    const char *name,
    workspace_asset_template_result *,
    char *error,
    size_t size
);
void workspace_asset_template_free(workspace_asset_template_result *);
bool workspace_asset_identity(
    const workspace_documents *,
    int document,
    int revision,
    char **kind,
    char **name,
    char *error,
    size_t size
);

typedef struct
{
    const char *path, *name, *type, *text, *description, *reference, *type_name,
        *unit, *relative, *component, *presentation_category, *icon;
    /* Owned exact JSON for resolved scalar rows; empty for containers/missing. */
    const char *json;
    int parent, depth;
    bool present, required, boolean, bounded, color, common, mixed, computed,
        added, overridden;
    double number, min, max;
} workspace_asset_field;
typedef struct
{
    const char *path, *value;
} workspace_asset_choice;
typedef struct
{
    arena storage;
    workspace_asset_field *fields;
    workspace_asset_choice *choices;
    const prototype_registry *registry; /* Borrowed only during inspection. */
} workspace_asset_inspection;
bool workspace_asset_inspect(
    const workspace_documents *,
    int document,
    int revision,
    const char *kind,
    workspace_asset_inspection *,
    char *error,
    size_t size
);
bool workspace_asset_inspect_registry(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *kind,
    workspace_asset_inspection *,
    char *error,
    size_t size
);
void workspace_asset_inspection_free(workspace_asset_inspection *);
/* JSON-pointer paths use ~0/~1 escapes and exact array indices. set replaces
 * or creates an object member; remove detaches a member/item; append adds one
 * item to an array. Values are typed KDL literals, not executable expressions.
 * Complete structural/canonical loader checks precede one revisioned edit. */
bool workspace_asset_edit(
    workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *operation,
    const char *path,
    const char *literal,
    char *error,
    size_t size
);
/* Owns a bounded candidate through the identical schema edit/validation path;
 * changes no caller document, revision, history or registry source. */
bool workspace_asset_prepare_registry(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *operation,
    const char *path,
    const char *literal,
    char **text,
    char *error,
    size_t
);
bool workspace_asset_edit_registry(
    const prototype_registry *,
    workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *operation,
    const char *path,
    const char *literal,
    char *error,
    size_t size
);
typedef enum
{
    WORKSPACE_SCENE_ADD,
    WORKSPACE_SCENE_RENAME,
    WORKSPACE_SCENE_REMOVE,
    WORKSPACE_SCENE_DUPLICATE,
    WORKSPACE_SCENE_MOVE
} workspace_scene_operation;
bool workspace_asset_component(
    workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *entity,
    const char *component,
    bool add,
    char *error,
    size_t size
);
bool workspace_asset_component_registry(
    const prototype_registry *,
    workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *entity,
    const char *component,
    bool add,
    char *error,
    size_t size
);
bool workspace_scene_edit(
    workspace_documents *,
    int document,
    int revision,
    workspace_scene_operation,
    const char *entity,
    const char *parent,
    const char *before,
    const char *name,
    const char *prototype,
    char *error,
    size_t size
);
bool workspace_scene_edit_registry(
    const prototype_registry *,
    workspace_documents *,
    int document,
    int revision,
    workspace_scene_operation,
    const char *entity,
    const char *parent,
    const char *before,
    const char *name,
    const char *prototype,
    char *error,
    size_t size
);
/* Pure clipboard candidates. At most256 selected roots,1MiB source/payload;
 * nested selections collapse to their top-level root. Returned strings and each
 * returned path are caller-owned. Paste remaps internal typed handles and uses
 * deterministic numeric suffixes for sibling name collisions. */
bool workspace_scene_clipboard(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *const *entities,
    int count,
    bool cut,
    char **payload,
    char **text,
    char *error,
    size_t size
);
bool workspace_scene_paste(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *parent,
    const char *payload,
    char **text,
    char ***paths,
    int *count,
    char *error,
    size_t size
);
/* XYZ is already parent-local. Sparse inherited Transform3D preserves its
 * rotation/scale; a new transform authors unit scale. Source may be a previous
 * private candidate; the canonical source revision is always checked first. */
bool workspace_scene_place_candidate(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *parent,
    const char *prototype_name,
    const char *name,
    float x,
    float y,
    float z,
    const char *source,
    char **text,
    char ***paths,
    int *count,
    char *error,
    size_t size
);
/* Pure, caller-owned source candidates. Apply requires the exact named target
 * document; save-as produces a new exclusive-create path plus instance text. */
bool workspace_prototype_candidate(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *entity,
    int prototype_document,
    int prototype_revision,
    const char *const *paths,
    int count,
    const char *operation,
    const char *name,
    char **object_text,
    char **prototype_text,
    char **new_path,
    char *error,
    size_t size
);
/* Inputs are valid UTF-8 source strings; replacement is caller-owned. */
bool workspace_candidate_difference(
    const char *original,
    const char *candidate,
    int *start,
    int *end,
    char **replacement
);
bool workspace_objects_candidate(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *const *entities,
    int count,
    const char *relative,
    const char *operation,
    const char *value,
    const char *source,
    char **text,
    char *error,
    size_t size
);
bool workspace_objects_component(
    const prototype_registry *,
    workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *const *entities,
    int count,
    const char *component,
    bool add,
    char *error,
    size_t size
);
bool workspace_objects_inspect(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *const *entities,
    int count,
    workspace_asset_inspection *,
    char *error,
    size_t size
);
bool workspace_objects_edit(
    const prototype_registry *,
    workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *const *entities,
    int count,
    const char *relative,
    const char *operation,
    const char *value,
    char *error,
    size_t size
);
bool workspace_object_inspect(
    const prototype_registry *,
    const workspace_documents *,
    int document,
    int revision,
    const char *kind,
    const char *entity,
    workspace_asset_inspection *,
    char *error,
    size_t size
);
typedef struct
{
    workspace_documents *documents;
    const prototype_registry *registry; /* Borrowed project catalog, defaults and include resolver. */
    void *scope; /* Optional factory-owned source scope. */
} workspace_assets_service;
#endif
