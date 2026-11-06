#include "automation/controller.h"

#include "assets/asset_source.h"
#include "assets/model_export.h"
#include "assets/model_import.h"
#include "assets/new_asset.h"
#include "assets/project.h"
#include "assets/prototype.h"
#include "assets/reference_adapters.h"
#include "assets/scenario_document.h"
#include "assets/scene_document.h"
#include "assets/ui_document.h"
#include "assets/ui_events.h"
#include "authoring/application_catalog.h"
#include "authoring/behavior_loader.h"
#include "authoring/component_declarations.h"
#include "authoring/component_presentation.h"
#include "authoring/graph_expression.h"
#include "authoring/image_prepare.h"
#include "authoring/language_catalog.h"
#include "authoring/mesh_prepare.h"
#include "authoring/native_prototype.h"
#include "authoring/preview_source.h"
#include "authoring/project_format.h"
#include "authoring/project_open.h"
#include "authoring/project_prepare.h"
#include "authoring/prototype_preview.h"
#include "authoring/reference_index.h"
#include "authoring/shader_prepare.h"
#include "authoring/sound_prepare.h"
#include "authoring/source_asset.h"
#include "authoring/source_catalog.h"
#include "authoring/source_collection.h"
#include "authoring/source_format.h"
#include "authoring/source_graph_library.h"
#include "authoring/source_intelligence.h"
#include "authoring/source_presentation.h"
#include "authoring/source_references.h"
#include "authoring/source_rename.h"
#include "authoring/studio_project.h"
#include "authoring/studio_schema.h"
#include "authoring/visual_prepare.h"
#include "automation/content_hash.h"
#include "automation/field_preview.h"
#include "automation/material_bake.h"
#include "automation/navigation.h"
#include "automation/ui_inspect.h"
#include "ecs/ecs.h"
#include "execution/backend.h"
#include "execution/function.h"
#include "graph/authoring/describe.h"
#include "graph/authoring/graph_edit.h"
#include "graph/authoring/layout.h"
#include "graph/compiler/validation.h"
#include "graph/document/events.h"
#include "graph/document/graph_signature.h"
#include "graph/document/library.h"
#include "graph/document/problems.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/studio_nodes.h"
#include "graph/shader/shader_graph.h"
#include "host/native_default.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "runtime/behaviors/source.h"
#include "runtime/component_catalog.h"
#include "runtime/models/model_inspection.h"
#include "runtime/terrain_storage.h"
#include "runtime/transform_propagation.h"
#include "runtime/ui.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/file_io.h"
#include "support/text_file.h"
#include "support/text_search.h"
#include "support/xalloc.h"
#include "types/text.h"
#include "types/type.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <yyjson.h>

static behavior_asset *load_project_behavior(const char *, const char *text)
{
    return behavior_asset_load(text);
}

static int inspect_source_presentation(
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    const authoring_source_presentation *presentation,
    char *error,
    size_t size
);

static int analyze_project_source(
    const char *root,
    yyjson_val *request,
    bool compile,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
);

typedef enum
{
    ASSET_GRAPH,
    ASSET_SYSTEM,
    ASSET_PROTOTYPE,
    ASSET_RELATIONSHIP,
    ASSET_SCENE,
    ASSET_EDITOR,
    ASSET_UI,
    ASSET_SCENARIO,
} asset_loader;

typedef struct
{
    const char *kind;
    const char *folder;
    const char *extension; /* of its files: KDL, code, or UI markup */
    asset_loader loader;
    reference_asset_kind reference; /* KIND_COUNT for assets nothing references */
} asset_family;

static const asset_family ASSET_FAMILIES[] = {
    { "behavior", "behaviors", CODE_ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_BEHAVIOR },
    { "event", "events", DECLARATIVE_ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_EVENT },
    { "library", "libraries", CODE_ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_LIBRARY },
    { "material", "materials", DECLARATIVE_ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_MATERIAL },
    { "shape", "shapes", DECLARATIVE_ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_SHAPE },
    { "model", "models", DECLARATIVE_ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_SHAPE },
    { "field", "fields", DECLARATIVE_ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_FIELD },
    { "shader-library", "shader-libraries", DECLARATIVE_ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_LIBRARY },
    { "system", "systems", CODE_ASSET_EXTENSION, ASSET_SYSTEM, REFERENCE_ASSET_SYSTEM },
    { "prototype", "prototypes", CODE_ASSET_EXTENSION, ASSET_PROTOTYPE, REFERENCE_ASSET_PROTOTYPE },
    { "relationship", "relationships", CODE_ASSET_EXTENSION, ASSET_RELATIONSHIP, REFERENCE_ASSET_RELATIONSHIP },
    { "scene", "scenes", DECLARATIVE_ASSET_EXTENSION, ASSET_SCENE, REFERENCE_ASSET_SCENE },
    { "editor", "editor", DECLARATIVE_ASSET_EXTENSION, ASSET_EDITOR, REFERENCE_ASSET_KIND_COUNT },
    { "ui", "ui", UI_MARKUP_EXTENSION, ASSET_UI, REFERENCE_ASSET_UI },
    { "scenario", "scenarios", DECLARATIVE_ASSET_EXTENSION, ASSET_SCENARIO, REFERENCE_ASSET_KIND_COUNT },
};

static bool source_program_family(const asset_family *family)
{
    return (family->loader == ASSET_GRAPH || family->loader == ASSET_SYSTEM) &&
           !strcmp(family->extension, CODE_ASSET_EXTENSION);
}
static bool code_filename(const char *path)
{
    size_t size = strlen(path);
    return size > 6 && !strcmp(path + size - 6, CODE_ASSET_EXTENSION);
}
static ecs_relationship_desc **PROJECT_RELATIONSHIPS;

typedef struct
{
    const asset_family *family;
    char filename[256];
    char relative[512];
    char name[PROTOTYPE_NAME_MAX + 1]; /* once read; see asset_name */
} asset_ref;

typedef struct
{
    const asset_family *family;
    char *filename;
    char *relative;
    char *name; /* see asset_name */
    size_t bytes;
    uint64_t hash;
} asset_entry;

typedef struct
{
    char *kind;
    char *name;
    bool resolved;
} catalog_reference;

typedef struct
{
    char *kind;
    char *name;
} catalog_member;

typedef struct
{
    char *severity;
    int rule;
    char *rule_name;
    char *message;
    int line, column; /* 0 when it names no place */
} catalog_diagnostic;

typedef struct
{
    asset_entry asset;
    char *name;
    int schema_version;
    char *description;
    bool valid;
    int errors;
    int warnings;
    catalog_member *members;
    catalog_reference *dependencies; /* derived from references */
    catalog_reference *dependents;
    catalog_diagnostic *diagnostics;
    int reference; /* its position in the snapshot's references, or -1 */
} catalog_asset;

typedef struct
{
    char *root;
    char *content; /* the folder the assets were read from */
    asset_entry *entries;
    catalog_asset *assets;
    reference_index *references;
    uint64_t revision;
    uint64_t content_hash;
    uint64_t components; /* the component schemas the assets were checked against */
} catalog_snapshot;

static catalog_snapshot CATALOG_SNAPSHOT;

typedef struct
{
    yyjson_val *value;
    uint64_t id;
} inspect_json_item;

typedef struct
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    bool ok;
} response;

typedef struct
{
    const char *name;
    const char *type;
    bool required;
    const char *description;
} contract_field;

typedef struct
{
    const char *name;
    const char *summary;
    const contract_field *fields;
    int field_count;
    int response_version;
    bool read_only;
    bool idempotent;
    bool destructive;
} operation_descriptor;

typedef struct
{
    uint64_t cursor;
    uint64_t limit;
    bool has_limit;
    bool summary;
} page_options;

#define SCENARIO_DEFAULT_FRAMES 1
#define SCENARIO_MAX_FRAMES 1800
#define SCENARIO_RUN_MAX_FRAMES 108000
#define SCENARIO_MAX_ACTIONS 32
#define SCENARIO_DEFAULT_FIXED_DT (1.0 / 60.0)
#define SCENARIO_RECORDING_VERSION 2
#define SCENARIO_RECORDING_FORMAT "chibi.scenario-recording"
#define SCENARIO_RECORDING_MAX_BYTES (1024 * 1024)
#define SCENARIO_RECORDING_PATH_PREFIX "/tmp/chibi-recording-"

#define CONTRACT_FIELD_COUNT(fields) ((int)(sizeof(fields) / sizeof *(fields)))

static const contract_field CONTRACT_NO_FIELDS[1] = { { NULL, NULL, false, NULL } };
static const contract_field CONTRACT_PROJECT_ROOT[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
};
static const contract_field CONTRACT_LIST_ASSETS[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "cursor", "uint64", false, "Zero-based result offset; defaults to zero." },
    { "limit", "uint64", false, "Maximum number of results; must be greater than zero." },
};
static const contract_field CONTRACT_ASSET_REFERENCE[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "path", "string", false, "Content-relative asset path, alternatively use kind and name." },
    { "kind", "string", false, "Asset family, required with name when path is omitted." },
    { "name", "string", false, "Asset name, required with kind when path is omitted." },
};
static const contract_field CONTRACT_NEW_PROJECT[] = {
    { "path", "string", true, "New project directory; it must not exist and its parent must." },
    { "name", "string", true, "Project name written to project.kdl." },
};
static const contract_field CONTRACT_NEW_ASSET[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "kind", "string", true, "scene, prototype, relationship, behavior, event, system, library, material, shape, field, shader-library or model." },
    { "name", "string", true, "Asset name: a letter, then letters, digits, - and _. Source declarations require a non-reserved identifier without -." },
};
static const contract_field CONTRACT_BAKE_MODEL[] = {
    { "project_root", "string", false, "Saved project directory; defaults to the controller root." },
    { "model", "string", true, "Saved declarative model name." },
    { "destination", "string", true, "New bundle directory; parent must exist. Existing output is retained and rejected." },
    { "options", "object", false, "Bounded cell_size, max_axis_cells, max_triangles, max_samples, max_work and edge_iterations." },
    { "overrides", "array", false, "Checked parameter snapshot keyed by asset, scope and parameter_id, with value components." },
};
static const contract_field CONTRACT_IMPORT_SKY[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "source", "string", true, "Radiance .hdr file to copy into the project, such as a download." },
    { "destination", "string", false, "New content/... .hdr path; defaults to content/skies/ and the source's file name. An existing file is kept and rejected." },
};
static const contract_field CONTRACT_IMPORT_MODEL[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "source", "string", true, "glTF 2.0 model to copy into the project: a .glb, or a .gltf beside its buffers and images." },
    { "name", "string", false, "Name of the prototype, mesh and material prefix; defaults to the source's file name without its extension." },
};
static const contract_field CONTRACT_ANALYZE_CODE[] = {
    { "project_root", "string", false, "Optional project directory supplying library/event signatures; defaults to the controller root." },
    { "rename", "string", false, "Propose a source-local rename at offset, without publishing." },
    { "source", "string", true, "Unsaved behavior source; uses the shared native frontend." },
    { "offset", "uint64", false, "Byte offset for shared completion and hover." },
    { "location", "object", false, "graph_id, node_id and optional pin_id to resolve to a source range." },
};
static const contract_field CONTRACT_READ_ASSET[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "path", "string", false, "Content-relative asset path, alternatively use kind and name." },
    { "kind", "string", false, "Asset family, required with name when path is omitted." },
    { "name", "string", false, "Asset name, required with kind when path is omitted." },
    { "complete", "object", false, "A 1-based source line and column; adds code completions, hover, and diagnostics, including for incomplete source." },
};
/* Workstream Q: inspect_ui completes at a position too. */
static const contract_field CONTRACT_INSPECT_UI[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "path", "string", false, "Content-relative asset path, alternatively use kind and name." },
    { "kind", "string", false, "ui." },
    { "name", "string", false, "UI asset name." },
    { "complete", "object", false, "A 1-based file line and column; adds the completions Studio's Text Editor offers there." },
};
static const contract_field CONTRACT_SEARCH_ASSETS[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "query", "string", false, "Case-insensitive text matched against asset metadata." },
    { "kinds", "string[]", false, "Asset families to include in the search." },
    { "cursor", "uint64", false, "Zero-based result offset; defaults to zero." },
    { "limit", "uint64", false, "Maximum number of results; must be greater than zero." },
    { "summary", "boolean", false, "Return identity and validation fields without relationship details." },
};
static const contract_field CONTRACT_DESCRIBE_REGISTRY[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "sections", "string[]", false, "nodes, types, services, or components; defaults to all but nodes." },
};
static const contract_field CONTRACT_CATALOG[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "cursor", "uint64", false, "Zero-based result offset; defaults to zero." },
    { "limit", "uint64", false, "Maximum number of results; must be greater than zero." },
    { "summary", "boolean", false, "Return identity and validation fields without relationship details." },
};
static const contract_field CONTRACT_ASSET_DEPENDENCIES[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "kind", "string", true, "Known asset family." },
    { "name", "string", true, "Asset name." },
    { "direction", "string", false, "Traversal direction; defaults to outgoing." },
    { "transitive", "boolean", false, "Traverse beyond direct dependencies; defaults to false." },
};
static const contract_field CONTRACT_INSPECT_GRAPH[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "path", "string", false, "Content-relative graph asset path, alternatively use kind and name." },
    { "kind", "string", false, "Graph asset family, required with name when path is omitted." },
    { "name", "string", false, "Graph asset name, required with kind when path is omitted." },
    { "graph_id", "uint64", false, "Select one graph by its stable authored ID." },
    { "graph_name", "string", false, "Select one graph by its authored name." },
    { "include_layout", "boolean", false, "Include editor coordinates and routing; defaults to false." },
    { "include_pins", "boolean", false, "List each node's pins as \"name: type\" with resolved wildcard types; defaults to true." },
    { "include_uses", "boolean", false, "Count project-wide uses by role of the asset, its functions and macros, and a behavior's variables; defaults to false." },
};
static const contract_field CONTRACT_INSPECT_SYSTEM[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "path", "string", false, "Content-relative system asset path, alternatively use kind and name." },
    { "kind", "string", false, "System asset family, required with name when path is omitted." },
    { "name", "string", false, "System asset name, required with kind when path is omitted." },
};
static const contract_field CONTRACT_RUN_SCENARIO[] = {
    { "kind", "string", false, "Target family; defaults to prototype." },
    { "name", "string", true, "Prototype or scene asset name." },
    { "frames", "uint64", false, "Number of deterministic fixed steps; maximum 108000." },
    { "fixed_dt", "number", false, "Fixed timestep in seconds; currently limited to 1/60." },
    { "inputs", "array", false, "Per-frame movement, aim, controls, controllers, actions, UI interactions and typed entity-targeted events. Controllers is an array of up to four {connected,buttons} slots; buttons are PadControl labels or integers." },
    { "input_timeline", "array", false, "At most 1800 sparse {frame,input} samples in increasing zero-based frame order; each frame is below frames; alternative to inputs." },
    { "saved_state", "object", false, "Isolated initial committed text values; never accesses player save files." },
    { "save_path", "string", false, "Absolute host save namespace; explicitly enables authored durable saves. Omit saved_state." },
};
static const contract_field CONTRACT_RUNTIME_SNAPSHOT[] = {
    { "kind", "string", false, "Target family; defaults to prototype." },
    { "name", "string", true, "Prototype or scene asset name." },
    { "frames", "uint64", false, "Number of deterministic fixed steps; maximum 108000." },
    { "fixed_dt", "number", false, "Fixed timestep in seconds; currently limited to 1/60." },
    { "entity_limit", "uint64", false, "Maximum number of ordered entities; defaults to 32, maximum 256." },
    { "component_limit", "uint64", false, "Maximum number of components per entity; maximum 64." },
    { "field_limit", "uint64", false, "Maximum number of reflected fields per component; maximum 128." },
    { "behavior_limit", "uint64", false, "Maximum number of behavior attachments per entity; maximum 64." },
    { "fields", "string[]", false, "Optional reflected field selectors such as Transform.x." },
    { "inputs", "array", false, "Per-frame movement, aim, controls, controllers, actions, UI interactions and typed entity-targeted events. Controllers is an array of up to four {connected,buttons} slots; buttons are PadControl labels or integers." },
    { "input_timeline", "array", false, "At most 1800 sparse {frame,input} samples in increasing frame order; alternative to inputs." },
    { "saved_state", "object", false, "Isolated initial committed text values; never accesses player save files." },
    { "save_path", "string", false, "Absolute host save namespace; explicitly enables authored durable saves. Omit saved_state." },
};
static const contract_field CONTRACT_TERRAIN_STORAGE[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "name", "string", true, "Scene asset name." },
    { "entity", "string", true, "Authored terrain entity path within the scene." },
    { "identity", "string", true, "Stable terrain identity and attachment key; 1-64 bytes." },
    { "save_path", "string", true, "Absolute host save namespace; binary terrain attachments commit independently of text saves." },
    { "frames", "uint64", false, "Fixed steps before storage; defaults to zero, maximum 120." },
    { "fixed_dt", "number", false, "Engine fixed timestep; must equal 1/60." },
    { "inputs", "array", false, "Bounded per-frame scenario inputs before storage; alternative to input_timeline." },
    { "input_timeline", "array", false, "Sparse scenario inputs before storage; alternative to inputs." },
};
static const contract_field CONTRACT_RECORD_SCENARIO[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "kind", "string", false, "Target family; defaults to prototype." },
    { "name", "string", true, "Prototype asset name." },
    { "frames", "uint64", false, "Number of deterministic fixed steps; maximum 1800." },
    { "fixed_dt", "number", false, "Fixed timestep in seconds; currently limited to 1/60." },
    { "inputs", "object[]", false, "Per-frame movement, aim, action, and event input records." },
    { "input_timeline", "array", false, "At most 1800 sparse {frame,input} samples in increasing frame order; alternative to inputs." },
    { "saved_state", "object", false, "Isolated initial committed text values." },
};
static const contract_field CONTRACT_REPLAY_SCENARIO[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "recording", "object", false, "Inline versioned scenario recording." },
    { "recording_path", "string", false, "Path returned by record_scenario for a generated recording artifact." },
};
static const contract_field CONTRACT_READ_RECORDING[] = {
    { "id", "string", true, "Generated recording identifier." },
};

static const contract_field CONTRACT_SEARCH_NODES[] = {
    { "query", "string", false, "Words that must all start a word in a node's or event's name, keywords, pin names or expression function; omit it and the filters to list the domain's node types and events." },
    { "domain", "string", false, "Filter to behavior, shader, field, or system nodes." },
    { "pin_type", "string", false, "Require a matching pin type or struct type name." },
    { "wire_from", "string", false, "Type of an output to wire from (exec, float, Health, array<int>); ranks nodes by the input that takes it." },
    { "wire_to", "string", false, "Type of an input to wire into; ranks nodes by the output that feeds it." },
    { "pin_container", "string", false, "Require a matching pin container: single, array, set, or map." },
    { "pin_kind", "string", false, "Require a matching pin kind: data or exec." },
    { "pure", "boolean", false, "Require pure or impure node descriptors." },
    { "limit", "integer", false, "Page size, default 10, maximum 50." },
    { "offset", "integer", false, "Zero-based page offset." },
    { "include_pins", "boolean", false, "Include typed pins, default true." },
    { "behavior", "string", false, "A behavior asset's name: offer only the UI events of the Ui documents on the prototypes and scene objects that attach it." },
};
static const contract_field CONTRACT_DESCRIBE_COMPONENTS[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "names", "string[]", false, "Optional component names or stable keys; omit to describe every reflected component." },
};
static const contract_field CONTRACT_PROFILE_SCENARIO[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "scenario", "string", false, "Named scenario asset; supplies the target and replay input. Omit kind, name, inputs and input_timeline." },
    { "kind", "string", false, "Target family for a direct target; defaults to prototype." },
    { "name", "string", false, "Prototype or scene asset name; required unless scenario is supplied." },
    { "frames", "uint64", false, "Measured deterministic fixed steps; warmup_frames plus frames may total at most 108000." },
    { "fixed_dt", "number", false, "Fixed timestep in seconds; currently limited to 1/60." },
    { "warmup_frames", "uint64", false, "Frames to run before collecting samples; warmup_frames plus frames may total at most 108000." },
    { "behavior_details", "boolean", false, "Collect per-instance behavior timing; defaults to true. False preserves ordinary prepared behavior execution while retaining frame and physics diagnostics." },
    { "inputs", "array", false, "For a direct target, per-frame movement, aim, controls, actions, UI interactions and typed entity-targeted events across warmup and measured frames; at most 1800 entries." },
    { "input_timeline", "array", false, "For a direct target, at most 1800 sparse {frame,input} samples in increasing zero-based frame order across the combined warmup and measured interval; alternative to inputs." },
};
static const contract_field CONTRACT_EXECUTION_STATUS[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "kind", "string", true, "Behavior or system." },
    { "name", "string", true, "Asset name." },
};
static const contract_field CONTRACT_PREVIEW_FIELD[] = {
    { "project_root", "string", false, "Project directory." },
    { "path", "string", false, "Field asset path, or use kind/name." },
    { "kind", "string", false, "Field asset family." },
    { "name", "string", false, "Field asset name." },
    { "width", "integer", false, "Preview width, default 256, maximum 512." },
    { "height", "integer", false, "Preview height, default 256, maximum 512." },
    { "cell_size", "number", false, "Positive grid spacing, default 2, maximum 1024." },
    { "seed", "uint32", false, "Deterministic noise seed." },
    { "parameters", "object", false, "Typed field parameter overrides." },
};
static const contract_field CONTRACT_BAKE_MATERIAL[] = {
    { "project_root", "string", false, "Saved project directory." },
    { "path", "string", false, "Saved material path, or use kind/name." },
    { "kind", "string", false, "material" },
    { "name", "string", false, "Saved material name." },
    { "width", "integer", false, "Width 1..128, default 32." },
    { "height", "integer", false, "Height 1..128, default 32." },
    { "channel", "string", false, "One channel name; omitted selects all channels." },
    { "uv_min", "array", false, "Two finite UV minimum coordinates, default [0,0]." },
    { "uv_max", "array", false, "Two increasing UV maximum coordinates, default [1,1]." },
    { "origin", "array", false, "Three finite plane-origin coordinates, default [0,0,0]." },
    { "axis_u", "array", false, "Full plane U edge, default [1,0,0]." },
    { "axis_v", "array", false, "Full plane V edge, default [0,1,0]." },
    { "time", "number", false, "Fixed finite sample time, default zero." },
    { "overrides", "array", false, "Material-root parameters keyed by asset, empty scope and parameter_id; unsupported scopes are rejected." },
};
static const contract_field CONTRACT_PREVIEW_SHADER[] = {
    { "project_root", "string", false, "Project directory." },
    { "path", "string", false, "Shader asset path, or use kind/name." },
    { "kind", "string", false, "Material or shape; preview shader libraries through a consumer." },
    { "name", "string", false, "Shader asset name." },
    { "width", "integer", false, "Preview width, default 256, maximum 512." },
    { "height", "integer", false, "Preview height, default 256, maximum 512." },
    { "time", "number", false, "Deterministic preview simulation time." },
    { "influence", "array", false, "Material Surface Influence linear RGB at the preview center, fading out at radius 50; default none." },
    { "parameters", "object", false, "Shader Parameter overrides keyed by authored parameter name; scalar values or exact-width numeric arrays." },
    { "camera", "object", false, "Optional preview framing with center and span two-vectors." },
    { "sample_grid", "object", false, "Optional bounded CPU shader samples over an explicit UV rectangle." },
    { "include_source", "boolean", false, "Include bounded generated GLSL, default false." },
};

static const contract_field CONTRACT_FIND_REFERENCES[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "asset", "object", false, "Asset {kind, name}; with graph or graph_id, its function or macro; with variable, parameter or local, that member." },
    { "graph", "string", false, "Function or macro name in asset: the selected graph, a parameter's function, or where a local is." },
    { "graph_id", "uint64", false, "Function or macro ID in asset, as inspectGraph lists it." },
    { "variable", "string", false, "Variable name in asset." },
    { "parameter", "string", false, "Input or output name of the function graph names." },
    { "local", "string", false, "Named value (local) in asset." },
    { "event", "string", false, "Event name: a host event, an event definition, or a ui or action event." },
    { "component", "string", false, "Component name or key." },
    { "field", "string", false, "Field path of component." },
    { "roles", "string[]", false, "Only uses in these roles: use, call, argument, read, write, send, handle, spawn, attach, bind." },
    { "offset", "integer", false, "Zero-based page offset." },
    { "limit", "integer", false, "Page size, default 50, maximum 200." },
    /* UI language service (workstream B) */
    { "ui_class", "string", false, "A class ui assets and stylesheets name, instead of an asset." },
    { "token", "string", false, "A custom property, with its --, instead of an asset." },
    { "save_key", "string", false, "A save key ui bindings read as save.<key>, instead of an asset." },
};
static const contract_field CONTRACT_CALL_HIERARCHY[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "asset", "object", false, "Asset {kind, name} holding the function or macro." },
    { "graph", "string", false, "Function or macro name in asset." },
    { "graph_id", "uint64", false, "Function or macro ID in asset." },
    { "event", "string", false, "Event name, instead of a function." },
    { "direction", "string", false, "incoming (callers), outgoing (callees) or both; defaults to both." },
    { "depth", "integer", false, "Levels below the root, 1 to 6; defaults to 2." },
};

static const operation_descriptor CONTRACT_OPERATIONS[] = {
    { "search_nodes",
      "Find a ranked page of nodes, configured component nodes, and their pins "
      "for authoring.",
      CONTRACT_SEARCH_NODES,
      CONTRACT_FIELD_COUNT(CONTRACT_SEARCH_NODES),
      1,
      true,
      true,
      false },
    { "describe_components",
      "Describe reflected component fields, stable keys, and native defaults "
      "for prototype authoring.",
      CONTRACT_DESCRIBE_COMPONENTS,
      CONTRACT_FIELD_COUNT(CONTRACT_DESCRIBE_COMPONENTS),
      1,
      true,
      true,
      false },
    { "execution_status",
      "Report the actual execution backend available for a behavior or system "
      "asset.",
      CONTRACT_EXECUTION_STATUS,
      CONTRACT_FIELD_COUNT(CONTRACT_EXECUTION_STATUS),
      1,
      true,
      true,
      false },
    { "profile_scenario",
      "Run a bounded deterministic target and return behavior "
      "inclusive/exclusive and engine timing samples.",
      CONTRACT_PROFILE_SCENARIO,
      CONTRACT_FIELD_COUNT(CONTRACT_PROFILE_SCENARIO),
      1,
      true,
      true,
      false },
    { "preview_field",
      "Bake a pure scalar field and return a PNG preview.",
      CONTRACT_PREVIEW_FIELD,
      CONTRACT_FIELD_COUNT(CONTRACT_PREVIEW_FIELD),
      1,
      true,
      true,
      false },
    { "bake_material",
      "Bake bounded raw linear material channels on the CPU without GPU "
      "initialization or file writes.",
      CONTRACT_BAKE_MATERIAL,
      CONTRACT_FIELD_COUNT(CONTRACT_BAKE_MATERIAL),
      1,
      false,
      false,
      false },
    { "preview_shader",
      "Compile a material or shape on the GPU and return a bounded PNG "
      "preview.",
      CONTRACT_PREVIEW_SHADER,
      CONTRACT_FIELD_COUNT(CONTRACT_PREVIEW_SHADER),
      1,
      true,
      true,
      false },
    {
        "describe_contract",
        "Describe the native automation contract and reflected capabilities.",
        CONTRACT_NO_FIELDS,
        0,
        1,
        true,
        true,
        false,
    },
    {
        "status",
        "Return project, asset, reflection, and operation summary information.",
        CONTRACT_PROJECT_ROOT,
        CONTRACT_FIELD_COUNT(CONTRACT_PROJECT_ROOT),
        1,
        true,
        true,
        false,
    },
    {
        "list_assets",
        "List authored assets and their stable content identities.",
        CONTRACT_LIST_ASSETS,
        CONTRACT_FIELD_COUNT(CONTRACT_LIST_ASSETS),
        1,
        true,
        true,
        false,
    },
    {
        "analyze_code",
        "Compile unsaved behavior source, return canonical code, its graph and "
        "source diagnostics.",
        CONTRACT_ANALYZE_CODE,
        CONTRACT_FIELD_COUNT(CONTRACT_ANALYZE_CODE),
        1,
        true,
        true,
        false,
    },
    {
        "read_asset",
        "Read authored source and normalized asset data, with optional code "
        "language assistance.",
        CONTRACT_READ_ASSET,
        CONTRACT_FIELD_COUNT(CONTRACT_READ_ASSET),
        1,
        true,
        true,
        false,
    },
    {
        "validate_asset",
        "Load and validate one authored asset.",
        CONTRACT_ASSET_REFERENCE,
        CONTRACT_FIELD_COUNT(CONTRACT_ASSET_REFERENCE),
        1,
        true,
        true,
        false,
    },
    {
        "validate_project",
        "Validate the manifest, start scene and every asset, listing only "
        "assets with problems.",
        CONTRACT_PROJECT_ROOT,
        CONTRACT_FIELD_COUNT(CONTRACT_PROJECT_ROOT),
        1,
        true,
        true,
        false,
    },
    {
        "new_project",
        "Create a project directory with project.kdl and an empty start "
        "scene.",
        CONTRACT_NEW_PROJECT,
        CONTRACT_FIELD_COUNT(CONTRACT_NEW_PROJECT),
        1,
        false,
        false,
        false,
    },
    {
        "new_asset",
        "Write an empty asset of a kind in its content folder, the form "
        "Studio's New menu starts from.",
        CONTRACT_NEW_ASSET,
        CONTRACT_FIELD_COUNT(CONTRACT_NEW_ASSET),
        1,
        false,
        false,
        false,
    },
    {
        "bake_model",
        "Extract a bounded mesh from saved declarative source and atomically "
        "publish an engine mesh, OBJ, MTL and provenance bundle.",
        CONTRACT_BAKE_MODEL,
        CONTRACT_FIELD_COUNT(CONTRACT_BAKE_MODEL),
        1,
        false,
        false,
        false,
    },
    {
        "import_sky",
        "Check that a Radiance .hdr file is an equirectangular sky and copy it "
        "into the project's content for Environment's sky.",
        CONTRACT_IMPORT_SKY,
        CONTRACT_FIELD_COUNT(CONTRACT_IMPORT_SKY),
        1,
        false,
        false,
        false,
    },
    {
        "import_model",
        "Copy a glTF model into the project's content as one mesh, Standard "
        "materials, their images and a prototype that draws it.",
        CONTRACT_IMPORT_MODEL,
        CONTRACT_FIELD_COUNT(CONTRACT_IMPORT_MODEL),
        1,
        false,
        false,
        false,
    },
    {
        "describe_registry",
        "Return reflected graph, type, service, operator, and component "
        "metadata.",
        CONTRACT_DESCRIBE_REGISTRY,
        CONTRACT_FIELD_COUNT(CONTRACT_DESCRIBE_REGISTRY),
        1,
        true,
        true,
        false,
    },
    {
        "catalog",
        "Return the semantic asset catalog and reflected registry summary.",
        CONTRACT_CATALOG,
        CONTRACT_FIELD_COUNT(CONTRACT_CATALOG),
        1,
        true,
        true,
        false,
    },
    {
        "search_assets",
        "Search the semantic asset catalog by metadata and family.",
        CONTRACT_SEARCH_ASSETS,
        CONTRACT_FIELD_COUNT(CONTRACT_SEARCH_ASSETS),
        1,
        true,
        true,
        false,
    },
    {
        "asset_dependencies",
        "Inspect direct or transitive asset dependency relationships.",
        CONTRACT_ASSET_DEPENDENCIES,
        CONTRACT_FIELD_COUNT(CONTRACT_ASSET_DEPENDENCIES),
        1,
        true,
        true,
        false,
    },
    {
        "find_references",
        "Find a symbol's definition and its uses across the project, by role.",
        CONTRACT_FIND_REFERENCES,
        CONTRACT_FIELD_COUNT(CONTRACT_FIND_REFERENCES),
        1,
        true,
        true,
        false,
    },
    {
        "call_hierarchy",
        "Trace a function's or event's callers and callees as a tree.",
        CONTRACT_CALL_HIERARCHY,
        CONTRACT_FIELD_COUNT(CONTRACT_CALL_HIERARCHY),
        1,
        true,
        true,
        false,
    },
    {
        "inspect_graph",
        "Inspect normalized graph nodes, pins, edges, calls, effects, and "
        "diagnostics.",
        CONTRACT_INSPECT_GRAPH,
        CONTRACT_FIELD_COUNT(CONTRACT_INSPECT_GRAPH),
        1,
        true,
        true,
        false,
    },
    {
        "inspect_system",
        "Inspect a system asset's canonical query, graph summary, and "
        "diagnostics.",
        CONTRACT_INSPECT_SYSTEM,
        CONTRACT_FIELD_COUNT(CONTRACT_INSPECT_SYSTEM),
        1,
        true,
        true,
        false,
    },
    {
        "run_scenario",
        "Launch one prototype or scene and return deterministic runtime "
        "evidence.",
        CONTRACT_RUN_SCENARIO,
        CONTRACT_FIELD_COUNT(CONTRACT_RUN_SCENARIO),
        1,
        false,
        false,
        false,
    },
    {
        "save_terrain",
        "Save one authored terrain through the world's storage module and "
        "return completion and canonical snapshot evidence.",
        CONTRACT_TERRAIN_STORAGE,
        CONTRACT_FIELD_COUNT(CONTRACT_TERRAIN_STORAGE),
        1,
        false,
        false,
        false,
    },
    {
        "load_terrain",
        "Restore one authored terrain through the world's storage module and "
        "synchronize physics before returning evidence.",
        CONTRACT_TERRAIN_STORAGE,
        CONTRACT_FIELD_COUNT(CONTRACT_TERRAIN_STORAGE),
        1,
        false,
        false,
        false,
    },
    {
        "runtime_snapshot",
        "Launch one prototype or scene and return a bounded reflected runtime "
        "snapshot.",
        CONTRACT_RUNTIME_SNAPSHOT,
        CONTRACT_FIELD_COUNT(CONTRACT_RUNTIME_SNAPSHOT),
        1,
        false,
        false,
        false,
    },
    {
        "record_scenario",
        "Record bounded deterministic scene or prototype inputs and per-frame "
        "evidence.",
        CONTRACT_RECORD_SCENARIO,
        CONTRACT_FIELD_COUNT(CONTRACT_RECORD_SCENARIO),
        1,
        true,
        true,
        false,
    },
    {
        "replay_scenario",
        "Replay a versioned scenario recording and report the first "
        "divergence.",
        CONTRACT_REPLAY_SCENARIO,
        CONTRACT_FIELD_COUNT(CONTRACT_REPLAY_SCENARIO),
        1,
        true,
        true,
        false,
    },
    {
        "read_recording",
        "Read one bounded generated recording artifact by identifier.",
        CONTRACT_READ_RECORDING,
        CONTRACT_FIELD_COUNT(CONTRACT_READ_RECORDING),
        1,
        true,
        true,
        false,
    },
    {
        "inspect_ui",
        "Inspect a ui asset's outline, props, events, slots, widgets, tokens "
        "and syntax errors, and complete at a position.",
        CONTRACT_INSPECT_UI,
        CONTRACT_FIELD_COUNT(CONTRACT_INSPECT_UI),
        1,
        true,
        true,
        false,
    },
};

static const char *const CONTRACT_ERROR_CATEGORIES[] = {
    "invalid_request", "io_error",         "validation_failed",
    "asset_not_found", "execution_failed", "asset_conflict",
};

static int handle_describe_registry(yyjson_val *request, response *result, int *exit_code);
static int handle_describe_contract(response *result);
static int handle_describe_components(yyjson_val *request, response *result, int *exit_code);
static int handle_execution_status(const char *root, yyjson_val *request, response *result, int *exit_code);
static int handle_profile_scenario(const char *root, yyjson_val *request, response *result, int *exit_code);
static int handle_scene_runtime_snapshot(
    const char *root,
    yyjson_val *request,
    response *result,
    int *exit_code
);
static int handle_run_scenario(const char *root, yyjson_val *request, response *result, int *exit_code);
static int handle_runtime_snapshot(const char *root, yyjson_val *request, response *result, int *exit_code);
static int handle_record_scenario(const char *root, yyjson_val *request, response *result, int *exit_code);
static int handle_replay_scenario(const char *root, yyjson_val *request, response *result, int *exit_code);
static int handle_read_recording(yyjson_val *request, response *result, int *exit_code);
static uint64_t catalog_content_hash(const catalog_asset *assets);
static uint64_t components_hash();
static int load_asset(
    const char *root,
    yyjson_val *request,
    asset_ref *ref,
    char **json,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
);

static prototype_registry project_registry()
{
    ecs_component_catalog catalog = project_components();
    return (
        prototype_registry
    ){ .components = catalog.components,
       .component_count = catalog.component_count,
       .relationships = (const ecs_relationship_desc *const *)PROJECT_RELATIONSHIPS,
       .relationship_count = acount(PROJECT_RELATIONSHIPS),
       .decode = authoring_prototype_document_read };
}

static const ecs_component_desc *automation_query_component(const char *name, void *)
{
    if (!name) {
        return NULL;
    }
    ecs_component_catalog catalog = project_components();
    for (int i = 0; i < catalog.component_count; i++) {
        const ecs_component_desc *component = catalog.components[i];
        if ((component->name && !strcmp(component->name, name)) ||
            (component->key && !strcmp(component->key, name))) {
            return component;
        }
    }
    return NULL;
}

static const asset_family *family_by_kind(const char *kind)
{
    if (!kind) {
        return NULL;
    }
    for (int i = 0; i < (int)(sizeof ASSET_FAMILIES / sizeof *ASSET_FAMILIES); i++) {
        if (!strcmp(kind, ASSET_FAMILIES[i].kind) ||
            !strcmp(kind, ASSET_FAMILIES[i].folder)) {
            return &ASSET_FAMILIES[i];
        }
    }
    return NULL;
}

static bool valid_asset_filename(const asset_family *family, const char *filename)
{
    if (!filename || !filename[0] || filename[0] == '.' || strchr(filename, '/') ||
        strchr(filename, '\\') || strstr(filename, "..")) {
        return false;
    }
    size_t length = strlen(filename), extension = strlen(family->extension);
    return (length > extension &&
            !strcmp(filename + length - extension, family->extension)) ||
           (family->loader == ASSET_PROTOTYPE &&
            ((length > 4 && !strcmp(filename + length - 4, ".kdl")) ||
             code_filename(filename)));
}

/* Sets `ref`'s file name to `name`, with its family's extension unless it
 * ends with it; NULL, or why the name is refused. */
static const char *name_asset_file(asset_ref *ref, const char *name)
{
    const char *extension = ref->family->extension;
    size_t length = strlen(name), suffix = strlen(extension);
    if ((length >= suffix && !strcmp(name + length - suffix, extension)) ||
        (ref->family->loader == ASSET_PROTOTYPE &&
         ((length > 4 && !strcmp(name + length - 4, ".kdl")) || code_filename(name)))) {
        if (!valid_asset_filename(ref->family, name) || length >= sizeof ref->filename) {
            return "asset name is invalid";
        }
        snprintf(ref->filename, sizeof ref->filename, "%s", name);
        return NULL;
    }
    if (length + suffix >= sizeof ref->filename) {
        return "asset name is too long";
    }
    snprintf(ref->filename, sizeof ref->filename, "%s%s", name, extension);
    return NULL;
}

static bool valid_relative_asset_path(const char *path)
{
    return path && path[0] && path[0] != '/' && !strstr(path, "..") &&
           !strstr(path, "//") && !strchr(path, '\\');
}

static int set_error(char *error, size_t size, const char *message)
{
    if (error && size) {
        snprintf(error, size, "%s", message ? message : "unknown error");
    }
    return -1;
}

static int set_errorf(char *error, size_t size, const char *format, const char *value)
{
    if (error && size) {
        snprintf(error, size, format, value ? value : "");
    }
    return -1;
}

static const char *request_string(yyjson_val *root, const char *key)
{
    yyjson_val *value = yyjson_obj_get(root, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

static bool request_uint(yyjson_val *root, const char *key, uint64_t *out)
{
    yyjson_val *value = yyjson_obj_get(root, key);
    if (yyjson_is_uint(value)) {
        *out = yyjson_get_uint(value);
        return true;
    }
    if (yyjson_is_sint(value) && yyjson_get_sint(value) >= 0) {
        *out = (uint64_t)yyjson_get_sint(value);
        return true;
    }
    return false;
}

static void response_error(response *result, int *exit_code, int code, const char *message);

static bool request_page(yyjson_val *request, page_options *page, response *result, int *exit_code)
{
    *page = (page_options){ 0 };
    yyjson_val *cursor = yyjson_obj_get(request, "cursor");
    if (cursor && !request_uint(request, "cursor", &page->cursor)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "cursor must be a non-negative integer");
        return false;
    }
    yyjson_val *limit = yyjson_obj_get(request, "limit");
    if (limit) {
        if (!request_uint(request, "limit", &page->limit) || page->limit == 0) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "limit must be a positive integer");
            return false;
        }
        page->has_limit = true;
    }
    yyjson_val *summary = yyjson_obj_get(request, "summary");
    if (summary) {
        if (!yyjson_is_bool(summary)) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "summary must be a boolean");
            return false;
        }
        page->summary = yyjson_get_bool(summary);
    }
    return true;
}

static void add_page_metadata(yyjson_mut_doc *doc, yyjson_mut_val *object, int total, uint64_t cursor, uint64_t count)
{
    uint64_t total_value = (uint64_t)total;
    uint64_t start = cursor < total_value ? cursor : total_value;
    uint64_t page_count = count < total_value - start ? count : total_value - start;
    bool has_more = start + page_count < total_value;
    yyjson_mut_obj_add_uint(doc, object, "total", total_value);
    yyjson_mut_obj_add_uint(doc, object, "count", page_count);
    yyjson_mut_obj_add_bool(doc, object, "has_more", has_more);
    if (has_more) {
        yyjson_mut_obj_add_uint(doc, object, "next_cursor", start + page_count);
    } else {
        yyjson_mut_obj_add_null(doc, object, "next_cursor");
    }
}

static response response_new(const char *operation)
{
    response result = { .doc = yyjson_mut_doc_new(NULL), .ok = true };
    result.root = yyjson_mut_obj(result.doc);
    yyjson_mut_doc_set_root(result.doc, result.root);
    yyjson_mut_obj_add_uint(result.doc, result.root, "contract", CHIBI_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_strcpy(result.doc, result.root, "operation", operation ? operation : "");
    return result;
}

static void response_error(response *result, int *exit_code, int code, const char *message)
{
    result->ok = false;
    yyjson_mut_val *error = yyjson_mut_obj_add_obj(result->doc, result->root, "error");
    yyjson_mut_obj_add_strcpy(
        result->doc,
        error,
        "code",
        code == CHIBI_AUTOMATION_INVALID_REQUEST    ? "invalid_request"
        : code == CHIBI_AUTOMATION_IO_ERROR         ? "io_error"
        : code == CHIBI_AUTOMATION_VALIDATION_ERROR ? "validation_failed"
        : code == CHIBI_AUTOMATION_NOT_FOUND        ? "asset_not_found"
        : code == CHIBI_AUTOMATION_CONFLICT         ? "asset_conflict"
                                                    : "execution_failed"
    );
    yyjson_mut_obj_add_strcpy(result->doc, error, "message", message ? message : "unknown error");
    if (exit_code && *exit_code == CHIBI_AUTOMATION_OK) {
        *exit_code = code;
    }
}

static char *response_finish(response *result)
{
    yyjson_mut_obj_add_bool(result->doc, result->root, "ok", result->ok);
    char *json = yyjson_mut_write(result->doc, 0, NULL);
    yyjson_mut_doc_free(result->doc);
    return json ? json
                : xstrdup(
                      "{\"contract\":1,\"ok\":false,\"error\":{\"code\":"
                      "\"execution_failed\","
                      "\"message\":\"the response could not be serialized\"}}"
                  );
}

static const operation_descriptor *operation_by_name(const char *name)
{
    for (int i = 0;
         i < (int)(sizeof CONTRACT_OPERATIONS / sizeof *CONTRACT_OPERATIONS);
         i++) {
        if (!strcmp(name, CONTRACT_OPERATIONS[i].name)) {
            return &CONTRACT_OPERATIONS[i];
        }
    }
    return NULL;
}

static char *read_file(const char *path, size_t *bytes, uint64_t *hash, char *error, size_t error_size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return set_errorf(error, error_size, "could not open %s", path), NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return set_error(error, error_size, "could not seek asset"), NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return set_error(error, error_size, "could not measure asset"), NULL;
    }
    char *text = xmalloc((size_t)length + 1);
    size_t read = fread(text, 1, (size_t)length, file);
    int close_result = fclose(file);
    if (read != (size_t)length || close_result != 0) {
        free(text);
        return set_error(error, error_size, "could not read asset"), NULL;
    }
    text[read] = '\0';
    if (bytes) {
        *bytes = read;
    }
    if (hash) {
        uint64_t value = UINT64_C(1469598103934665603);
        for (size_t i = 0; i < read; i++) {
            value ^= (unsigned char)text[i];
            value *= UINT64_C(1099511628211);
        }
        *hash = value;
    }
    return text;
}

static char *root_path(const char *requested, const char *default_root, char *error, size_t error_size)
{
    const char *candidate = requested && requested[0]
                              ? requested
                              : (default_root && default_root[0] ? default_root : ".");
    char *resolved = realpath(candidate, NULL);
    if (!resolved) {
        set_errorf(error, error_size, "project root does not exist: %s", candidate);
        return NULL;
    }
    struct stat info;
    if (stat(resolved, &info) != 0 || !S_ISDIR(info.st_mode)) {
        free(resolved);
        set_error(error, error_size, "project root is not a directory");
        return NULL;
    }
    return resolved;
}

static int resolve_asset_ref(yyjson_val *root, asset_ref *out, char *error, size_t error_size)
{
    const char *path = request_string(root, "path");
    if (path) {
        if (!valid_relative_asset_path(path) || strncmp(path, "content/", 8)) {
            return set_error(error, error_size, "asset path must stay below content");
        }
        const char *folder = path + 8;
        const char *slash = strchr(folder, '/');
        if (!slash || strchr(slash + 1, '/')) {
            set_error(error, error_size, "asset not found");
            return -2;
        }
        char folder_name[64];
        size_t folder_length = (size_t)(slash - folder);
        if (folder_length >= sizeof folder_name) {
            return set_error(error, error_size, "asset folder name is too long");
        }
        memcpy(folder_name, folder, folder_length);
        folder_name[folder_length] = '\0';
        out->family = family_by_kind(folder_name);
        if (!out->family) {
            set_error(error, error_size, "asset not found");
            return -2;
        }
        if (!valid_asset_filename(out->family, slash + 1)) {
            return set_error(error, error_size, "unknown or invalid asset path");
        }
        if (strlen(slash + 1) >= sizeof out->filename ||
            strlen(path) >= sizeof out->relative) {
            return set_error(error, error_size, "asset path is too long");
        }
        snprintf(out->filename, sizeof out->filename, "%s", slash + 1);
        snprintf(
            out->relative,
            sizeof out->relative,
            "content/%s/%s",
            out->family->folder,
            out->filename
        );
        return 0;
    }

    const char *kind = request_string(root, "kind");
    const char *name = request_string(root, "name");
    out->family = family_by_kind(kind);
    if (!out->family || !name || !name[0] || strchr(name, '/') ||
        strchr(name, '\\') || strstr(name, "..")) {
        return set_error(error, error_size, "asset requires a known kind and safe name");
    }
    const char *refused = name_asset_file(out, name);
    if (refused) {
        return set_error(error, error_size, refused);
    }
    snprintf(out->relative, sizeof out->relative, "content/%s/%s", out->family->folder, out->filename);
    return 0;
}

/* The project the current request reads: its root and the folder its
 * manifest keeps content in, read once per request. */
static struct
{
    char *root, *content;
} PROJECT;

static int open_authored_project(chibi_project *, const char *, char *, size_t);

/* Reads where the project at `root` keeps its content for the request that
 * follows; false with why in `error` when its manifest is malformed. */
static bool open_content_mode(const char *root, bool metadata, char *error, size_t error_size)
{
    char *content;
    if (metadata) {
        /* Metadata reads the content location without opening/migrating the
         * project. Validation reports other manifest errors in its result. */
        content = chibi_project_content(root, error, error_size);
        if (!content) {
            return false;
        }
    } else {
        chibi_project opened = { 0 };
        native_capabilities *native = authoring_application_catalog_new(
            studio_project_is(root),
            error,
            error_size
        );
        int status = native
                       ? authoring_project_open_content(&opened, root, native, error, error_size)
                       : -1;
        native_capabilities_free(native);
        if (status) {
            return false;
        }
        content = xstrdup(opened.content);
        chibi_project_free(&opened);
    }
    ecs_relationship_desc **relationships = NULL;
    if (!authoring_relationship_declarations_load(content, &relationships, error, error_size)) {
        for (int i = 0; i < acount(relationships); i++) {
            authoring_relationship_source_free(relationships[i]);
        }
        afree(relationships);
        free(content);
        return false;
    }
    for (int i = 0; i < acount(PROJECT_RELATIONSHIPS); i++) {
        authoring_relationship_source_free(PROJECT_RELATIONSHIPS[i]);
    }
    afree(PROJECT_RELATIONSHIPS);
    PROJECT_RELATIONSHIPS = relationships;
    free(PROJECT.root);
    free(PROJECT.content);
    PROJECT.root = xstrdup(root);
    PROJECT.content = content;
    return true;
}
static bool open_content(const char *root, char *error, size_t error_size)
{
    return open_content_mode(root, false, error, error_size);
}

const char *chibi_automation_content(const char *root)
{
    assert(PROJECT.root && !strcmp(PROJECT.root, root));
    (void)root;
    return PROJECT.content;
}

/* The owned file a virtual content/ path names in the project at `root`. */
static char *content_path(const char *root, const char *relative)
{
    assert(!strncmp(relative, "content/", strlen("content/")));
    const char *content = chibi_automation_content(root);
    size_t size = strlen(content) + strlen(relative);
    char *path = xmalloc(size);
    snprintf(path, size, "%s%s", content, relative + strlen("content"));
    return path;
}

/* Asset paths are rooted at a real project directory. Do not follow a
 * symlink at either content level: a local automation caller must not be able
 * to turn a project-relative asset path into an arbitrary filesystem read. */
static int verify_asset_directory(const char *root, const asset_family *family, char *error, size_t error_size)
{
    const char *content = chibi_automation_content(root);
    struct stat info;
    if (lstat(content, &info) != 0) {
        if (errno == ENOENT) {
            return 0;
        }
        return set_errorf(error, error_size, "could not inspect %s", content), -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        return set_errorf(error, error_size, "content is not a directory: %s", content),
             -1;
    }

    size_t directory_size = strlen(content) + strlen(family->folder) + 2;
    char *directory_path = xmalloc(directory_size);
    snprintf(directory_path, directory_size, "%s/%s", content, family->folder);
    if (lstat(directory_path, &info) != 0) {
        int missing = errno == ENOENT;
        free(directory_path);
        if (missing) {
            return 0;
        }
        return set_errorf(error, error_size, "could not inspect %s", family->folder),
             -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        free(directory_path);
        return set_errorf(
                   error,
                   error_size,
                   "asset directory is not a directory: %s",
                   family->folder
               ),
             -1;
    }
    free(directory_path);
    return 1;
}

static int asset_entry_compare(const void *left, const void *right)
{
    const asset_entry *a = left;
    const asset_entry *b = right;
    return strcmp(a->relative, b->relative);
}

static void free_asset_entries(asset_entry *entries)
{
    for (int i = 0; i < acount(entries); i++) {
        free(entries[i].filename);
        free(entries[i].relative);
        free(entries[i].name);
    }
    afree(entries);
}

/* The asset name a file name holds before its extension. */
static char *asset_name_from_filename(const char *filename)
{
    const char *extension = strrchr(filename, '.');
    assert(extension);
    size_t length = (size_t)(extension - filename);
    char *name = xmalloc(length + 1);
    memcpy(name, filename, length);
    name[length] = '\0';
    return name;
}

/* Prototypes and relationships use their declared identities, as the runtime
 * catalog does. Invalid declarations fall back to their diagnostic file name. */
static char *asset_name(const asset_family *family, const char *filename, const char *text)
{
    if (family->loader == ASSET_PROTOTYPE) {
        char error[160];
        av_doc *document = authoring_prototype_document_read(text, NULL, error, sizeof error);
        const char *declared = av_get_str(av_obj_get(av_doc_root(document), "name"));
        char *name = declared && declared[0] ? xstrdup(declared) : NULL;
        av_doc_free(document);
        if (name) {
            return name;
        }
    }
    if (family->loader == ASSET_RELATIONSHIP) {
        char error[160];
        ecs_relationship_desc *desc = authoring_relationship_source_read(text, error, sizeof error);
        char *name = desc ? xstrdup(desc->name) : NULL;
        authoring_relationship_source_free(desc);
        if (name) {
            return name;
        }
    }
    return asset_name_from_filename(filename);
}

static void free_catalog_references(catalog_reference *references)
{
    for (int i = 0; i < acount(references); i++) {
        free(references[i].kind);
        free(references[i].name);
    }
    afree(references);
}

static void free_catalog_assets(catalog_asset *assets)
{
    for (int i = 0; i < acount(assets); i++) {
        catalog_asset *asset = &assets[i];
        free(asset->asset.filename);
        free(asset->asset.relative);
        free(asset->asset.name);
        free(asset->name);
        free(asset->description);
        for (int j = 0; j < acount(asset->members); j++) {
            free(asset->members[j].kind);
            free(asset->members[j].name);
        }
        afree(asset->members);
        free_catalog_references(asset->dependencies);
        free_catalog_references(asset->dependents);
        for (int j = 0; j < acount(asset->diagnostics); j++) {
            free(asset->diagnostics[j].severity);
            free(asset->diagnostics[j].rule_name);
            free(asset->diagnostics[j].message);
        }
        afree(asset->diagnostics);
    }
    afree(assets);
}

static void free_catalog_snapshot(catalog_snapshot *snapshot)
{
    free(snapshot->root);
    free(snapshot->content);
    free_asset_entries(snapshot->entries);
    free_catalog_assets(snapshot->assets);
    reference_index_free(snapshot->references);
    memset(snapshot, 0, sizeof *snapshot);
}

static int catalog_reference_compare(const void *left, const void *right)
{
    const catalog_reference *a = left;
    const catalog_reference *b = right;
    int kind = strcmp(a->kind ? a->kind : "", b->kind ? b->kind : "");
    return kind ? kind : strcmp(a->name, b->name);
}

static int catalog_member_compare(const void *left, const void *right)
{
    const catalog_member *a = left;
    const catalog_member *b = right;
    int kind = strcmp(a->kind, b->kind);
    return kind ? kind : strcmp(a->name, b->name);
}

static void catalog_member_add(catalog_asset *asset, const char *kind, const char *name)
{
    if (!name || !name[0]) {
        return;
    }
    for (int i = 0; i < acount(asset->members); i++) {
        if (!strcmp(asset->members[i].kind, kind) &&
            !strcmp(asset->members[i].name, name)) {
            return;
        }
    }
    catalog_member member = { .kind = xstrdup(kind), .name = xstrdup(name) };
    apush(asset->members, member);
}

/* Scene entities and prototype children by path, their children nested
 * under them. */
static void catalog_scan_entities(catalog_asset *asset, yyjson_val *entities, const char *parent, int depth)
{
    size_t index, count;
    yyjson_val *entity;
    yyjson_arr_foreach(entities, index, count, entity)
    {
        const char *name = yyjson_is_obj(entity) ? request_string(entity, "name")
                                                 : NULL;
        if (!name || !name[0] || depth >= SCENE_DEPTH_MAX) {
            continue;
        }
        size_t size = (parent ? strlen(parent) + 1 : 0) + strlen(name) + 1;
        char *path = xmalloc(size);
        snprintf(path, size, "%s%s%s", parent ? parent : "", parent ? "." : "", name);
        catalog_member_add(asset, "entity", path);
        catalog_scan_entities(asset, yyjson_obj_get(entity, "children"), path, depth + 1);
        free(path);
    }
}

static void catalog_scan_members(catalog_asset *asset, yyjson_val *root)
{
    yyjson_val *graphs = yyjson_obj_get(root, "graphs");
    size_t index, count;
    yyjson_val *graph_value;
    yyjson_arr_foreach(graphs, index, count, graph_value)
    {
        if (!yyjson_is_obj(graph_value)) {
            continue;
        }
        const char *name = request_string(graph_value, "name");
        const char *kind = request_string(graph_value, "kind");
        catalog_member_add(asset, kind ? kind : "graph", name);
    }
    yyjson_val *variables = yyjson_obj_get(root, "variables");
    yyjson_val *variable;
    yyjson_arr_foreach(variables, index, count, variable)
    {
        if (yyjson_is_obj(variable)) {
            catalog_member_add(asset, "variable", request_string(variable, "name"));
        }
    }
    yyjson_val *components = yyjson_obj_get(root, "components");
    yyjson_val *component;
    yyjson_arr_foreach(components, index, count, component)
    {
        if (yyjson_is_obj(component)) {
            const char *name = request_string(component, "type");
            if (!name) {
                name = request_string(component, "name");
            }
            catalog_member_add(asset, "component", name);
        }
    }
    catalog_scan_entities(asset, yyjson_obj_get(root, "entities"), NULL, 0);
    catalog_scan_entities(asset, yyjson_obj_get(root, "children"), NULL, 0);
    if (acount(asset->members) > 1) {
        asort(asset->members, catalog_member_compare);
    }
}

static void catalog_add_diagnostic(
    catalog_asset *asset,
    const char *severity,
    int rule,
    const char *rule_name,
    const char *message
)
{
    catalog_diagnostic diagnostic = {
        .severity = xstrdup(severity ? severity : "error"),
        .rule = rule,
        .rule_name = xstrdup(rule_name ? rule_name : "LOAD"),
        .message = xstrdup(message ? message : "asset rejected"),
    };
    apush(asset->diagnostics, diagnostic);
}
/* Non-fatal source warnings leave the source valid. */
static void catalog_add_warnings(catalog_asset *asset, const script_analysis *analysis)
{
    asset->warnings = analysis->warning_count;
    for (int i = 0; i < analysis->warning_count; i++) {
        const script_diagnostic *warning = &analysis->warnings[i];
        apush(
            asset->diagnostics,
            ((catalog_diagnostic){ .severity = xstrdup("warning"),
                                   .rule_name = xstrdup("UNITS"),
                                   .message = xstrdup(warning->message),
                                   .line = warning->range.line,
                                   .column = warning->range.column })
        );
    }
}

static void free_behavior_assets(behavior_asset **assets);
static int load_behavior_family_assets(
    const char *root,
    const asset_family *family,
    behavior_asset ***out,
    char *error,
    size_t error_size
);

/* Editor assets hold Studio's component presentation. */
static bool read_editor_asset(const char *json, char *error, size_t error_size)
{
    ecs_component_catalog registry = project_components();
    component_presentation presentation = { 0 };
    bool ok = component_presentation_read(json, registry, &presentation, error, error_size);
    component_presentation_free(&presentation);
    return ok;
}

/* Scenarios replay gameplay input on a scene. */
static bool read_scenario_asset(const char *json, char *error, size_t error_size)
{
    scenario_document *document = scenario_document_load(json, error, error_size);
    bool ok = document != NULL;
    scenario_document_free(document);
    return ok;
}

/* The ui folder of the project at `root`. */
static void ui_folder(const char *root, char *out, size_t size)
{
    snprintf(out, size, "%s/ui", chibi_automation_content(root));
}

/* A ui asset's styles beside its markup, owned; NULL when it has none. */
static char *ui_styles(const char *root, const char *name)
{
    char folder[1024];
    ui_folder(root, folder, sizeof folder);
    char *path = ui_document_path(folder, name, true);
    char *text = text_file_read(path);
    free(path);
    return text;
}

/* UI assets hold RML markup, and RCSS beside it, that Ui components
 * show. */
static bool read_ui_asset(const char *root, const char *name, char *error, size_t error_size)
{
    char folder[1024];
    ui_folder(root, folder, sizeof folder);
    ui_document document = { 0 };
    bool ok = ui_document_load(folder, name, NULL, &document, error, error_size);
    ui_document_free(&document);
    return ok;
}

/* Whether the ui folder holds markup for `name`, matched as preparation
 * matches it: a regular file with the exact name, top level only. Like the
 * catalog, which checks each document's markup, it does not follow a link. */
static bool ui_exists(const char *root, const char *name)
{
    char folder[1024], file[512], path[1600];
    ui_folder(root, folder, sizeof folder);
    snprintf(file, sizeof file, "%s" UI_MARKUP_EXTENSION, name);
    DIR *dir = opendir(folder);
    bool found = false;
    for (struct dirent *entry; dir && !found && (entry = readdir(dir));) {
        found = entry->d_name[0] != '.' && !strcmp(entry->d_name, file);
    }
    if (dir) {
        closedir(dir);
    }
    snprintf(path, sizeof path, "%s/%s", folder, file);
    struct stat info;
    return found && !lstat(path, &info) && S_ISREG(info.st_mode);
}

/* UI language service (workstream B): validateAsset for a ui asset whose
 * markup is `rml`: its syntax, widgets, imports and RmlUi's warnings.
 * Returns the error count. */
static int validate_ui(
    const char *root,
    const char *name,
    const char *rml,
    yyjson_mut_doc *doc,
    yyjson_mut_val *diagnostics
)
{
    char *rcss = ui_styles(root, name);
    int errors = automation_ui_validate(chibi_automation_content(root), name, rml, rcss, doc, diagnostics);
    free(rcss);
    return errors;
}

/* What graph assets link with: the libraries, shader libraries and event
   definitions, loaded once per catalog. `error` is why they did not load. */
typedef struct
{
    behavior_asset **assets;
    authoring_language_catalog *native;
    authoring_sources *sources;
    char error[256];
    bool studio; /* the project is Studio's, whose world runs its editor nodes */
} catalog_links;

static bool links_family(const asset_family *family)
{
    return !strcmp(family->kind, "library") ||
           !strcmp(family->kind, "shader-library") ||
           !strcmp(family->kind, "event") || !strcmp(family->kind, "model");
}

static int open_authored_project(chibi_project *project, const char *path, char *error, size_t size)
{
    native_capabilities
        *native = authoring_application_catalog_new(studio_project_is(path), error, size);
    int result = native ? authoring_project_open(project, path, native, error, size)
                        : -1;
    native_capabilities_free(native);
    return result;
}

static authoring_language_catalog *source_native_catalog(const char *root, char *error, size_t size)
{
    native_capabilities
        *table = authoring_application_catalog_new(studio_project_is(root), error, size);
    if (!table) {
        return NULL;
    }
    const authoring_language_catalog_input input = { .native = table };
    authoring_language_catalog *catalog = authoring_language_catalog_new(&input, error, size);
    native_capabilities_free(table);
    return catalog;
}

/* Installs the components the project declares in content/components until
 * *out is freed, as an empty set when it declares none. */
static bool load_declared(const char *root, declared_components **out, char *error, size_t size)
{
    *out = NULL;
    native_capabilities
        *table = authoring_application_catalog_new(studio_project_is(root), error, size);
    if (!table) {
        return false;
    }
    bool loaded = authoring_component_declarations_load(
        chibi_automation_content(root),
        native_capabilities_catalog(table),
        out,
        error,
        size
    );
    native_capabilities_free(table);
    return loaded;
}

static void load_catalog_links(const char *root, const asset_entry *entries, catalog_links *links)
{
    links->native = source_native_catalog(root, links->error, sizeof links->error);
    if (!links->native) {
        return;
    }
    script_lower_bindings bindings = authoring_language_bindings(links->native);
    links->sources = authoring_sources_prepare(
        chibi_automation_content(root),
        authoring_language_catalog_view(links->native),
        &bindings,
        BEHAVIOR_BYTECODE
    );
    for (int i = 0; i < acount(entries) && !links->error[0]; i++) {
        if (!links_family(entries[i].family) ||
            !strcmp(entries[i].family->extension, CODE_ASSET_EXTENSION)) {
            continue;
        }
        char *path = content_path(root, entries[i].relative);
        char *text = read_file(path, NULL, NULL, links->error, sizeof links->error);
        behavior_asset *asset = text ? behavior_asset_load(text) : NULL;
        if (text && !asset) {
            snprintf(
                links->error,
                sizeof links->error,
                "could not load %s: %s",
                entries[i].relative,
                graph_last_error()
            );
        }
        free(path);
        free(text);
        if (asset) {
            apush(links->assets, asset);
        }
    }
    behavior_asset **libraries = authoring_source_graph_libraries(links->sources);
    for (int i = 0; i < acount(libraries); i++) {
        apush(links->assets, libraries[i]);
    }
    afree(libraries);
}

static void catalog_add_report(catalog_asset *asset, const compile_report *report)
{
    asset->errors = report->error_count;
    asset->warnings = report->warning_count;
    for (int i = 0; i < acount(report->items); i++) {
        const compile_diagnostic *diagnostic = &report->items[i];
        catalog_add_diagnostic(
            asset,
            cmpl_severity_name(diagnostic->severity),
            diagnostic->rule,
            cmpl_rule_name(diagnostic->rule),
            diagnostic->message
        );
    }
}

static int load_named_asset(
    const char *root,
    const asset_family *family,
    const char *name,
    asset_ref *ref,
    char **json,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
);

/* Prototype includes name other prototypes, found by the names they declare
 * as scene entities find theirs. Each is read once and kept until the
 * includes are freed. */
typedef struct
{
    const char *root;
    char **names, **texts;
} prototype_includes;

static const char *include_prototype(const char *name, void *user)
{
    prototype_includes *includes = user;
    for (int i = 0; i < acount(includes->names); i++) {
        if (!strcmp(includes->names[i], name)) {
            return includes->texts[i];
        }
    }
    asset_ref ref;
    char *text = NULL, error[256];
    const asset_family *family = family_by_kind("prototype");
    if (load_named_asset(includes->root, family, name, &ref, &text, NULL, NULL, error, sizeof error) !=
        CHIBI_AUTOMATION_OK) {
        free(text);
        return NULL;
    }
    apush(includes->names, xstrdup(name));
    apush(includes->texts, text);
    return text;
}

static prototype_registry registry_with_includes(prototype_includes *includes)
{
    prototype_registry registry = project_registry();
    registry.include = include_prototype;
    registry.include_user = includes;
    return registry;
}

static void prototype_includes_free(prototype_includes *includes)
{
    for (int i = 0; i < acount(includes->texts); i++) {
        free(includes->names[i]);
        free(includes->texts[i]);
    }
    afree(includes->names);
    afree(includes->texts);
}

static void catalog_validate_asset(
    const char *root,
    const catalog_links *links,
    catalog_asset *asset,
    const char *json,
    const char *path
)
{
    const asset_family *family = asset->asset.family;
    if (source_program_family(family)) {
        const authoring_source *source = NULL;
        for (int i = 0; links->sources && i < authoring_sources_count(links->sources);
             i++) {
            const authoring_source *candidate = authoring_sources_at(links->sources, i);
            if (!strcmp(candidate->path, path)) {
                source = candidate;
                break;
            }
        }
        asset->errors = source ? source->diagnostic_count : 1;
        asset->valid = source && source->valid;
        if (!source) {
            catalog_add_diagnostic(
                asset,
                "error",
                0,
                "SOURCE",
                links->error[0] ? links->error : "Source missing from project snapshot"
            );
        }
        for (int i = 0; source && i < source->diagnostic_count; i++) {
            const script_diagnostic *diagnostic = &source->diagnostics[i];
            apush(
                asset->diagnostics,
                ((catalog_diagnostic){ .severity = xstrdup("error"),
                                       .rule_name = xstrdup("SOURCE"),
                                       .message = xstrdup(diagnostic->message),
                                       .line = diagnostic->range.line,
                                       .column = diagnostic->range.column })
            );
        }
        if (source) {
            catalog_add_warnings(asset, source->analysis);
        }
        return;
    }
    if (family->loader == ASSET_GRAPH) {
        behavior_asset *behavior = behavior_asset_load(json);
        if (!behavior || links->error[0]) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", behavior ? links->error : graph_last_error());
        } else {
            compile_report report;
            compile_report_init(&report);
            behavior_asset_free(behavior_asset_validate(
                behavior,
                (const behavior_asset *const *)links->assets,
                acount(links->assets),
                &report
            ));
            if (!links->studio) {
                studio_nodes_check(behavior, &report);
            }
            catalog_add_report(asset, &report);
            compile_report_free(&report);
        }
        behavior_asset_free(behavior);
    } else if (family->loader == ASSET_RELATIONSHIP) {
        char error[256] = { 0 };
        ecs_relationship_desc *relationship = authoring_relationship_source_read(json, error, sizeof error);
        if (!relationship) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        }
        authoring_relationship_source_free(relationship);
    } else if (family->loader == ASSET_PROTOTYPE) {
        prototype_includes includes = { .root = root };
        prototype_registry registry = registry_with_includes(&includes);
        char error[256] = { 0 };
        prototype *prototype = prototype_load(json, &registry, error, sizeof error);
        if (!prototype) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        } else {
            prototype_free(prototype);
        }
        prototype_includes_free(&includes);
    } else if (family->loader == ASSET_EDITOR || family->loader == ASSET_SCENARIO) {
        char error[256] = { 0 };
        bool ok = family->loader == ASSET_EDITOR
                    ? read_editor_asset(json, error, sizeof error)
                    : read_scenario_asset(json, error, sizeof error);
        if (!ok) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        }
    } else {
        char error[256] = { 0 };
        scene_document *scene = scene_document_load(json, error, sizeof error);
        if (!scene) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        } else {
            scene_document_free(scene);
        }
    }
    asset->valid = asset->errors == 0;
}

static int catalog_asset_index(const catalog_asset *assets, int count, const char *kind, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (kind && kind[0] && strcmp(assets[i].asset.family->kind, kind)) {
            continue;
        }
        if (!strcmp(assets[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static const asset_family *family_by_reference(reference_asset_kind kind)
{
    for (int i = 0; i < (int)(sizeof ASSET_FAMILIES / sizeof *ASSET_FAMILIES); i++) {
        if (ASSET_FAMILIES[i].reference == kind) {
            return &ASSET_FAMILIES[i];
        }
    }
    assert(!"every reference kind has a family");
    return NULL;
}

/* Models share the runtime shape namespace, while an unresolved whole-model
 * instance still has a known authored family for diagnostics and navigation. */
static const char *reference_family_kind(const reference_index *index, int asset)
{
    const reference_asset *named = reference_index_asset_at(index, asset);
    if (named->kind == REFERENCE_ASSET_SHAPE) {
        for (int i = 0; i < reference_index_count(index); i++) {
            const reference *r = reference_index_at(index, i);
            if (r->role == REFERENCE_CALL && r->symbol.kind == REFERENCE_SYMBOL_ASSET &&
                r->symbol.asset == asset) {
                return "model";
            }
        }
    }
    return family_by_reference(named->kind)->kind;
}

static void catalog_reference_add(
    catalog_reference **references,
    const char *kind,
    const char *name,
    bool resolved
)
{
    for (int i = 0; i < acount(*references); i++) {
        if (!strcmp((*references)[i].kind, kind) &&
            !strcmp((*references)[i].name, name)) {
            return;
        }
    }
    catalog_reference reference = { .kind = xstrdup(kind),
                                    .name = xstrdup(name),
                                    .resolved = resolved };
    apush(*references, reference);
}

/* An owned string from project.kdl, or NULL for none. */
static char *manifest_string(const char *root, const char *key)
{
    char path[1024], error[128];
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    char *text = text_file_read(path);
    av_doc *manifest = text ? av_doc_read(text, error, sizeof error) : NULL;
    free(text);
    const char *value = av_get_str(av_obj_get(av_doc_root(manifest), key));
    char *copy = value && value[0] ? xstrdup(value) : NULL;
    av_doc_free(manifest);
    return copy;
}

/* Adds what an asset holds to the index, with `page` the project's default
 * page. An asset its loader rejects holds nothing; its diagnostics say why. */
static void catalog_add_content(
    reference_index *index,
    const catalog_asset *asset,
    const char *text,
    const char *page
)
{
    if (source_program_family(asset->asset.family)) {
        return;
    }
    char error[256];
    switch (asset->asset.family->loader) {
        case ASSET_GRAPH: {
            behavior_asset *graph = behavior_asset_load(text);
            if (graph) {
                reference_index_add_graphs(index, asset->reference, graph);
            }
            behavior_asset_free(graph);
            return;
        }
        case ASSET_SYSTEM:
            return; /* Executable declarations were indexed through canonical facts. */
        case ASSET_PROTOTYPE: {
            /* What an included prototype holds is its references, not this
             * one's, which only names it. */
            prototype_registry registry = project_registry();
            prototype *prototype = prototype_load_authored(text, &registry, error, sizeof error);
            if (prototype) {
                reference_index_add_prototype(index, asset->reference, prototype);
                prototype_free(prototype);
            }
            return;
        }
        case ASSET_SCENE: {
            scene_document *scene = scene_document_load(text, error, sizeof error);
            for (int i = 0; scene && i < acount(scene->entities); i++) {
                const scene_entity *entity = &scene->entities[i];
                char *path = scene_document_entity_path(scene, i);
                reference_object object = {
                    .name = path,
                    .prototype = entity->prototype,
                    .overrides = entity->components,
                    .behaviors = (const char *const *)entity->behaviors,
                    .behavior_count = acount(entity->behaviors),
                };
                reference_index_add_object(index, asset->reference, &object);
                free(path);
            }
            if (scene) {
                reference_index_add_page(index, asset->reference, scene->ui, page);
            }
            scene_document_free(scene);
            return;
        }
        case ASSET_UI:
            reference_index_add_ui(index, asset->reference, text);
            return;
        case ASSET_EDITOR:
        case ASSET_RELATIONSHIP:
        case ASSET_SCENARIO: return;
    }
}

/* The catalog asset at each reference index position, or -1 for an asset
 * only named. The caller frees it. */
static int *catalog_positions(const catalog_asset *assets, const reference_index *index)
{
    int *catalog = xmalloc(((size_t)reference_index_asset_count(index) + 1) * sizeof *catalog);
    for (int i = 0; i < reference_index_asset_count(index); i++) {
        catalog[i] = -1;
    }
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].reference >= 0) {
            catalog[assets[i].reference] = i;
        }
    }
    return catalog;
}

/* Each asset's dependencies and dependents: the assets its references use. */
static void catalog_link_references(catalog_asset *assets, const reference_index *index)
{
    int *catalog = catalog_positions(assets, index);
    for (int i = 0; i < reference_index_count(index); i++) {
        const reference *r = reference_index_at(index, i);
        int target = reference_dependency(r);
        if (target < 0) {
            continue;
        }
        catalog_asset *source = &assets[catalog[r->source]];
        const reference_asset *named = reference_index_asset_at(index, target);
        catalog_asset *used = catalog[target] >= 0 ? &assets[catalog[target]] : NULL;
        catalog_reference_add(
            &source->dependencies,
            used ? used->asset.family->kind : reference_family_kind(index, target),
            named->name,
            used != NULL
        );
        if (used) {
            catalog_reference_add(
                &used->dependents,
                source->asset.family->kind,
                source->name,
                true
            );
        }
    }
    free(catalog);
    for (int i = 0; i < acount(assets); i++) {
        catalog_reference *dependencies = assets[i].dependencies;
        catalog_reference *dependents = assets[i].dependents;
        if (acount(dependencies) > 1) {
            asort(dependencies, catalog_reference_compare);
        }
        if (acount(dependents) > 1) {
            asort(dependents, catalog_reference_compare);
        }
    }
}

/* Every asset first, then what each holds, so references find the event
 * definitions they name. */
static int catalog_index_references(
    const char *root,
    catalog_asset *assets,
    const authoring_sources *sources,
    reference_index **out,
    char *error,
    size_t error_size
)
{
    reference_index *index = reference_index_new();
    for (int i = 0; i < acount(assets); i++) {
        reference_asset_kind kind = assets[i].asset.family->reference;
        char *path = content_path(root, assets[i].asset.relative);
        assets[i]
            .reference = kind == REFERENCE_ASSET_KIND_COUNT
                           ? -1
                           : reference_index_add_asset(index, kind, assets[i].name, path, NULL);
        free(path);
    }
    char *page = manifest_string(root, "ui");
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].reference < 0) {
            continue;
        }
        char *path = content_path(root, assets[i].asset.relative);
        char *text = read_file(path, NULL, NULL, error, error_size);
        free(path);
        if (!text) {
            free(page);
            reference_index_free(index);
            return -1;
        }
        catalog_add_content(index, &assets[i], text, page);
        free(text);
    }
    free(page);
    authoring_document *documents = NULL;
    for (int i = 0; sources && i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        apush(
            documents,
            ((authoring_document){ source->path, source->syntax, source->analysis })
        );
    }
    if (sources) {
        authoring_navigation *navigation = authoring_navigation_new_project(
            documents,
            acount(documents),
            authoring_sources_catalog(sources),
            authoring_sources_definitions(sources),
            authoring_sources_definition_count(sources),
            authoring_sources_references(sources),
            authoring_sources_reference_count(sources)
        );
        authoring_source_references_add(index, navigation);
        authoring_navigation_free(navigation);
    }
    afree(documents);
    catalog_link_references(assets, index);
    *out = index;
    return 0;
}

static int enumerate_assets(const char *root, asset_entry **out, char *error, size_t error_size);

/* Inspect KDL documents or executable source through native asset values. */
static yyjson_doc *parse_project_asset_text(const char *, const char *text)
{
    char error[160];
    av_doc *native = av_doc_read(text, error, sizeof error);
    char *json = native ? av_json_write(av_doc_root(native)) : NULL;
    yyjson_doc *document = json ? yyjson_read(json, strlen(json), 0) : NULL;
    free(json);
    av_doc_free(native);
    return document;
}

static yyjson_doc *parse_project_asset_path(const char *root, const char *relative, const char *text)
{
    bool prototype_source = strstr(relative, "prototypes/") && code_filename(relative);
    bool relationship_source = strstr(relative, "relationships/") &&
                               code_filename(relative);
    if (prototype_source || relationship_source) {
        char error[256] = { 0 };
        av_doc *values = NULL;
        if (prototype_source) {
            prototype_registry registry = project_registry();
            values = authoring_prototype_source_read(text, &registry, error, sizeof error);
        } else {
            ecs_relationship_desc *desc = authoring_relationship_source_read(text, error, sizeof error);
            if (desc) {
                values = av_doc_new();
                av_value *value = av_obj(values);
                av_doc_set_root(values, value);
                av_obj_add_str(values, value, "name", desc->name);
                av_obj_add_bool(values, value, "exclusive", !!(desc->traits & ECS_REL_EXCLUSIVE));
                av_obj_add_bool(values, value, "acyclic", !!(desc->traits & ECS_REL_ACYCLIC));
                av_obj_add_bool(values, value, "traversable", !!(desc->traits & ECS_REL_TRAVERSABLE));
                av_obj_add_str(
                    values,
                    value,
                    "on_delete_target",
                    desc->on_delete_target == ECS_CLEANUP_DELETE  ? "Delete"
                    : desc->on_delete_target == ECS_CLEANUP_PANIC ? "Panic"
                                                                  : "Remove"
                );
                authoring_relationship_source_free(desc);
            }
        }
        char *json = values ? av_json_write(av_doc_root(values)) : NULL;
        yyjson_doc *document = json ? yyjson_read(json, strlen(json), 0) : NULL;
        free(json);
        av_doc_free(values);
        return document;
    }
    const char *extension = strrchr(relative, '.');
    if (!extension || strcmp(extension, CODE_ASSET_EXTENSION)) {
        return parse_project_asset_text(root, text);
    }
    yyjson_mut_doc *request = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *value = yyjson_mut_obj(request);
    yyjson_mut_doc_set_root(request, value);
    yyjson_mut_obj_add_str(request, value, "source", text);
    yyjson_mut_obj_add_str(request, value, "path", relative);
    yyjson_doc *input = yyjson_mut_doc_imut_copy(request, NULL);
    yyjson_mut_doc *output = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(output);
    yyjson_mut_doc_set_root(output, result);
    char error[256] = "";
    int status = analyze_project_source(root, yyjson_doc_get_root(input), false, output, result, error, sizeof error);
    yyjson_mut_val *document = yyjson_mut_obj_get(result, "document");
    yyjson_mut_doc *copy = status == CHIBI_AUTOMATION_OK && document
                             ? yyjson_mut_doc_new(NULL)
                             : NULL;
    if (copy) {
        yyjson_mut_doc_set_root(copy, yyjson_mut_val_mut_copy(copy, document));
    }
    yyjson_doc *parsed = copy ? yyjson_mut_doc_imut_copy(copy, NULL) : NULL;
    yyjson_mut_doc_free(copy);
    yyjson_mut_doc_free(output);
    yyjson_doc_free(input);
    yyjson_mut_doc_free(request);
    return parsed;
}

static void source_range_json(yyjson_mut_doc *doc, yyjson_mut_val *value, script_range range)
{
    yyjson_mut_obj_add_int(doc, value, "start", range.start);
    yyjson_mut_obj_add_int(doc, value, "end", range.end);
    yyjson_mut_obj_add_int(doc, value, "line", range.line);
    yyjson_mut_obj_add_int(doc, value, "column", range.column);
}

static void source_location_json(
    const authoring_navigation *navigation,
    yyjson_mut_doc *doc,
    yyjson_mut_val *value,
    authoring_location location
)
{
    const authoring_document
        *document = authoring_navigation_document_at(navigation, location.document);
    yyjson_mut_obj_add_strcpy(doc, value, "path", document->path);
    source_range_json(doc, value, location.range);
}

static void source_rename_proposal(
    const char *content,
    const script_catalog *native,
    const authoring_navigation *navigation,
    int selected,
    int offset,
    const char *name,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result
)
{
    char error[256] = "";
    const authoring_reference *reference = authoring_navigation_at(navigation, selected, offset);
    authoring_rename_plan
        *plan = reference
                  ? authoring_source_rename(navigation, reference->target, name, error, sizeof error)
                  : NULL;
    if (!plan) {
        yyjson_mut_obj_add_strcpy(doc, result, "rename_error", error[0] ? error : "No resolved symbol at offset");
        return;
    }
    source_catalog_edit *edits = NULL;
    int *documents = NULL;
    for (int i = 0; i < plan->count; i++) {
        int document = plan->edits[i].document;
        bool included = false;
        for (int j = 0; j < acount(documents); j++) {
            included |= documents[j] == document;
        }
        if (included) {
            continue;
        }
        const authoring_document *source = authoring_navigation_document_at(navigation, document);
        char *text = authoring_rename_document(
            plan,
            document,
            authoring_navigation_document_text(navigation, document),
            error,
            sizeof error
        );
        if (!text) {
            break;
        }
        apush(edits, ((source_catalog_edit){ .path = source->path, .text = text }));
        apush(documents, document);
    }
    source_catalog *candidate = !error[0]
                                  ? source_catalog_edited(content, native, edits, acount(edits))
                                  : NULL;
    if (candidate && source_catalog_diagnostic_count(candidate)) {
        snprintf(
            error,
            sizeof error,
            "%s",
            source_catalog_diagnostic_at(candidate, 0)->message
        );
    }
    for (int i = 0;
         candidate && !error[0] && i < source_catalog_source_count(candidate);
         i++) {
        const source_catalog_source *source = source_catalog_source_at(candidate, i);
        script_analysis *analysis = script_analyze_document(
            source->syntax,
            source_catalog_view(candidate),
            source->identity
        );
        if (!analysis->valid) {
            snprintf(
                error,
                sizeof error,
                "%s",
                analysis->diagnostic_count ? analysis->diagnostics[0].message : "Rename candidate is invalid"
            );
        }
        script_analysis_free(analysis);
    }
    if (error[0]) {
        yyjson_mut_obj_add_strcpy(doc, result, "rename_error", error);
    } else {
        yyjson_mut_val *proposal = yyjson_mut_obj_add_arr(doc, result, "rename_edits");
        for (int i = 0; i < plan->count; i++) {
            const authoring_rename_edit *edit = &plan->edits[i];
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, proposal);
            yyjson_mut_obj_add_strcpy(doc, item, "path", edit->path);
            source_range_json(doc, item, edit->range);
            yyjson_mut_obj_add_strcpy(doc, item, "expected", edit->expected);
            yyjson_mut_obj_add_strcpy(doc, item, "replacement", edit->replacement);
        }
        for (int i = 0; i < acount(edits); i++) {
            if (documents[i] == selected) {
                yyjson_mut_obj_add_strcpy(
                    doc,
                    result,
                    "renamed_source",
                    edits[i].text
                );
            }
        }
    }
    source_catalog_free(candidate);
    for (int i = 0; i < acount(edits); i++) {
        free((char *)edits[i].text);
    }
    afree(edits);
    afree(documents);
    authoring_rename_plan_free(plan);
}

static int analyze_project_source(
    const char *root,
    yyjson_val *request,
    bool compile,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    char *error,
    size_t size
)
{
    yyjson_val *source_value = yyjson_obj_get(request, "source");
    const char *text = yyjson_get_str(source_value);
    yyjson_val *position = yyjson_obj_get(request, "offset");
    if (!text || strlen(text) != yyjson_get_len(source_value) ||
        strlen(text) > 512 * 1024) {
        set_error(error, size, "source must be a NUL-free string of at most 512 KiB");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (position &&
        (!yyjson_is_uint(position) || yyjson_get_uint(position) > strlen(text))) {
        set_error(error, size, "offset must be a byte position within source");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_val *rename = yyjson_obj_get(request, "rename");
    if (rename && (!position || !yyjson_is_str(rename) ||
                   strlen(yyjson_get_str(rename)) != yyjson_get_len(rename))) {
        set_error(error, size, "rename requires offset and a NUL-free name");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_val *location = yyjson_obj_get(request, "location");
    if (location && (!yyjson_is_obj(location) ||
                     !yyjson_is_uint(yyjson_obj_get(location, "graph_id")) ||
                     !yyjson_is_uint(yyjson_obj_get(location, "node_id")) ||
                     (yyjson_obj_get(location, "pin_id") &&
                      !yyjson_is_uint(yyjson_obj_get(location, "pin_id"))))) {
        set_error(error, size, "location requires unsigned graph_id, node_id and optional pin_id");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    authoring_language_catalog *native = source_native_catalog(root, error, size);
    if (!native) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    const char *content = chibi_automation_content(root);
    asset_ref ref = { 0 };
    char ignored[256] = "";
    bool bound = !resolve_asset_ref(request, &ref, ignored, sizeof ignored) &&
                 source_program_family(ref.family);
    char *path = content_path(root, bound ? ref.relative : "content/behaviors/__draft.chibi");
    source_catalog_edit edit = { .path = path, .text = text };
    source_catalog *catalog = source_catalog_edited(
        content,
        authoring_language_catalog_view(native),
        &edit,
        1
    );
    const script_catalog *view = source_catalog_view(catalog);
    authoring_document *documents = NULL;
    script_analysis **analyses = NULL;
    int selected = -1;
    for (int i = 0; i < source_catalog_source_count(catalog); i++) {
        const source_catalog_source *source = source_catalog_source_at(catalog, i);
        script_analysis
            *analysis = script_analyze_document(source->syntax, view, source->identity);
        apush(analyses, analysis);
        apush(documents, ((authoring_document){ source->path, source->syntax, analysis }));
        if (!strcmp(path, source->path)) {
            selected = i;
        }
    }
    if (selected < 0) {
        set_error(error, size, "source was not accepted into the bounded project snapshot");
        for (int i = 0; i < acount(analyses); i++) {
            script_analysis_free(analyses[i]);
        }
        afree(analyses);
        afree(documents);
        source_catalog_free(catalog);
        authoring_language_catalog_free(native);
        free(path);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    authoring_navigation *navigation = authoring_navigation_new_project(
        documents,
        acount(documents),
        view,
        source_catalog_definitions(catalog),
        source_catalog_definition_count(catalog),
        source_catalog_references(catalog),
        source_catalog_reference_count(catalog)
    );
    const script_analysis *analysis = analyses[selected];
    const script_syntax *syntax = documents[selected].syntax;
    yyjson_mut_obj_add_bool(doc, result, "valid", analysis->valid);
    if (analysis->name) {
        yyjson_mut_obj_add_strcpy(doc, result, "name", analysis->name);
    }
    const char *kinds[] = { "behavior", "library", "system" };
    yyjson_mut_obj_add_strcpy(doc, result, "kind", kinds[analysis->kind]);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(doc, result, "diagnostics");
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, diagnostics);
        source_range_json(doc, item, analysis->diagnostics[i].range);
        yyjson_mut_obj_add_strcpy(doc, item, "message", analysis->diagnostics[i].message);
        yyjson_mut_obj_add_int(doc, item, "severity", 1);
    }
    for (int i = 0; i < analysis->warning_count; i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, diagnostics);
        source_range_json(doc, item, analysis->warnings[i].range);
        yyjson_mut_obj_add_strcpy(doc, item, "message", analysis->warnings[i].message);
        yyjson_mut_obj_add_int(doc, item, "severity", 2);
    }
    if (compile && analysis->valid) {
        script_lower_bindings bindings = authoring_language_bindings(native);
        authoring_sources *prepared = authoring_sources_prepare_catalog(
            source_catalog_edited(content, authoring_language_catalog_view(native), &edit, 1),
            &bindings,
            BEHAVIOR_BYTECODE
        );
        for (int i = 0; i < authoring_sources_count(prepared); i++) {
            const authoring_source *source = authoring_sources_at(prepared, i);
            if (strcmp(source->path, path)) {
                continue;
            }
            yyjson_mut_obj_put(
                result,
                yyjson_mut_str(doc, "valid"),
                yyjson_mut_bool(doc, source->valid)
            );
            for (int j = 0; j < source->diagnostic_count; j++) {
                yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, diagnostics);
                source_range_json(doc, item, source->diagnostics[j].range);
                yyjson_mut_obj_add_strcpy(
                    doc,
                    item,
                    "message",
                    source->diagnostics[j].message
                );
                yyjson_mut_obj_add_int(doc, item, "severity", 1);
            }
        }
        authoring_sources_free(prepared);
    }
    yyjson_mut_val *queries = yyjson_mut_obj_add_arr(doc, result, "queries");
    yyjson_mut_obj_add_int(doc, result, "query", analysis->query);
    for (int i = 0; i < analysis->query_count; i++) {
        const script_query *query = &analysis->queries[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, queries);
        yyjson_mut_obj_add_int(doc, item, "kind", query->kind);
        yyjson_mut_obj_add_uint(doc, item, "component", query->component);
        source_range_json(doc, item, query->range);
        yyjson_mut_val *children = yyjson_mut_obj_add_arr(doc, item, "children");
        for (int j = 0; j < query->child_count; j++) {
            yyjson_mut_arr_add_int(doc, children, query->children[j]);
        }
    }
    int highlight_count = 0;
    authoring_highlight *highlights = authoring_source_highlights(navigation, selected, &highlight_count);
    const char *span_fields[] = { "spans", "semantic_spans" };
    for (int field = 0; field < 2; field++) {
        yyjson_mut_val *spans = yyjson_mut_obj_add_arr(doc, result, span_fields[field]);
        for (int i = 0; i < highlight_count; i++) {
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, spans);
            source_range_json(doc, item, highlights[i].range);
            yyjson_mut_obj_add_strcpy(doc, item, "class", highlights[i].cls);
        }
    }
    free(highlights);
    int inspection_status = CHIBI_AUTOMATION_OK;
    authoring_source_presentation
        *presentation = authoring_source_presentation_new(analysis, view, NULL);
    if (presentation) {
        av_doc *values = av_doc_new();
        yyjson_mut_obj_add_val(
            doc,
            result,
            "document",
            av_to_json(doc, behavior_asset_to_value(values, authoring_source_presentation_view(presentation)))
        );
        av_doc_free(values);
        if (yyjson_get_bool(yyjson_obj_get(request, "inspect_source_graph"))) {
            inspection_status = inspect_source_presentation(request, doc, result, presentation, error, size);
        }
        yyjson_mut_val *origins = yyjson_mut_obj_add_arr(doc, result, "origins");
        for (int i = 0; i < authoring_source_presentation_origin_count(presentation);
             i++) {
            const source_presentation_origin
                *origin = authoring_source_presentation_origin_at(presentation, i);
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, origins);
            yyjson_mut_obj_add_uint(doc, item, "identity", origin->identity);
            yyjson_mut_obj_add_uint(doc, item, "semantic", origin->semantic);
            yyjson_mut_obj_add_uint(doc, item, "graph_id", origin->graph);
            yyjson_mut_obj_add_uint(doc, item, "node_id", origin->node);
            yyjson_mut_obj_add_uint(doc, item, "pin_id", origin->pin);
            source_range_json(doc, item, origin->range);
        }
        if (location) {
            const source_presentation_origin *origin = authoring_source_presentation_origin(
                presentation,
                yyjson_get_uint(yyjson_obj_get(location, "graph_id")),
                yyjson_get_uint(yyjson_obj_get(location, "node_id")),
                yyjson_get_uint(yyjson_obj_get(location, "pin_id"))
            );
            if (origin) {
                source_range_json(
                    doc,
                    yyjson_mut_obj_add_obj(doc, result, "location"),
                    origin->range
                );
            }
        }
    }
    char formatting_error[192] = "";
    char *formatted = authoring_source_format(syntax, formatting_error, sizeof formatting_error);
    if (formatted) {
        yyjson_mut_obj_add_strcpy(doc, result, "formatted", formatted);
    }
    free(formatted);
    if (position) {
        int offset = (int)yyjson_get_uint(position);
        authoring_completions completions = { 0 };
        authoring_source_complete(navigation, selected, offset, &completions);
        yyjson_mut_val *items = yyjson_mut_obj_add_arr(doc, result, "completions");
        const char *completion_kinds[] = { "variable", "function", "event",
                                           "type",     "field",    "keyword" };
        for (int i = 0; i < completions.count; i++) {
            authoring_completion *completion = &completions.items[i];
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, items);
            yyjson_mut_obj_add_strcpy(doc, item, "label", completion->label);
            yyjson_mut_obj_add_strcpy(
                doc,
                item,
                "kind",
                completion_kinds[completion->kind]
            );
            yyjson_mut_obj_add_strcpy(doc, item, "detail", completion->detail);
            yyjson_mut_obj_add_strcpy(doc, item, "insert", completion->insert);
            source_range_json(doc, item, completions.range);
        }
        authoring_completions_free(&completions);
        char *hover = authoring_source_hover(navigation, selected, offset);
        if (hover) {
            yyjson_mut_obj_add_strcpy(doc, result, "hover", hover);
        }
        free(hover);
        if (rename) {
            source_rename_proposal(
                content,
                authoring_language_catalog_view(native),
                navigation,
                selected,
                offset,
                yyjson_get_str(rename),
                doc,
                result
            );
        }
        const authoring_reference *picked = authoring_navigation_at(navigation, selected, offset);
        const authoring_reference
            *definition = picked
                            ? authoring_navigation_definition(navigation, picked->target)
                            : NULL;
        if (definition) {
            source_location_json(
                navigation,
                doc,
                yyjson_mut_obj_add_obj(doc, result, "definition"),
                definition->location
            );
        }
        yyjson_mut_val *references = yyjson_mut_obj_add_arr(doc, result, "references");
        for (int i = 0; picked && i < authoring_navigation_reference_count(navigation);
             i++) {
            const authoring_reference *reference = authoring_navigation_reference_at(navigation, i);
            if (authoring_target_equal(picked->target, reference->target)) {
                source_location_json(
                    navigation,
                    doc,
                    yyjson_mut_arr_add_obj(doc, references),
                    reference->location
                );
            }
        }
        yyjson_mut_val *calls = yyjson_mut_obj_add_arr(doc, result, "calls");
        authoring_target
            target = picked ? authoring_navigation_callable(navigation, picked->target)
                            : (authoring_target){ 0 };
        for (int i = 0; target.id && i < authoring_navigation_call_count(navigation);
             i++) {
            const authoring_call *call = authoring_navigation_call_at(navigation, i);
            authoring_target caller = authoring_navigation_caller(navigation, call);
            bool incoming = authoring_target_equal(target, call->callee);
            bool outgoing = authoring_target_equal(target, caller);
            if (!incoming && !outgoing) {
                continue;
            }
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, calls);
            yyjson_mut_obj_add_str(doc, item, "direction", incoming ? "caller" : "callee");
            yyjson_mut_obj_add_uint(doc, item, "caller_id", caller.id);
            yyjson_mut_obj_add_uint(doc, item, "target_id", call->callee.id);
            source_location_json(navigation, doc, item, call->location);
            const char *name = authoring_navigation_name(navigation, call->callee);
            if (name) {
                yyjson_mut_obj_add_strcpy(doc, item, "target", name);
            }
        }
    }
    authoring_source_presentation_free(presentation);
    authoring_navigation_free(navigation);
    for (int i = 0; i < acount(analyses); i++) {
        script_analysis_free(analyses[i]);
    }
    afree(analyses);
    afree(documents);
    source_catalog_free(catalog);
    authoring_language_catalog_free(native);
    free(path);
    return inspection_status;
}

static void catalog_validate_ui(const char *root, catalog_asset *asset)
{
    char error[256] = { 0 };
    if (!read_ui_asset(root, asset->name, error, sizeof error)) {
        asset->valid = false;
        asset->errors = 1;
        catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
    }
}

static int load_catalog_asset(
    const char *root,
    const catalog_links *links,
    const asset_entry *entry,
    catalog_asset *asset,
    char *error,
    size_t error_size
)
{
    *asset = (catalog_asset) {
        .asset = {
            .family = entry->family,
            .filename = xstrdup(entry->filename),
            .relative = xstrdup(entry->relative),
            .name = xstrdup(entry->name),
            .bytes = entry->bytes,
            .hash = entry->hash,
        },
        .name = xstrdup(entry->name),
        .valid = true,
    };
    char *path = content_path(root, entry->relative);
    char read_error[256] = { 0 };
    char *json = read_file(path, NULL, NULL, read_error, sizeof read_error);
    if (!json) {
        free(path);
        free_catalog_assets(asset);
        return set_error(error, error_size, read_error);
    }
    if (entry->family->loader == ASSET_UI) {
        catalog_validate_ui(root, asset);
        free(path);
        free(json);
        return 0;
    }
    yyjson_doc *document = NULL;
    if (source_program_family(entry->family)) {
        for (int i = 0; links->sources && i < authoring_sources_count(links->sources);
             i++) {
            const authoring_source *source = authoring_sources_at(links->sources, i);
            if (strcmp(source->path, path)) {
                continue;
            }
            authoring_source_presentation *presentation = authoring_source_presentation_new(
                source->analysis,
                authoring_sources_catalog(links->sources),
                NULL
            );
            if (presentation) {
                av_doc *values = av_doc_new();
                char *text = av_json_write(
                    behavior_asset_to_value(values, authoring_source_presentation_view(presentation))
                );
                document = text ? yyjson_read(text, strlen(text), 0) : NULL;
                free(text);
                av_doc_free(values);
                authoring_source_presentation_free(presentation);
            }
            break;
        }
    } else {
        document = parse_project_asset_path(root, entry->relative, json);
    }
    if (!document) {
        asset->valid = false;
        asset->errors = 1;
        catalog_add_diagnostic(asset, "error", 0, "JSON", "asset source is invalid");
    } else {
        yyjson_val *json_root = yyjson_doc_get_root(document);
        yyjson_val *version = yyjson_obj_get(json_root, "version");
        if (yyjson_is_num(version)) {
            asset->schema_version = yyjson_get_sint(version);
        }
        const char *description = request_string(json_root, "description");
        if (description) {
            asset->description = xstrdup(description);
        }
        catalog_scan_members(asset, json_root);
        yyjson_doc_free(document);
        catalog_validate_asset(root, links, asset, json, path);
    }
    free(path);
    free(json);
    return 0;
}

static int cached_catalog_asset_index(const catalog_asset *assets, const asset_entry *entry)
{
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].asset.family == entry->family &&
            !strcmp(assets[i].asset.relative, entry->relative) &&
            assets[i].asset.bytes == entry->bytes &&
            assets[i].asset.hash == entry->hash) {
            return i;
        }
    }
    return -1;
}

static void reset_catalog_derived_data(catalog_asset *asset)
{
    free_catalog_references(asset->dependencies);
    free_catalog_references(asset->dependents);
    asset->dependencies = NULL;
    asset->dependents = NULL;
}

/* Graph assets validate linked with the libraries and events, so their
   cached results hold while those are unchanged. */
static bool catalog_links_unchanged(const catalog_asset *previous, const asset_entry *entries)
{
    int unmatched = 0;
    for (int i = 0; i < acount(previous); i++) {
        unmatched += links_family(previous[i].asset.family);
    }
    for (int i = 0; i < acount(entries); i++) {
        if (!links_family(entries[i].family)) {
            continue;
        }
        if (cached_catalog_asset_index(previous, &entries[i]) < 0) {
            return false;
        }
        unmatched--;
    }
    return unmatched == 0;
}

static int build_catalog_from_entries(
    const char *root,
    const asset_entry *entries,
    catalog_asset *previous,
    catalog_asset **out,
    reference_index **references,
    char *error,
    size_t error_size
)
{
    for (int i = 0; i < acount(entries); i++) {
        if (strcmp(entries[i].family->kind, "model")) {
            continue;
        }
        for (int j = 0; j < acount(entries); j++) {
            if (!strcmp(entries[j].family->kind, "shape") &&
                !strcmp(entries[i].filename, entries[j].filename)) {
                snprintf(
                    error,
                    error_size,
                    "model and shape asset names must be unique: %s",
                    entries[i].filename
                );
                return CHIBI_AUTOMATION_VALIDATION_ERROR;
            }
        }
    }
    catalog_asset *assets = NULL;
    bool graphs_cached = previous && catalog_links_unchanged(previous, entries);
    catalog_links links = { .studio = studio_project_is(root) };
    load_catalog_links(root, entries, &links);
    int status = 0;
    for (int i = 0; i < acount(entries) && !status; i++) {
        asset_loader loader = entries[i].family->loader;
        bool graph = loader == ASSET_GRAPH || loader == ASSET_SYSTEM;
        /* A prototype is loaded again each time, since a prototype it
           includes may have changed or appeared. */
        int previous_index = previous && (!graph || graphs_cached) && loader != ASSET_PROTOTYPE
                               ? cached_catalog_asset_index(previous, &entries[i])
                               : -1;
        if (previous_index >= 0) {
            catalog_asset asset = previous[previous_index];
            previous[previous_index] = (catalog_asset){ 0 };
            reset_catalog_derived_data(&asset);
            apush(assets, asset);
            continue;
        }
        catalog_asset asset = { 0 };
        status = load_catalog_asset(root, &links, &entries[i], &asset, error, error_size);
        if (!status) {
            apush(assets, asset);
        }
    }
    if (!status) {
        status = catalog_index_references(root, assets, links.sources, references, error, error_size);
    }
    free_behavior_assets(links.assets);
    authoring_sources_free(links.sources);
    authoring_language_catalog_free(links.native);
    if (status) {
        free_catalog_assets(assets);
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    *out = assets;
    return CHIBI_AUTOMATION_OK;
}

static int enumerate_assets(const char *root, asset_entry **out, char *error, size_t error_size)
{
    const char *content = chibi_automation_content(root);
    asset_entry *entries = NULL;
    for (int family_index = 0;
         family_index < (int)(sizeof ASSET_FAMILIES / sizeof *ASSET_FAMILIES);
         family_index++) {
        const asset_family *family = &ASSET_FAMILIES[family_index];
        int directory_status = verify_asset_directory(root, family, error, error_size);
        if (directory_status < 0) {
            free_asset_entries(entries);
            return -1;
        }
        if (directory_status == 0) {
            continue;
        }
        size_t directory_size = strlen(content) + strlen(family->folder) + 2;
        char *directory_path = xmalloc(directory_size);
        snprintf(directory_path, directory_size, "%s/%s", content, family->folder);
        DIR *directory = opendir(directory_path);
        if (!directory) {
            free(directory_path);
            free_asset_entries(entries);
            return set_errorf(
                error,
                error_size,
                "could not enumerate %s",
                family->folder
            );
        }
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (entry->d_name[0] == '.' ||
                !valid_asset_filename(family, entry->d_name)) {
                continue;
            }
            size_t full_size = strlen(directory_path) + strlen(entry->d_name) + 2;
            char *full_path = xmalloc(full_size);
            snprintf(full_path, full_size, "%s/%s", directory_path, entry->d_name);
            struct stat info;
            int regular = lstat(full_path, &info) == 0 && S_ISREG(info.st_mode);
            if (!regular) {
                free(full_path);
                continue;
            }
            size_t bytes = 0;
            uint64_t hash = 0;
            char read_error[160] = { 0 };
            char *contents = read_file(full_path, &bytes, &hash, read_error, sizeof read_error);
            if (!contents) {
                free(full_path);
                closedir(directory);
                free(directory_path);
                free_asset_entries(entries);
                return set_error(error, error_size, read_error);
            }
            asset_entry item = {
                .family = family,
                .filename = xstrdup(entry->d_name),
                .relative = xmalloc(
                    strlen("content/") + strlen(family->folder) + strlen(entry->d_name) + 2
                ),
                .name = asset_name(family, entry->d_name, contents),
                .bytes = bytes,
                .hash = hash,
            };
            free(contents);
            snprintf(
                item.relative,
                strlen("content/") + strlen(family->folder) + strlen(entry->d_name) + 2,
                "content/%s/%s",
                family->folder,
                entry->d_name
            );
            apush(entries, item);
            free(full_path);
        }
        closedir(directory);
        free(directory_path);
    }
    if (acount(entries) > 1) {
        asort(entries, asset_entry_compare);
    }
    *out = entries;
    return 0;
}

static bool asset_entries_equal(const asset_entry *left, const asset_entry *right)
{
    if (acount(left) != acount(right)) {
        return false;
    }
    for (int i = 0; i < acount(left); i++) {
        if (left[i].family != right[i].family ||
            strcmp(left[i].relative, right[i].relative) ||
            left[i].bytes != right[i].bytes || left[i].hash != right[i].hash) {
            return false;
        }
    }
    return true;
}

/* CHIBI_AUTOMATION_OK, VALIDATION_ERROR for asset names that collide, or
 * IO_ERROR, with why in `error`. */
static int catalog_snapshot_get(
    const char *root,
    catalog_asset **out,
    uint64_t *revision,
    uint64_t *content_hash,
    char *error,
    size_t error_size
)
{
    asset_entry *entries = NULL;
    if (enumerate_assets(root, &entries, error, error_size) != 0) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    uint64_t components = components_hash();
    if (CATALOG_SNAPSHOT.assets && !strcmp(CATALOG_SNAPSHOT.root, root) &&
        !strcmp(CATALOG_SNAPSHOT.content, chibi_automation_content(root)) &&
        CATALOG_SNAPSHOT.components == components &&
        asset_entries_equal(entries, CATALOG_SNAPSHOT.entries)) {
        free_asset_entries(entries);
        *out = CATALOG_SNAPSHOT.assets;
        *revision = CATALOG_SNAPSHOT.revision;
        *content_hash = CATALOG_SNAPSHOT.content_hash;
        return 0;
    }
    catalog_asset *assets = NULL;
    reference_index *references = NULL;
    /* Validity and graphs depend on the component schemas, so reuse nothing
     * built against other ones. */
    catalog_asset *previous = CATALOG_SNAPSHOT.components == components
                                ? CATALOG_SNAPSHOT.assets
                                : NULL;
    int status = build_catalog_from_entries(root, entries, previous, &assets, &references, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        free_asset_entries(entries);
        free_catalog_snapshot(&CATALOG_SNAPSHOT);
        return status;
    }
    uint64_t next_revision = CATALOG_SNAPSHOT.revision + 1;
    free_catalog_snapshot(&CATALOG_SNAPSHOT);
    CATALOG_SNAPSHOT.root = xstrdup(root);
    CATALOG_SNAPSHOT.content = xstrdup(chibi_automation_content(root));
    CATALOG_SNAPSHOT.entries = entries;
    CATALOG_SNAPSHOT.assets = assets;
    CATALOG_SNAPSHOT.references = references;
    CATALOG_SNAPSHOT.revision = next_revision;
    CATALOG_SNAPSHOT.content_hash = catalog_content_hash(assets);
    CATALOG_SNAPSHOT.components = components;
    *out = CATALOG_SNAPSHOT.assets;
    *revision = CATALOG_SNAPSHOT.revision;
    *content_hash = CATALOG_SNAPSHOT.content_hash;
    return 0;
}

static void hash_text(uint64_t hash, char text[17])
{
    snprintf(text, 17, "%016llx", (unsigned long long)hash);
}

static void add_hash_named(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, uint64_t hash)
{
    char text[17];
    hash_text(hash, text);
    yyjson_mut_obj_add_strcpy(doc, object, key, text);
}

static void add_hash(yyjson_mut_doc *doc, yyjson_mut_val *object, uint64_t hash)
{
    add_hash_named(doc, object, "hash", hash);
}

static yyjson_mut_val *add_asset_entry(yyjson_mut_doc *doc, yyjson_mut_val *array, const asset_entry *entry)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", entry->family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", entry->name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", entry->relative);
    yyjson_mut_obj_add_uint(doc, object, "bytes", entry->bytes);
    add_hash(doc, object, entry->hash);
    return object;
}

static const char *pin_json_name(pin_type type)
{
    const char *name = pin_type_name(type);
    return name ? name : "flow";
}

static const char *container_json_name(pin_container container)
{
    switch (container) {
        case PIN_CONTAINER_SINGLE: return "single";
        case PIN_CONTAINER_ARRAY: return "array";
        case PIN_CONTAINER_SET: return "set";
        case PIN_CONTAINER_MAP: return "map";
        case PIN_CONTAINER_VIEW: return "view";
    }
    return "invalid";
}

static void add_pin(yyjson_mut_doc *doc, yyjson_mut_val *array, const pin_spec *pin)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "name", pin->name ? pin->name : "");
    yyjson_mut_obj_add_strcpy(doc, object, "type", pin_json_name(pin->type));
    yyjson_mut_obj_add_strcpy(doc, object, "kind", pin->kind == PIN_EXEC ? "exec" : "data");
    yyjson_mut_obj_add_strcpy(doc, object, "container", container_json_name(pin->container));
    yyjson_mut_obj_add_uint(doc, object, "id", pin->id);
    yyjson_mut_obj_add_uint(doc, object, "flags", pin->flags);
    yyjson_mut_obj_add_int(doc, object, "group", pin->group);
    if (pin->type_name) {
        yyjson_mut_obj_add_strcpy(doc, object, "type_name", pin->type_name);
    }
    if (pin->field_name) {
        yyjson_mut_obj_add_strcpy(doc, object, "field_name", pin->field_name);
    }
    if (pin->field_source) {
        yyjson_mut_obj_add_uint(doc, object, "field_source", pin->field_source);
    }
}

static yyjson_mut_val *add_pins(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *key,
    const pin_spec *pins,
    int count
)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, key);
    for (int i = 0; i < count; i++) {
        add_pin(doc, array, &pins[i]);
    }
    return array;
}

static void add_type(yyjson_mut_doc *doc, yyjson_mut_val *array, const type_desc *type)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "name", type->name ? type->name : "");
    yyjson_mut_obj_add_uint(doc, object, "bytes", type->size);
    if (type->alias_of) {
        yyjson_mut_obj_add_strcpy(doc, object, "alias_of", type->alias_of);
    }
    yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, fields);
        yyjson_mut_obj_add_strcpy(doc, entry, "name", field->name ? field->name : "");
        yyjson_mut_obj_add_strcpy(doc, entry, "type", pin_json_name(field->type));
        yyjson_mut_obj_add_uint(doc, entry, "offset", field->offset);
        yyjson_mut_obj_add_uint(doc, entry, "bytes", field->size);
        if (field->type_name) {
            yyjson_mut_obj_add_strcpy(doc, entry, "type_name", field->type_name);
        }
    }
}

static void add_registry(yyjson_mut_doc *doc, yyjson_mut_val *object)
{
    yyjson_mut_obj_add_int(doc, object, "packs", registry_pack_count());
    yyjson_mut_obj_add_int(doc, object, "nodes", registry_node_count());
    yyjson_mut_obj_add_int(doc, object, "types", type_catalog_count());
    yyjson_mut_obj_add_int(doc, object, "services", registry_service_count());
    yyjson_mut_obj_add_int(doc, object, "components", project_components().component_count);
}

static int handle_describe_contract(response *result)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_uint(result->doc, value, "contract_version", CHIBI_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_int(result->doc, value, "response_version", 1);
    yyjson_mut_val *errors = yyjson_mut_obj_add_arr(result->doc, value, "error_categories");
    for (int i = 0;
         i < (int)(sizeof CONTRACT_ERROR_CATEGORIES / sizeof *CONTRACT_ERROR_CATEGORIES);
         i++) {
        yyjson_mut_arr_add_strcpy(result->doc, errors, CONTRACT_ERROR_CATEGORIES[i]);
    }
    yyjson_mut_val *operations = yyjson_mut_obj_add_arr(result->doc, value, "operations");
    for (int i = 0;
         i < (int)(sizeof CONTRACT_OPERATIONS / sizeof *CONTRACT_OPERATIONS);
         i++) {
        const operation_descriptor *operation = &CONTRACT_OPERATIONS[i];
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(result->doc, operations);
        yyjson_mut_obj_add_strcpy(result->doc, object, "name", operation->name);
        yyjson_mut_obj_add_strcpy(result->doc, object, "summary", operation->summary);
        yyjson_mut_obj_add_int(
            result->doc,
            object,
            "response_version",
            operation->response_version
        );
        yyjson_mut_obj_add_bool(result->doc, object, "read_only", operation->read_only);
        yyjson_mut_obj_add_bool(result->doc, object, "idempotent", operation->idempotent);
        yyjson_mut_obj_add_bool(result->doc, object, "destructive", operation->destructive);
        yyjson_mut_val *fields = yyjson_mut_obj_add_arr(result->doc, object, "request_fields");
        for (int field_index = 0; field_index < operation->field_count; field_index++) {
            const contract_field *field = &operation->fields[field_index];
            yyjson_mut_val *field_value = yyjson_mut_arr_add_obj(result->doc, fields);
            yyjson_mut_obj_add_strcpy(result->doc, field_value, "name", field->name);
            yyjson_mut_obj_add_strcpy(result->doc, field_value, "type", field->type);
            yyjson_mut_obj_add_bool(result->doc, field_value, "required", field->required);
            yyjson_mut_obj_add_strcpy(
                result->doc,
                field_value,
                "description",
                field->description
            );
        }
    }
    yyjson_mut_val *reflection = yyjson_mut_obj_add_obj(result->doc, value, "reflection");
    add_registry(result->doc, reflection);
    return 0;
}

/* Diagnostics name their pin as <node>.<key> when the asset resolves it. */
static void add_diagnostic_pin(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const behavior_asset *asset,
    const compile_diagnostic *diagnostic
)
{
    graph *g = asset ? behavior_asset_find_graph(asset, diagnostic->graph_id) : NULL;
    node *n = g && diagnostic->pin_id ? graph_node_by_id(g, diagnostic->node_id) : NULL;
    int output = 0, index = -1;
    char ref[SERIALIZE_KEY_SIZE + 32];
    if (n && graph_pin_by_id(n, diagnostic->pin_id, &output, &index) &&
        serialize_pin_ref(n, output, index, ref, sizeof ref)) {
        yyjson_mut_obj_add_strcpy(doc, object, "pin", ref);
        return;
    }
    yyjson_mut_obj_add_uint(doc, object, "pin", diagnostic->pin_id);
}

static void add_diagnostic(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const behavior_asset *asset,
    const compile_diagnostic *diagnostic
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(
        doc,
        object,
        "severity",
        cmpl_severity_name(diagnostic->severity)
    );
    yyjson_mut_obj_add_uint(doc, object, "graph", diagnostic->graph_id);
    yyjson_mut_obj_add_uint(doc, object, "node", diagnostic->node_id);
    add_diagnostic_pin(doc, object, asset, diagnostic);
    yyjson_mut_obj_add_int(doc, object, "rule", diagnostic->rule);
    yyjson_mut_obj_add_strcpy(
        doc,
        object,
        "rule_name",
        cmpl_rule_name(diagnostic->rule)
    );
    yyjson_mut_obj_add_strcpy(doc, object, "message", diagnostic->message);
}

static void add_load_diagnostic(yyjson_mut_doc *doc, yyjson_mut_val *array, const char *message)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "severity", "error");
    yyjson_mut_obj_add_strcpy(doc, object, "rule_name", "LOAD");
    yyjson_mut_obj_add_strcpy(doc, object, "message", message ? message : "asset rejected");
}

static void add_asset_identity(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const asset_ref *ref,
    size_t bytes,
    uint64_t hash
)
{
    yyjson_mut_obj_put(
        object,
        yyjson_mut_str(doc, "kind"),
        yyjson_mut_strcpy(doc, ref->family->kind)
    );
    yyjson_mut_obj_put(object, yyjson_mut_str(doc, "name"), yyjson_mut_strcpy(doc, ref->name));
    yyjson_mut_obj_add_strcpy(doc, object, "path", ref->relative);
    yyjson_mut_obj_add_uint(doc, object, "bytes", bytes);
    add_hash(doc, object, hash);
}

static yyjson_mut_val *add_catalog_reference(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const catalog_reference *reference,
    const catalog_asset *assets,
    int count
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", reference->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", reference->name);
    yyjson_mut_obj_add_bool(doc, object, "resolved", reference->resolved);
    if (reference->resolved) {
        int index = catalog_asset_index(
            assets,
            count,
            reference->kind,
            reference->name
        );
        if (index >= 0) {
            yyjson_mut_obj_add_strcpy(doc, object, "path", assets[index].asset.relative);
        }
    }
    return object;
}

/* A source's stand-in for the declaration catalog's own diagnostics. */
static bool catalog_fallout(const char *message)
{
    return !strcmp(message, AUTHORING_SOURCES_CATALOG_INCOMPLETE);
}

/* Notes a check that failed only with catalog fallout and lets the checks
 * after it run, so the fallout cannot hide an asset's own problems. */
static int past_fallout(int status, char *error, bool *fallout)
{
    if (status == CHIBI_AUTOMATION_OK || !catalog_fallout(error)) {
        return status;
    }
    *fallout = true;
    error[0] = '\0';
    return CHIBI_AUTOMATION_OK;
}

/* The fallout past_fallout set aside, once no other problem turned up. */
static int fallout_status(bool fallout, char *error, size_t error_size)
{
    if (!fallout) {
        return CHIBI_AUTOMATION_OK;
    }
    set_error(error, error_size, AUTHORING_SOURCES_CATALOG_INCOMPLETE);
    return CHIBI_AUTOMATION_VALIDATION_ERROR;
}

/* Whether `diagnostic` is the declaration catalog's `message` for the file at `path`. */
static bool catalog_names(const source_catalog_diagnostic *diagnostic, const char *path, const char *message)
{
    return diagnostic->path && !strcmp(diagnostic->path, path) &&
           !strcmp(diagnostic->message, message);
}

/* Whether the declaration catalog lists `message` for the file at `path`. */
static bool catalog_lists(const source_catalog *catalog, const char *path, const char *message)
{
    for (int i = 0; i < source_catalog_diagnostic_count(catalog); i++) {
        if (catalog_names(source_catalog_diagnostic_at(catalog, i), path, message)) {
            return true;
        }
    }
    return false;
}

/* Marks the asset's own diagnostics validate_project leaves out: catalog
 * fallout and consequences of an error already reported at its declaration.
 * The declaration diagnostics retain their original source locations. */
static bool *catalog_omitted(const source_catalog *catalog, const catalog_asset *asset, const char *path)
{
    bool *omitted = xcalloc((size_t)acount(asset->diagnostics) + 1, sizeof *omitted);
    for (int i = 0; i < acount(asset->diagnostics); i++) {
        const char *message = asset->diagnostics[i].message;
        omitted[i] = catalog_fallout(message) || catalog_lists(catalog, path, message);
    }
    return omitted;
}

/* Leaves out the diagnostics `omitted` marks, when it is set. */
static void add_catalog_diagnostics(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const catalog_asset *asset,
    const bool *omitted
)
{
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(doc, object, "diagnostics");
    for (int i = 0; i < acount(asset->diagnostics); i++) {
        const catalog_diagnostic *diagnostic = &asset->diagnostics[i];
        if (omitted && omitted[i]) {
            continue;
        }
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, diagnostics);
        yyjson_mut_obj_add_strcpy(doc, item, "severity", diagnostic->severity);
        yyjson_mut_obj_add_int(doc, item, "rule", diagnostic->rule);
        yyjson_mut_obj_add_strcpy(doc, item, "rule_name", diagnostic->rule_name);
        yyjson_mut_obj_add_strcpy(doc, item, "message", diagnostic->message);
        if (diagnostic->line) {
            yyjson_mut_obj_add_int(doc, item, "line", diagnostic->line);
            yyjson_mut_obj_add_int(doc, item, "column", diagnostic->column);
        }
    }
}

static yyjson_mut_val *add_catalog_asset(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const catalog_asset *asset,
    const catalog_asset *assets,
    int count,
    bool summary
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", asset->asset.family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", asset->name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", asset->asset.relative);
    yyjson_mut_obj_add_int(doc, object, "schema_version", asset->schema_version);
    yyjson_mut_obj_add_uint(doc, object, "bytes", asset->asset.bytes);
    add_hash(doc, object, asset->asset.hash);
    if (asset->description) {
        yyjson_mut_obj_add_strcpy(doc, object, "description", asset->description);
    }
    if (summary) {
        yyjson_mut_obj_add_bool(doc, object, "valid", asset->valid);
        yyjson_mut_obj_add_int(doc, object, "errors", asset->errors);
        yyjson_mut_obj_add_int(doc, object, "warnings", asset->warnings);
        return object;
    }
    yyjson_mut_val *members = yyjson_mut_obj_add_arr(doc, object, "members");
    for (int i = 0; i < acount(asset->members); i++) {
        yyjson_mut_val *member = yyjson_mut_arr_add_obj(doc, members);
        yyjson_mut_obj_add_strcpy(doc, member, "kind", asset->members[i].kind);
        yyjson_mut_obj_add_strcpy(doc, member, "name", asset->members[i].name);
    }
    yyjson_mut_val *dependencies = yyjson_mut_obj_add_arr(doc, object, "dependencies");
    for (int i = 0; i < acount(asset->dependencies); i++) {
        add_catalog_reference(doc, dependencies, &asset->dependencies[i], assets, count);
    }
    yyjson_mut_val *dependents = yyjson_mut_obj_add_arr(doc, object, "dependents");
    for (int i = 0; i < acount(asset->dependents); i++) {
        add_catalog_reference(doc, dependents, &asset->dependents[i], assets, count);
    }
    add_catalog_diagnostics(doc, object, asset, NULL);
    yyjson_mut_obj_add_bool(doc, object, "valid", asset->valid);
    yyjson_mut_obj_add_int(doc, object, "errors", asset->errors);
    yyjson_mut_obj_add_int(doc, object, "warnings", asset->warnings);
    return object;
}

static void catalog_hash_text(uint64_t *hash, const char *text)
{
    for (const unsigned char *value = (const unsigned char *)text; *value; value++) {
        *hash ^= *value;
        *hash *= UINT64_C(1099511628211);
    }
}

/* A project's declarations change its component schemas without changing
 * any asset the catalog lists. Units are not layout, but scripts' unit
 * warnings depend on them. */
static uint64_t components_hash()
{
    uint64_t hash = UINT64_C(1469598103934665603);
    ecs_component_catalog catalog = project_components();
    for (int i = 0; i < catalog.component_count; i++) {
        const type_desc *type = catalog.components[i]->type;
        char layout[32];
        snprintf(layout, sizeof layout, ":%016llx;", (unsigned long long)type_layout_hash(type));
        catalog_hash_text(&hash, catalog.components[i]->name);
        catalog_hash_text(&hash, layout);
        for (int j = 0; j < type->field_count; j++) {
            catalog_hash_text(&hash, type->fields[j].unit ? type->fields[j].unit : "");
            catalog_hash_text(&hash, ";");
        }
    }
    return hash;
}

static uint64_t catalog_content_hash(const catalog_asset *assets)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int i = 0; i < acount(assets); i++) {
        char value[64];
        catalog_hash_text(&hash, assets[i].asset.relative);
        snprintf(
            value,
            sizeof value,
            ":%016llx;",
            (unsigned long long)assets[i].asset.hash
        );
        catalog_hash_text(&hash, value);
    }
    return hash;
}

/* How well an asset matches the request, as Studio's palette ranks
   (support/text_search.h): a word of its name above one of its kind, path,
   description or members; -1 when it does not match, 0 without a query. */
static int catalog_search_score(yyjson_val *request, const catalog_asset *asset)
{
    yyjson_val *kinds = yyjson_obj_get(request, "kinds");
    bool kind = !kinds;
    size_t index, count;
    yyjson_val *wanted;
    yyjson_arr_foreach(kinds, index, count, wanted)
    {
        kind |= yyjson_is_str(wanted) &&
                family_by_kind(yyjson_get_str(wanted)) == asset->asset.family;
    }
    if (!kind) {
        return -1;
    }
    const char *query = request_string(request, "query");
    if (!query || !query[0]) {
        return 0;
    }
    /* Members past the buffer's end go unsearched. */
    char secondary[4096];
    int length = snprintf(
        secondary,
        sizeof secondary,
        "%s %s %s",
        asset->asset.family->kind,
        asset->asset.relative,
        asset->description ? asset->description : ""
    );
    for (int i = 0; i < acount(asset->members) && length < (int)sizeof secondary; i++) {
        length += snprintf(
            secondary + length,
            sizeof secondary - (size_t)length,
            " %s",
            asset->members[i].name
        );
    }
    return text_search_word_score(query, asset->name, secondary);
}

typedef struct
{
    int index; /* into the catalog */
    int score;
} catalog_match;

/* Best first; the catalog's order breaks ties. */
static int compare_matches(const void *a, const void *b)
{
    const catalog_match *x = a, *y = b;
    return x->score != y->score ? y->score - x->score : x->index - y->index;
}

static int handle_catalog(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    page_options page;
    if (!request_page(request, &page, result, exit_code)) {
        return -1;
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t content_hash_value = 0;
    char error[256] = { 0 };
    int cataloged = catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error);
    if (cataloged != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, cataloged, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_int(result->doc, value, "catalog_version", 1);
    char content_hash[32];
    snprintf(content_hash, sizeof content_hash, "%016llx", (unsigned long long)content_hash_value);
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_hash", content_hash);
    yyjson_mut_obj_add_strcpy(result->doc, value, "project_root", root);
    yyjson_mut_obj_add_bool(result->doc, value, "summary", page.summary);
    uint64_t total = (uint64_t)acount(assets);
    uint64_t start = page.cursor < total ? page.cursor : total;
    uint64_t count = total - start;
    if (page.has_limit && page.limit < count) {
        count = page.limit;
    }
    add_page_metadata(result->doc, value, acount(assets), start, count);
    yyjson_mut_val *asset_array = yyjson_mut_obj_add_arr(result->doc, value, "assets");
    for (uint64_t i = 0; i < count; i++) {
        add_catalog_asset(result->doc, asset_array, &assets[start + i], assets, acount(assets), page.summary);
    }
    /* Registry counts only; describe_registry returns the registry itself. */
    yyjson_mut_val *registry = yyjson_mut_obj_add_obj(result->doc, value, "registry");
    add_registry(result->doc, registry);
    return 0;
}

static int handle_search_assets(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *kinds = yyjson_obj_get(request, "kinds");
    if (kinds && !yyjson_is_arr(kinds)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "kinds must be an array");
        return -1;
    }
    page_options page;
    if (!request_page(request, &page, result, exit_code)) {
        return -1;
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t content_hash_value = 0;
    char error[256] = { 0 };
    int cataloged = catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error);
    if (cataloged != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, cataloged, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    char content_hash[32];
    snprintf(content_hash, sizeof content_hash, "%016llx", (unsigned long long)content_hash_value);
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_hash", content_hash);
    const char *query = request_string(request, "query");
    yyjson_mut_obj_add_strcpy(result->doc, value, "query", query ? query : "");
    yyjson_mut_obj_add_bool(result->doc, value, "summary", page.summary);
    catalog_match *found = NULL;
    for (int i = 0; i < acount(assets); i++) {
        int score = catalog_search_score(request, &assets[i]);
        if (score >= 0) {
            apush(found, ((catalog_match){ i, score }));
        }
    }
    if (acount(found) > 1) {
        asort(found, compare_matches);
    }
    int total = acount(found);
    uint64_t start = page.cursor < (uint64_t)total ? page.cursor : (uint64_t)total;
    uint64_t count = (uint64_t)total - start;
    if (page.has_limit && page.limit < count) {
        count = page.limit;
    }
    add_page_metadata(result->doc, value, total, start, count);
    yyjson_mut_val *matches = yyjson_mut_obj_add_arr(result->doc, value, "assets");
    for (uint64_t i = start; i < start + count; i++) {
        add_catalog_asset(result->doc, matches, &assets[found[i].index], assets, acount(assets), page.summary);
    }
    afree(found);
    return 0;
}

static yyjson_mut_val *add_dependency_node(yyjson_mut_doc *doc, yyjson_mut_val *array, const catalog_asset *asset)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", asset->asset.family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", asset->name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", asset->asset.relative);
    return object;
}

static void add_dependency_edge(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const catalog_asset *from,
    const catalog_asset *to,
    const catalog_reference *unresolved
)
{
    yyjson_mut_val *edge = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_val *from_value = yyjson_mut_obj_add_obj(doc, edge, "from");
    yyjson_mut_obj_add_strcpy(doc, from_value, "kind", from->asset.family->kind);
    yyjson_mut_obj_add_strcpy(doc, from_value, "name", from->name);
    yyjson_mut_obj_add_strcpy(doc, from_value, "path", from->asset.relative);
    yyjson_mut_val *to_value = yyjson_mut_obj_add_obj(doc, edge, "to");
    if (to) {
        yyjson_mut_obj_add_strcpy(doc, to_value, "kind", to->asset.family->kind);
        yyjson_mut_obj_add_strcpy(doc, to_value, "name", to->name);
        yyjson_mut_obj_add_strcpy(doc, to_value, "path", to->asset.relative);
        yyjson_mut_obj_add_bool(doc, to_value, "resolved", true);
    } else {
        yyjson_mut_obj_add_strcpy(doc, to_value, "kind", unresolved->kind);
        yyjson_mut_obj_add_strcpy(doc, to_value, "name", unresolved->name);
        yyjson_mut_obj_add_bool(doc, to_value, "resolved", false);
    }
}

static int handle_asset_dependencies(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *kind = request_string(request, "kind");
    const char *name = request_string(request, "name");
    if (!kind || !name || !name[0] || !family_by_kind(kind)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "asset_dependencies requires a known kind and name");
        return -1;
    }
    const char *direction = request_string(request, "direction");
    if (!direction) {
        direction = "outgoing";
    }
    if (strcmp(direction, "outgoing") && strcmp(direction, "incoming") &&
        strcmp(direction, "both")) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "direction must be outgoing, incoming, or both");
        return -1;
    }
    bool transitive = false;
    yyjson_val *transitive_value = yyjson_obj_get(request, "transitive");
    if (transitive_value) {
        if (!yyjson_is_bool(transitive_value)) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "transitive must be a boolean");
            return -1;
        }
        transitive = yyjson_get_bool(transitive_value);
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t content_hash_value = 0;
    char error[256] = { 0 };
    int cataloged = catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error);
    if (cataloged != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, cataloged, error);
        return -1;
    }
    int count = acount(assets);
    int root_index = catalog_asset_index(assets, count, family_by_kind(kind)->kind, name);
    if (root_index < 0) {
        response_error(result, exit_code, CHIBI_AUTOMATION_NOT_FOUND, "asset not found");
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    char content_hash[32];
    snprintf(content_hash, sizeof content_hash, "%016llx", (unsigned long long)content_hash_value);
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_hash", content_hash);
    yyjson_mut_obj_add_strcpy(result->doc, value, "direction", direction);
    yyjson_mut_obj_add_bool(result->doc, value, "transitive", transitive);
    yyjson_mut_val *root_value = yyjson_mut_obj_add_obj(result->doc, value, "root");
    yyjson_mut_obj_add_strcpy(
        result->doc,
        root_value,
        "kind",
        assets[root_index].asset.family->kind
    );
    yyjson_mut_obj_add_strcpy(
        result->doc,
        root_value,
        "name",
        assets[root_index].name
    );
    yyjson_mut_obj_add_strcpy(
        result->doc,
        root_value,
        "path",
        assets[root_index].asset.relative
    );
    yyjson_mut_obj_add_val(
        result->doc,
        value,
        "asset",
        yyjson_mut_val_mut_copy(result->doc, root_value)
    );
    yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(result->doc, value, "nodes");
    yyjson_mut_val *edges = yyjson_mut_obj_add_arr(result->doc, value, "edges");
    bool *node_seen = xcalloc((size_t)count, sizeof *node_seen);
    bool *edge_seen = xcalloc((size_t)count * (size_t)count, sizeof *edge_seen);
    int *queue = NULL;
    apush(queue, root_index);
    node_seen[root_index] = true;
    add_dependency_node(result->doc, nodes, &assets[root_index]);
    int cursor = 0;
    while (cursor < acount(queue)) {
        int current = queue[cursor++];
        catalog_asset *source = &assets[current];
        bool visit_outgoing = !strcmp(direction, "outgoing") ||
                              !strcmp(direction, "both");
        bool visit_incoming = !strcmp(direction, "incoming") ||
                              !strcmp(direction, "both");
        if (visit_outgoing) {
            for (int i = 0; i < acount(source->dependencies); i++) {
                catalog_reference *reference = &source->dependencies[i];
                int target = reference->resolved ? catalog_asset_index(
                                                       assets,
                                                       count,
                                                       reference->kind,
                                                       reference->name
                                                   )
                                                 : -1;
                if (target < 0) {
                    add_dependency_edge(result->doc, edges, source, NULL, reference);
                    continue;
                }
                if (!edge_seen[current * count + target]) {
                    edge_seen[current * count + target] = true;
                    add_dependency_edge(result->doc, edges, source, &assets[target], NULL);
                }
                if (!node_seen[target]) {
                    node_seen[target] = true;
                    add_dependency_node(result->doc, nodes, &assets[target]);
                    if (transitive) {
                        apush(queue, target);
                    }
                }
            }
        }
        if (visit_incoming) {
            for (int i = 0; i < acount(source->dependents); i++) {
                catalog_reference *reference = &source->dependents[i];
                int dependent = catalog_asset_index(
                    assets,
                    count,
                    reference->kind,
                    reference->name
                );
                if (dependent < 0) {
                    continue;
                }
                if (!edge_seen[dependent * count + current]) {
                    edge_seen[dependent * count + current] = true;
                    add_dependency_edge(result->doc, edges, &assets[dependent], source, NULL);
                }
                if (!node_seen[dependent]) {
                    node_seen[dependent] = true;
                    add_dependency_node(result->doc, nodes, &assets[dependent]);
                    if (transitive) {
                        apush(queue, dependent);
                    }
                }
            }
        }
        if (!transitive) {
            break;
        }
    }
    yyjson_mut_obj_add_int(result->doc, value, "node_count", yyjson_mut_arr_size(nodes));
    yyjson_mut_obj_add_int(result->doc, value, "edge_count", yyjson_mut_arr_size(edges));
    afree(queue);
    free(node_seen);
    free(edge_seen);
    return 0;
}

/* The catalog's names for the reference index's assets; an asset only named
 * takes its kind's family and no path. */
static navigation_asset *navigation_assets(const catalog_asset *assets, const reference_index *index)
{
    int count = reference_index_asset_count(index);
    int *catalog = catalog_positions(assets, index);
    navigation_asset *named = xcalloc((size_t)count + 1, sizeof *named);
    for (int i = 0; i < count; i++) {
        const reference_asset *asset = reference_index_asset_at(index, i);
        const catalog_asset *found = catalog[i] >= 0 ? &assets[catalog[i]] : NULL;
        named[i] = found ? (navigation_asset){ found->asset.family->kind,
                                               found->name,
                                               found->asset.relative }
                         : (navigation_asset){ reference_family_kind(index, i),
                                               asset->name,
                                               NULL };
    }
    free(catalog);
    return named;
}

/* find_references and call_hierarchy, over the catalog's references. */
static int handle_navigation(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    /* UI language service (workstream B): classes, tokens and save keys. */
    if (!strcmp(request_string(request, "operation"), "find_references") &&
        automation_ui_references_requested(request)) {
        char failure[256] = { 0 };
        yyjson_mut_val *found = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
        int status = automation_ui_references(
            chibi_automation_content(root),
            request,
            result->doc,
            found,
            failure,
            sizeof failure
        );
        if (status != CHIBI_AUTOMATION_OK) {
            response_error(result, exit_code, status, failure);
        }
        return status == CHIBI_AUTOMATION_OK ? 0 : -1;
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0, content_hash = 0;
    char error[256] = { 0 };
    int cataloged = catalog_snapshot_get(root, &assets, &revision, &content_hash, error, sizeof error);
    if (cataloged != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, cataloged, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    add_hash_named(result->doc, value, "content_hash", content_hash);
    navigation nav = { CATALOG_SNAPSHOT.references,
                       navigation_assets(assets, CATALOG_SNAPSHOT.references) };
    bool hierarchy = !strcmp(request_string(request, "operation"), "call_hierarchy");
    int status = hierarchy
                   ? automation_call_hierarchy(&nav, request, result->doc, value, error, sizeof error)
                   : automation_find_references(&nav, request, result->doc, value, error, sizeof error);
    free((navigation_asset *)nav.assets);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    return 0;
}

/* inspect_graph's per-member use counts across the project. Skipped when the
 * catalog cannot be read; the graph itself is still inspected. */
static void add_graph_use_counts(
    const char *root,
    const asset_ref *ref,
    yyjson_mut_doc *doc,
    yyjson_mut_val *value
)
{
    catalog_asset *assets = NULL;
    uint64_t revision = 0, content_hash = 0;
    char error[256] = { 0 };
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash, error, sizeof error) !=
        0) {
        return;
    }
    char *name = asset_name_from_filename(ref->filename);
    int at = catalog_asset_index(assets, acount(assets), ref->family->kind, name);
    free(name);
    if (at < 0 || assets[at].reference < 0) {
        return;
    }
    navigation nav = { CATALOG_SNAPSHOT.references,
                       navigation_assets(assets, CATALOG_SNAPSHOT.references) };
    automation_add_use_counts(&nav, assets[at].reference, doc, value);
    free((navigation_asset *)nav.assets);
}

static uint64_t inspect_json_uint(yyjson_val *value)
{
    if (yyjson_is_uint(value)) {
        return yyjson_get_uint(value);
    }
    if (yyjson_is_sint(value) && yyjson_get_sint(value) >= 0) {
        return (uint64_t)yyjson_get_sint(value);
    }
    return 0;
}

static int compare_uint64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static int inspect_node_compare(const void *left, const void *right)
{
    const node *a = *(node *const *)left;
    const node *b = *(node *const *)right;
    return a->id < b->id ? -1 : a->id > b->id ? 1 : 0;
}

static int inspect_item_compare(const void *left, const void *right)
{
    const inspect_json_item *a = left;
    const inspect_json_item *b = right;
    return a->id < b->id ? -1 : a->id > b->id ? 1 : 0;
}

static void inspect_add_sorted_array(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *key,
    yyjson_val *source,
    bool edges,
    bool include_layout
)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, key);
    if (!yyjson_is_arr(source)) {
        return;
    }
    int count = (int)yyjson_arr_size(source);
    inspect_json_item *items = count ? xmalloc((size_t)count * sizeof *items) : NULL;
    size_t index, maximum;
    yyjson_val *item;
    int item_count = 0;
    yyjson_arr_foreach(source, index, maximum, item)
    {
        items[item_count++] = (inspect_json_item){
            .value = item,
            .id = edges ? (uint64_t)index
                        : inspect_json_uint(yyjson_obj_get(item, "id")),
        };
    }
    /* Documents already order edges canonically. */
    if (!edges) {
        if (item_count > 1) {
            qsort(items, (size_t)item_count, sizeof *items, inspect_item_compare);
        }
    }
    for (int i = 0; i < item_count; i++) {
        yyjson_mut_val *copy = yyjson_val_mut_copy(doc, items[i].value);
        if (!include_layout) {
            if (edges) {
                const char *const layout_keys[] = {
                    "from_handle_x", "from_handle_y", "to_handle_x",
                    "to_handle_y",   "reroutes",
                };
                for (int layout = 0;
                     layout < (int)(sizeof layout_keys / sizeof *layout_keys);
                     layout++) {
                    yyjson_mut_obj_remove_key(copy, layout_keys[layout]);
                }
            } else {
                const char *const layout_keys[] = {
                    "x",
                    "y",
                    "placed",
                    "presentation",
                    "show_advanced",
                    "comment_hidden",
                };
                for (int layout = 0;
                     layout < (int)(sizeof layout_keys / sizeof *layout_keys);
                     layout++) {
                    yyjson_mut_obj_remove_key(copy, layout_keys[layout]);
                }
            }
        }
        yyjson_mut_arr_add_val(array, copy);
    }
    free(items);
}

static bool inspect_report_has_missing_reference(const compile_report *report, uint64_t node_id)
{
    for (int i = 0; i < acount(report->items); i++) {
        if (report->items[i].rule == CMPL_RULE_MISSING_REFERENCE &&
            report->items[i].node_id == node_id) {
            return true;
        }
    }
    return false;
}

static void inspect_add_calls(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const behavior_asset *asset,
    const graph *source,
    const compile_report *report
)
{
    yyjson_mut_val *calls = yyjson_mut_obj_add_arr(doc, object, "calls");
    int count = acount(source->nodes);
    node **nodes = count ? xmalloc((size_t)count * sizeof *nodes) : NULL;
    for (int i = 0; i < count; i++) {
        nodes[i] = source->nodes[i];
    }
    if (count > 1) {
        qsort(nodes, (size_t)count, sizeof *nodes, inspect_node_compare);
    }
    for (int i = 0; i < count; i++) {
        const node *call = nodes[i];
        if (!strcmp(call->desc->name, "Model Parameter") ||
            !strcmp(call->desc->name, "Model Part") ||
            !strcmp(call->desc->name, "Model Material Slot") ||
            !strcmp(call->desc->name, "Model Anchor")) {
            continue;
        }
        if (!call->reference_id && !call->reference_name && !call->reference_asset) {
            continue;
        }
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, calls);
        yyjson_mut_obj_add_uint(doc, entry, "node_id", call->id);
        yyjson_mut_obj_add_strcpy(doc, entry, "node_type", call->desc->name);
        yyjson_mut_val *target = yyjson_mut_obj_add_obj(doc, entry, "target");
        bool resolved = true;
        if (call->reference_asset) {
            yyjson_mut_obj_add_strcpy(doc, target, "kind", "asset");
            yyjson_mut_obj_add_strcpy(doc, target, "asset", call->reference_asset);
            if (call->reference_name) {
                yyjson_mut_obj_add_strcpy(doc, target, "name", call->reference_name);
            }
            resolved = !inspect_report_has_missing_reference(report, call->id);
        } else if (call->reference_id) {
            graph *target_graph = behavior_asset_find_graph(asset, call->reference_id);
            yyjson_mut_obj_add_strcpy(doc, target, "kind", "local");
            yyjson_mut_obj_add_uint(doc, target, "graph_id", call->reference_id);
            char *expression = call->desc == &NODE_BehaviorExpression
                                 ? graph_expression_display(asset, call)
                                 : NULL;
            if (expression || (target_graph && target_graph->name)) {
                yyjson_mut_obj_add_strcpy(
                    doc,
                    target,
                    "name",
                    expression ? expression : target_graph->name
                );
            }
            free(expression);
            resolved = target_graph != NULL;
        } else {
            yyjson_mut_obj_add_strcpy(doc, target, "kind", "event");
            yyjson_mut_obj_add_strcpy(doc, target, "name", call->reference_name);
        }
        yyjson_mut_obj_add_bool(doc, entry, "resolved", resolved);
    }
    free(nodes);
}

static void inspect_add_effects(yyjson_mut_doc *doc, yyjson_mut_val *object, const compile_report *report)
{
    const compile_effects *effects = compile_report_effects(report);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "effects");
    yyjson_mut_val *reads = yyjson_mut_obj_add_arr(doc, value, "reads");
    int read_count = acount(effects->reads);
    uint64_t *ordered_reads = read_count
                                ? xmalloc((size_t)read_count * sizeof *ordered_reads)
                                : NULL;
    for (int i = 0; i < read_count; i++) {
        ordered_reads[i] = effects->reads[i];
    }
    if (read_count > 1) {
        qsort(ordered_reads, (size_t)read_count, sizeof *ordered_reads, compare_uint64);
    }
    for (int i = 0; i < read_count; i++) {
        yyjson_mut_arr_add_uint(doc, reads, ordered_reads[i]);
    }
    free(ordered_reads);
    yyjson_mut_val *writes = yyjson_mut_obj_add_arr(doc, value, "writes");
    int write_count = acount(effects->writes);
    uint64_t *ordered_writes = write_count
                                 ? xmalloc((size_t)write_count * sizeof *ordered_writes)
                                 : NULL;
    for (int i = 0; i < write_count; i++) {
        ordered_writes[i] = effects->writes[i];
    }
    if (write_count > 1) {
        qsort(ordered_writes, (size_t)write_count, sizeof *ordered_writes, compare_uint64);
    }
    for (int i = 0; i < write_count; i++) {
        yyjson_mut_arr_add_uint(doc, writes, ordered_writes[i]);
    }
    free(ordered_writes);
    yyjson_mut_obj_add_uint(doc, value, "flags", effects->flags);
    yyjson_mut_obj_add_bool(doc, value, "all_writes", effects->all_writes != 0);
    yyjson_mut_obj_add_bool(doc, value, "parallel_eligible", effects->parallel_eligible != 0);
    yyjson_mut_obj_add_uint(doc, value, "blocking_node_id", effects->blocking_node_id);
    if (effects->blocking_reason) {
        yyjson_mut_obj_add_strcpy(doc, value, "blocking_reason", effects->blocking_reason);
    } else {
        yyjson_mut_obj_add_null(doc, value, "blocking_reason");
    }
    yyjson_mut_obj_add_uint(doc, value, "topology_revision", effects->topology_revision);
}

static void add_pin_texts(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    const char *key,
    const pin_spec *pins,
    int count,
    bool settings
);

/* Each node's pins as they are, "name: type", with the types wildcards
 * resolved to; the serialized nodes omit pins that match their template. */
static void inspect_add_pins(yyjson_mut_doc *doc, yyjson_mut_val *nodes, const graph *source)
{
    size_t index, maximum;
    yyjson_mut_val *item;
    yyjson_mut_arr_foreach(nodes, index, maximum, item)
    {
        uint64_t id = yyjson_mut_get_uint(yyjson_mut_obj_get(item, "id"));
        const node *n = graph_node_by_id(source, id);
        add_pin_texts(doc, item, "in", n->input_pins, n->input_count, true);
        add_pin_texts(doc, item, "out", n->output_pins, n->output_count, true);
        for (int output = 0; output <= 1; output++) {
            const pin_spec *pins = output ? n->output_pins : n->input_pins;
            int count = output ? n->output_count : n->input_count;
            yyjson_mut_val *labels = yyjson_mut_arr(doc);
            for (int pin = 0; pin < count; pin++) {
                if (!pins[pin].display_name || !pins[pin].display_name[0]) {
                    continue;
                }
                yyjson_mut_val *label = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, label, "id", pins[pin].id);
                yyjson_mut_obj_add_strcpy(
                    doc,
                    label,
                    "name",
                    pins[pin].name ? pins[pin].name : ""
                );
                yyjson_mut_obj_add_strcpy(doc, label, "display_name", pins[pin].display_name);
                yyjson_mut_arr_append(labels, label);
            }
            if (yyjson_mut_arr_size(labels)) {
                yyjson_mut_obj_add_val(doc, item, output ? "output_labels" : "input_labels", labels);
            }
        }
    }
}

static int inspect_source_presentation(
    yyjson_val *request,
    yyjson_mut_doc *doc,
    yyjson_mut_val *result,
    const authoring_source_presentation *presentation,
    char *error,
    size_t size
)
{
    const char *flags[] = { "include_layout", "include_pins", "include_uses" };
    for (int i = 0; i < 3; i++) {
        yyjson_val *value = yyjson_obj_get(request, flags[i]);
        if (value && !yyjson_is_bool(value)) {
            snprintf(error, size, "%s must be a boolean", flags[i]);
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
    }
    uint64_t id = 0;
    bool has_id = yyjson_obj_get(request, "graph_id") != NULL;
    if (has_id && !request_uint(request, "graph_id", &id)) {
        set_error(error, size, "graph_id must be an unsigned integer");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_val *name_value = yyjson_obj_get(request, "graph_name");
    if (name_value && !yyjson_is_str(name_value)) {
        set_error(error, size, "graph_name must be a string");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    const char *name = yyjson_get_str(name_value);
    bool layout = yyjson_get_bool(yyjson_obj_get(request, "include_layout"));
    yyjson_val *pins_value = yyjson_obj_get(request, "include_pins");
    bool pins = !pins_value || yyjson_get_bool(pins_value);
    const behavior_asset *asset = authoring_source_presentation_view(presentation);
    const graph *selected = NULL;
    yyjson_mut_val *summaries = yyjson_mut_obj_add_arr(doc, result, "graphs");
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, summaries);
        yyjson_mut_obj_add_uint(doc, item, "id", g->graph_id);
        yyjson_mut_obj_add_strcpy(doc, item, "name", g->name ? g->name : "");
        yyjson_mut_obj_add_strcpy(doc, item, "kind", graph_kind_name(g->kind));
        yyjson_mut_obj_add_int(doc, item, "node_count", acount(g->nodes));
        yyjson_mut_obj_add_int(doc, item, "edge_count", acount(g->edges));
        if (!selected && (!has_id || id == g->graph_id) &&
            (!name || !strcmp(name, g->name ? g->name : ""))) {
            selected = g;
        }
    }
    if (!selected && (has_id || name)) {
        set_error(error, size, "graph not found in canonical source presentation");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (!selected) {
        return CHIBI_AUTOMATION_OK;
    }
    /* Serialization only copies the authoring view; no graph is parsed or compiled. */
    yyjson_mut_doc *copy = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(copy, yyjson_mut_val_mut_copy(copy, yyjson_mut_obj_get(result, "document")));
    yyjson_doc *serialized = yyjson_mut_doc_imut_copy(copy, NULL);
    yyjson_val *graphs = yyjson_obj_get(yyjson_doc_get_root(serialized), "graphs");
    yyjson_val *source = NULL;
    size_t index, maximum;
    yyjson_val *item;
    yyjson_arr_foreach(graphs, index, maximum, item)
    {
        if (yyjson_get_uint(yyjson_obj_get(item, "id")) == selected->graph_id) {
            source = item;
            break;
        }
    }
    assert(source);
    yyjson_mut_val *output = yyjson_val_mut_copy(doc, source);
    yyjson_mut_obj_remove_key(output, "nodes");
    yyjson_mut_obj_remove_key(output, "edges");
    yyjson_mut_obj_add_uint(doc, output, "id", selected->graph_id);
    inspect_add_sorted_array(doc, output, "nodes", yyjson_obj_get(source, "nodes"), false, layout);
    inspect_add_sorted_array(doc, output, "edges", yyjson_obj_get(source, "edges"), true, layout);
    yyjson_mut_val *nodes = yyjson_mut_obj_get(output, "nodes");
    if (pins) {
        inspect_add_pins(doc, nodes, selected);
    }
    yyjson_mut_val *node_value;
    yyjson_mut_arr_foreach(nodes, index, maximum, node_value)
    {
        uint64_t node_id = yyjson_mut_get_uint(yyjson_mut_obj_get(node_value, "id"));
        for (int i = 0; i < authoring_source_presentation_origin_count(presentation);
             i++) {
            const source_presentation_origin
                *origin = authoring_source_presentation_origin_at(presentation, i);
            if (origin->graph == selected->graph_id &&
                origin->node == node_id && !origin->pin) {
                source_range_json(doc, node_value, origin->range);
                break;
            }
        }
    }
    yyjson_mut_obj_add_val(doc, result, "graph", output);
    yyjson_doc_free(serialized);
    yyjson_mut_doc_free(copy);
    return CHIBI_AUTOMATION_OK;
}

static const node *inspect_expression_caller(const behavior_asset *asset, const graph *generated)
{
    if (!generated->math_expression) {
        return NULL;
    }
    for (int g = 0; g < acount(asset->graphs); g++) {
        for (int i = 0; i < acount(asset->graphs[g]->nodes); i++) {
            const node *n = asset->graphs[g]->nodes[i];
            if (n->desc == &NODE_BehaviorExpression && !n->reference_asset &&
                n->reference_id == generated->graph_id) {
                return n;
            }
        }
    }
    return NULL;
}

static yyjson_mut_val *inspect_graph_value(
    yyjson_mut_doc *doc,
    const behavior_asset *asset,
    const graph *source,
    const compile_report *report,
    bool include_layout,
    bool include_pins
)
{
    av_doc *native = av_doc_new();
    av_value *serialized = graph_to_value(native, source);
    graph *arranged = include_layout ? graph_from_value(serialized) : NULL;
    if (arranged) {
        graph_place_unplaced(arranged);
        serialized = graph_to_value(native, arranged);
        graph_free(arranged);
        free(arranged);
    }
    yyjson_mut_doc *boundary = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(boundary, av_to_json(boundary, serialized));
    yyjson_doc *document = yyjson_mut_doc_imut_copy(boundary, NULL);
    yyjson_mut_doc_free(boundary);
    av_doc_free(native);
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, object, "id", source->graph_id);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", graph_kind_name(source->kind));
    const node *caller = inspect_expression_caller(asset, source);
    char *formula = caller ? graph_expression_display(asset, caller) : NULL;
    yyjson_mut_obj_add_strcpy(
        doc,
        object,
        "name",
        formula ? formula : (source->name ? source->name : "")
    );
    yyjson_mut_obj_add_uint(doc, object, "entry_id", source->entry_id);
    yyjson_mut_obj_add_uint(doc, object, "return_id", source->return_id);
    yyjson_mut_obj_add_uint(doc, object, "revision", source->topology_revision);
    yyjson_mut_obj_add_bool(doc, object, "layout_included", include_layout);
    yyjson_mut_obj_add_bool(doc, object, "breakpoints_enabled", source->breakpoints_enabled != 0);
    yyjson_mut_obj_add_bool(doc, object, "dataflow", source->dataflow != 0);
    yyjson_mut_obj_add_int(doc, object, "call_depth_limit", source->call_depth_limit);
    if (source->source_asset) {
        yyjson_mut_obj_add_strcpy(doc, object, "source_asset", source->source_asset);
        yyjson_mut_obj_add_uint(doc, object, "source_graph_id", source->source_graph_id);
    }
    if (source->math_expression) {
        yyjson_mut_obj_add_strcpy(
            doc,
            object,
            "math_expression",
            formula ? formula : source->math_expression
        );
        yyjson_mut_obj_add_int(doc, object, "expression_mode", source->expression_mode);
    }
    yyjson_val *serialized_root = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *description = yyjson_obj_get(serialized_root, "description");
    if (description) {
        yyjson_mut_obj_add_val(doc, object, "description", yyjson_val_mut_copy(doc, description));
    }
    if (include_layout) {
        yyjson_val *comment_boxes = yyjson_obj_get(serialized_root, "comment_boxes");
        if (comment_boxes) {
            yyjson_mut_obj_add_val(doc, object, "comment_boxes", yyjson_val_mut_copy(doc, comment_boxes));
        }
    }
    inspect_add_sorted_array(doc, object, "nodes", yyjson_obj_get(serialized_root, "nodes"), false, include_layout);
    if (include_pins) {
        inspect_add_pins(doc, yyjson_mut_obj_get(object, "nodes"), source);
    }
    yyjson_mut_val *inspected_nodes = yyjson_mut_obj_get(object, "nodes");
    size_t node_index, node_maximum;
    yyjson_mut_val *item;
    yyjson_mut_arr_foreach(inspected_nodes, node_index, node_maximum, item)
    {
        uint64_t id = yyjson_mut_get_uint(yyjson_mut_obj_get(item, "id"));
        const node *n = graph_node_by_id(source, id);
        if (n->desc == &NODE_BehaviorExpression) {
            char *expression = graph_expression_display(asset, n);
            if (expression) {
                yyjson_mut_obj_add_strcpy(doc, item, "expression", expression);
                yyjson_mut_obj_remove_key(item, "reference_name");
                yyjson_mut_obj_add_strcpy(doc, item, "reference_name", expression);
                free(expression);
            }
            if (include_pins) {
                yyjson_mut_val *labels = yyjson_mut_arr(doc);
                yyjson_mut_val *inputs = yyjson_mut_arr(doc);
                for (int input = 0; input < n->input_count; input++) {
                    const pin_spec *pin = &n->input_pins[input];
                    char name[80];
                    if (pin->kind == PIN_DATA) {
                        graph_expression_input_name(asset, n, input, name, sizeof name);
                        yyjson_mut_val *label = yyjson_mut_obj(doc);
                        yyjson_mut_obj_add_uint(doc, label, "id", pin->id);
                        yyjson_mut_obj_add_strcpy(doc, label, "name", name);
                        yyjson_mut_obj_add_strcpy(doc, label, "display_name", name);
                        const char *value = graph_expression_input_label(asset, n, input);
                        if (value && strncmp(value, "_code_input_", 12)) {
                            yyjson_mut_obj_add_strcpy(doc, label, "source", value);
                        }
                        yyjson_mut_arr_append(labels, label);
                    } else {
                        snprintf(name, sizeof name, "%s", pin->name);
                    }
                    char text[160];
                    snprintf(
                        text,
                        sizeof text,
                        "%s: %s",
                        name,
                        pin->kind == PIN_DATA
                            ? (pin->type_name ? pin->type_name : pin_type_name(pin->type))
                            : "exec"
                    );
                    yyjson_mut_arr_append(inputs, yyjson_mut_strcpy(doc, text));
                }
                yyjson_mut_obj_remove_key(item, "in");
                yyjson_mut_obj_add_val(doc, item, "in", inputs);
                yyjson_mut_obj_remove_key(item, "input_labels");
                yyjson_mut_obj_add_val(doc, item, "input_labels", labels);
            }
        }
    }
    inspect_add_sorted_array(doc, object, "edges", yyjson_obj_get(serialized_root, "edges"), true, include_layout);
    inspect_add_calls(doc, object, asset, source, report);
    if (document) {
        yyjson_doc_free(document);
    }
    free(formula);
    return object;
}

static void inspect_add_variables(yyjson_mut_doc *doc, yyjson_mut_val *object, yyjson_val *source)
{
    yyjson_mut_val *variables = yyjson_mut_obj_add_arr(doc, object, "variables");
    if (!yyjson_is_arr(source)) {
        return;
    }
    int count = (int)yyjson_arr_size(source);
    inspect_json_item *items = count ? xmalloc((size_t)count * sizeof *items) : NULL;
    size_t index, maximum;
    yyjson_val *item;
    int item_count = 0;
    yyjson_arr_foreach(source, index, maximum, item)
    {
        items[item_count++] = (inspect_json_item){
            .value = item,
            .id = inspect_json_uint(yyjson_obj_get(item, "id")),
        };
    }
    if (item_count > 1) {
        qsort(items, (size_t)item_count, sizeof *items, inspect_item_compare);
    }
    for (int i = 0; i < item_count; i++) {
        yyjson_mut_arr_add_val(variables, yyjson_val_mut_copy(doc, items[i].value));
    }
    free(items);
}

static void inspect_add_validation(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const behavior_asset *asset,
    const compile_report *report
)
{
    yyjson_mut_val *validation = yyjson_mut_obj_add_obj(doc, object, "validation");
    yyjson_mut_obj_add_bool(doc, validation, "valid", report->error_count == 0);
    yyjson_mut_obj_add_int(doc, validation, "errors", report->error_count);
    yyjson_mut_obj_add_int(doc, validation, "warnings", report->warning_count);
    yyjson_mut_obj_add_int(doc, validation, "notes", report->note_count);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(doc, validation, "diagnostics");
    for (int i = 0; i < acount(report->items); i++) {
        add_diagnostic(doc, diagnostics, asset, &report->items[i]);
    }
    inspect_add_effects(doc, validation, report);
}

static behavior_asset *link_project_libraries(
    const char *root,
    const behavior_asset *asset,
    char *error,
    size_t error_size
);

static int compile_project_graph(const char *, const behavior_asset *, compile_report *);
static int load_model_catalog(const char *, behavior_asset ***, char *, size_t);

static void model_parameter_values(
    yyjson_mut_doc *doc,
    yyjson_mut_val *entry,
    const char *key,
    const float *values,
    int width
)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, entry, key);
    for (int i = 0; i < width; i++) {
        yyjson_mut_arr_add_real(doc, array, values[i]);
    }
}

static void inspect_model_parameters(
    const char *root,
    const behavior_asset *asset,
    const behavior_asset *authored,
    yyjson_mut_doc *doc,
    yyjson_mut_val *value
)
{
    if (asset->domain != BEHAVIOR_DOMAIN_MODEL) {
        return;
    }
    if (authored->reference.image) {
        const model_reference *reference = &authored->reference;
        yyjson_mut_val *entry = yyjson_mut_obj_add_obj(doc, value, "model_reference");
        yyjson_mut_obj_add_strcpy(doc, entry, "image", reference->image);
        model_parameter_values(doc, entry, "center", reference->center, 2);
        yyjson_mut_obj_add_real(doc, entry, "span", reference->span);
        yyjson_mut_obj_add_real(doc, entry, "angle", reference->angle);
        yyjson_mut_obj_add_bool(doc, entry, "locked", reference->locked);
    }
    behavior_asset **models = NULL;
    char message[256] = { 0 };
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    bool valid = load_model_catalog(root, &models, message, sizeof message) ==
                 CHIBI_AUTOMATION_OK;
    double compile_ms = 0;
    if (valid) {
        struct timespec start, end;
        int status = clock_gettime(CLOCK_MONOTONIC, &start);
        assert(!status);
        valid = model_compile_with_catalog(
            asset,
            (const behavior_asset *const *)models,
            acount(models),
            &program,
            &error
        );
        status = clock_gettime(CLOCK_MONOTONIC, &end);
        assert(!status);
        (void)status;
        compile_ms = (double)(end.tv_sec - start.tv_sec) * 1000 +
                     (double)(end.tv_nsec - start.tv_nsec) / 1000000;
    }
    yyjson_mut_val *compilation = yyjson_mut_obj_add_obj(doc, value, "model_compilation");
    yyjson_mut_obj_add_bool(doc, compilation, "valid", valid);
    int source_nodes = 0;
    for (int i = 0; i < acount(authored->graphs); i++) {
        source_nodes += acount(authored->graphs[i]->nodes);
    }
    yyjson_mut_obj_add_int(doc, compilation, "source_node_count", source_nodes);
    yyjson_mut_obj_add_real(doc, compilation, "compile_ms", compile_ms);
    if (!valid) {
        yyjson_mut_obj_add_uint(doc, compilation, "diagnostic_node", error.node);
        yyjson_mut_obj_add_strcpy(
            doc,
            compilation,
            "diagnostic",
            message[0] ? message : error.message
        );
    }
    model_inspection inspection = { 0 };
    if (valid) {
        model_program_inspect(&program, &inspection);
        yyjson_mut_val *images = yyjson_mut_obj_add_arr(doc, compilation, "image_dependencies");
        for (int i = 0; i < acount(program.shader.images); i++) {
            yyjson_mut_arr_add_strcpy(doc, images, program.shader.images[i].key);
        }
        int public_slots = inspection.public_slots;
        int private_slots = inspection.private_slots;
        yyjson_mut_obj_add_int(
            doc,
            compilation,
            "expanded_shader_node_count",
            acount(program.shader.nodes)
        );
        yyjson_mut_obj_add_int(doc, compilation, "public_parameter_slots", public_slots);
        yyjson_mut_obj_add_int(doc, compilation, "private_parameter_slots", private_slots);
        yyjson_mut_obj_add_int(doc, compilation, "part_count", acount(program.parts));
        yyjson_mut_obj_add_int(
            doc,
            compilation,
            "hit_shader_node_count",
            acount(program.hit_shader.nodes)
        );
        yyjson_mut_obj_add_int(
            doc,
            compilation,
            "material_slot_count",
            acount(program.material_slots)
        );
        yyjson_mut_val *budgets = yyjson_mut_obj_add_obj(doc, compilation, "budgets");
        yyjson_mut_obj_add_int(doc, budgets, "distance_nodes", inspection.distance_limit);
        yyjson_mut_obj_add_int(doc, budgets, "hit_nodes", inspection.hit_limit);
        yyjson_mut_obj_add_int(doc, budgets, "uniform_slots", inspection.parameter_limit);
        yyjson_mut_obj_add_int(doc, budgets, "material_slots", inspection.material_limit);
        yyjson_mut_obj_add_str(doc, compilation, "part_cost_semantics", "inclusive overlapping reachable distance IR nodes; counts are not additive");
        yyjson_mut_val *dependencies = yyjson_mut_obj_add_arr(doc, compilation, "dependencies");
        for (int i = 0; i < acount(program.dependencies); i++) {
            yyjson_mut_arr_add_strcpy(doc, dependencies, program.dependencies[i]);
        }
        yyjson_mut_val *materials = yyjson_mut_obj_add_arr(doc, compilation, "materials");
        for (int i = 0; i < acount(program.material_slots); i++) {
            const model_material_slot *slot = &program.material_slots[i];
            const behavior_asset *material = NULL;
            for (int j = 0; j < acount(models); j++) {
                if (models[j]->domain == BEHAVIOR_DOMAIN_MATERIAL &&
                    !strcmp(models[j]->name, slot->material)) {
                    material = models[j];
                    break;
                }
            }
            yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, materials);
            yyjson_mut_obj_add_strcpy(doc, entry, "slot", slot->name);
            yyjson_mut_obj_add_strcpy(doc, entry, "material", slot->material);
            shader_graph graph;
            shader_graph_init(&graph);
            bool material_valid = material &&
                                  shader_graph_from_graph(material->graph, &graph, &error);
            yyjson_mut_obj_add_bool(doc, entry, "valid", material_valid);
            if (material_valid) {
                shader_inspection resource = shader_graph_inspect(&graph);
                yyjson_mut_obj_add_int(doc, entry, "ir_nodes", resource.nodes);
                yyjson_mut_obj_add_int(doc, entry, "uniform_slots", resource.uniforms);
                yyjson_mut_obj_add_int(doc, entry, "image_variants", resource.images);
                yyjson_mut_obj_add_int(doc, entry, "ramp_vec4s", resource.ramp_vec4s);
                yyjson_mut_obj_add_int(
                    doc,
                    entry,
                    "static_texture_sample_bound",
                    resource.texture_samples
                );
                yyjson_mut_obj_add_int(
                    doc,
                    entry,
                    "detail_helper_evaluations",
                    resource.detail_expressions
                );
                yyjson_mut_obj_add_str(doc, entry, "sample_cost_semantics", "conservative static call bound per shading evaluation; not measured GPU work");
            } else {
                yyjson_mut_obj_add_strcpy(
                    doc,
                    entry,
                    "diagnostic",
                    material ? error.message : "missing material source"
                );
            }
            shader_graph_free(&graph);
        }
        yyjson_mut_val *anchors = yyjson_mut_obj_add_arr(doc, compilation, "anchors");
        float prepared[SHADER_PARAMETER_MAX][4];
        model_bounds prepared_bounds;
        bool prepared_ok = model_program_prepare(&program, NULL, prepared, &prepared_bounds, &error);
        for (int i = 0; i < acount(program.anchors); i++) {
            const model_anchor *anchor = &program.anchors[i];
            yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, anchors);
            yyjson_mut_obj_add_uint(doc, entry, "node", anchor->node);
            yyjson_mut_obj_add_strcpy(doc, entry, "asset", anchor->asset);
            yyjson_mut_obj_add_strcpy(doc, entry, "name", anchor->name);
            yyjson_mut_obj_add_strcpy(doc, entry, "scope", anchor->scope);
            float position[3], angles[3];
            if (prepared_ok &&
                model_program_anchor(&program, i, prepared, position, angles, &error)) {
                model_parameter_values(doc, entry, "position", position, 3);
                model_parameter_values(doc, entry, "angles", angles, 3);
            }
        }
        yyjson_mut_val *parts = yyjson_mut_obj_add_arr(doc, compilation, "parts");
        for (int i = 0; i < acount(program.parts); i++) {
            const model_part *part = &program.parts[i];
            yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, parts);
            yyjson_mut_obj_add_uint(doc, entry, "node", part->node);
            yyjson_mut_obj_add_strcpy(doc, entry, "asset", part->asset);
            yyjson_mut_obj_add_strcpy(doc, entry, "name", part->name);
            yyjson_mut_obj_add_strcpy(doc, entry, "path", part->path);
            yyjson_mut_obj_add_int(doc, entry, "material_slot", part->material);
            yyjson_mut_obj_add_int(
                doc,
                entry,
                "inclusive_distance_nodes",
                inspection.parts[i].distance_nodes
            );
        }
        yyjson_mut_val *bounds = yyjson_mut_obj_add_obj(doc, compilation, "bounds");
        yyjson_mut_obj_add_bool(doc, bounds, "empty", program.bounds.empty);
        model_parameter_values(doc, bounds, "minimum", program.bounds.min, 3);
        model_parameter_values(doc, bounds, "maximum", program.bounds.max, 3);
    }
    yyjson_mut_val *slots = yyjson_mut_obj_add_arr(doc, value, "model_material_slots");
    if (valid) {
        for (int i = 0; i < acount(program.material_slots); i++) {
            const model_material_slot *slot = &program.material_slots[i];
            yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, slots);
            yyjson_mut_obj_add_uint(doc, entry, "id", slot->node);
            yyjson_mut_obj_add_strcpy(doc, entry, "asset", slot->asset);
            yyjson_mut_obj_add_strcpy(doc, entry, "name", slot->name);
            yyjson_mut_obj_add_strcpy(doc, entry, "scope", slot->scope);
            yyjson_mut_obj_add_strcpy(doc, entry, "material", slot->material);
            yyjson_mut_obj_add_int(doc, entry, "slot", slot->slot);
        }
        yyjson_mut_val *dependencies = yyjson_mut_obj_add_arr(doc, compilation, "material_dependencies");
        for (int i = 0; i < acount(program.material_dependencies); i++) {
            yyjson_mut_arr_add_strcpy(doc, dependencies, program.material_dependencies[i]);
        }
    } else {
        for (int g = 0; g < acount(asset->graphs); g++) {
            for (int i = 0; i < acount(asset->graphs[g]->nodes); i++) {
                const node *n = asset->graphs[g]->nodes[i];
                if (strcmp(n->desc->name, "Model Material Slot")) {
                    continue;
                }
                model_material_decl slot;
                if (!model_material_get(n, &slot, &error)) {
                    continue;
                }
                yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, slots);
                yyjson_mut_obj_add_uint(doc, entry, "id", slot.id);
                yyjson_mut_obj_add_strcpy(doc, entry, "asset", asset->name);
                yyjson_mut_obj_add_strcpy(doc, entry, "name", slot.name);
                yyjson_mut_obj_add_strcpy(doc, entry, "scope", "");
                yyjson_mut_obj_add_strcpy(doc, entry, "material", slot.material);
                yyjson_mut_obj_add_int(doc, entry, "slot", -1);
            }
        }
    }
    yyjson_mut_val *parameters = yyjson_mut_obj_add_arr(doc, value, "model_parameters");
    if (valid) {
        for (int i = 0; i < acount(program.parameters); i++) {
            const model_parameter *parameter = &program.parameters[i];
            if (parameter->hidden) {
                continue;
            }
            yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, parameters);
            yyjson_mut_obj_add_uint(doc, entry, "id", parameter->node);
            yyjson_mut_obj_add_strcpy(doc, entry, "asset", parameter->asset);
            yyjson_mut_obj_add_strcpy(doc, entry, "scope", parameter->scope);
            yyjson_mut_obj_add_strcpy(doc, entry, "name", parameter->name);
            yyjson_mut_obj_add_strcpy(
                doc,
                entry,
                "type",
                pin_json_name(parameter->type)
            );
            if (parameter->type_name) {
                yyjson_mut_obj_add_strcpy(doc, entry, "type_name", parameter->type_name);
            }
            yyjson_mut_obj_add_int(doc, entry, "width", parameter->width);
            yyjson_mut_obj_add_int(doc, entry, "slot", parameter->slot);
            yyjson_mut_obj_add_bool(doc, entry, "runtime", true);
            model_parameter_values(
                doc,
                entry,
                "default",
                parameter->defaults,
                parameter->width
            );
            model_parameter_values(
                doc,
                entry,
                "minimum",
                parameter->minimum,
                parameter->width
            );
            model_parameter_values(
                doc,
                entry,
                "maximum",
                parameter->maximum,
                parameter->width
            );
        }
    }
    for (int g = 0; g < acount(asset->graphs); g++) {
        for (int n = 0; n < acount(asset->graphs[g]->nodes); n++) {
            const node *node = asset->graphs[g]->nodes[n];
            if (node->desc != &NODE_ModelParameter) {
                continue;
            }
            bool emitted = false;
            for (int i = 0; i < acount(program.parameters); i++) {
                const model_parameter *parameter = &program.parameters[i];
                emitted |= !parameter->hidden && parameter->node == node->id &&
                           !strcmp(parameter->asset, asset->name);
            }
            model_parameter_decl declaration;
            if (emitted || !model_parameter_get(node, &declaration, &error)) {
                continue;
            }
            yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, parameters);
            yyjson_mut_obj_add_uint(doc, entry, "id", declaration.id);
            yyjson_mut_obj_add_strcpy(doc, entry, "asset", asset->name);
            yyjson_mut_obj_add_strcpy(doc, entry, "name", declaration.name);
            yyjson_mut_obj_add_strcpy(
                doc,
                entry,
                "type",
                pin_json_name(declaration.type)
            );
            if (declaration.type_name) {
                yyjson_mut_obj_add_strcpy(doc, entry, "type_name", declaration.type_name);
            }
            yyjson_mut_obj_add_int(doc, entry, "width", declaration.width);
            yyjson_mut_obj_add_bool(doc, entry, "runtime", false);
            model_parameter_values(
                doc,
                entry,
                "default",
                declaration.defaults,
                declaration.width
            );
            model_parameter_values(
                doc,
                entry,
                "minimum",
                declaration.minimum,
                declaration.width
            );
            model_parameter_values(
                doc,
                entry,
                "maximum",
                declaration.maximum,
                declaration.width
            );
        }
    }
    model_inspection_free(&inspection);
    model_program_free(&program);
    free_behavior_assets(models);
}

static int handle_import_sky(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *source = request_string(request, "source");
    const char *destination = request_string(request, "destination");
    if (!source || (yyjson_obj_get(request, "destination") && !destination)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "Invalid import_sky source or destination.");
        return -1;
    }
    chibi_project project = { 0 };
    char error[512] = { 0 };
    char *key = open_authored_project(&project, root, error, sizeof error) == 0
                  ? image_prepare_sky_import(project.content, source, destination, error, sizeof error)
                  : NULL;
    chibi_project_free(&project);
    if (!key) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "path", key);
    free(key);
    return 0;
}

/* The source file's name without its folders or extension. */
static void file_stem(const char *path, char *stem, size_t size)
{
    const char *name = path;
    for (const char *c = path; *c; c++) {
        name = *c == '/' || *c == '\\' ? c + 1 : name;
    }
    const char *dot = strrchr(name, '.');
    int length = dot && dot > name ? (int)(dot - name) : (int)strlen(name);
    snprintf(stem, size, "%.*s", length, name);
}

static int handle_import_model(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *source = request_string(request, "source");
    const char *name = request_string(request, "name");
    if (!source || (yyjson_obj_get(request, "name") && !name)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "Invalid import_model source or name.");
        return -1;
    }
    char stem[256], error[512] = { 0 };
    if (!name) {
        file_stem(source, stem, sizeof stem);
        name = stem;
    }
    char reason[128];
    if (!new_asset_name_valid(name, reason, sizeof reason)) {
        snprintf(error, sizeof error, "Name the import with name; '%s' is not an asset name: %s", name, reason);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, error);
        return -1;
    }
    /* A prototype's name is the one it declares, which a file named
     * otherwise may already hold. */
    asset_ref ref;
    char *text = NULL;
    int found = load_named_asset(root, family_by_kind("prototype"), name, &ref, &text, NULL, NULL, error, sizeof error);
    free(text);
    if (found == CHIBI_AUTOMATION_OK) {
        snprintf(error, sizeof error, "%s already declares the prototype '%s'", ref.relative, name);
    }
    if (found != CHIBI_AUTOMATION_NOT_FOUND) {
        response_error(result, exit_code, found == CHIBI_AUTOMATION_OK ? CHIBI_AUTOMATION_CONFLICT : found, error);
        return -1;
    }
    chibi_project project = { 0 };
    model_import_report report = { 0 };
    bool imported = open_authored_project(&project, root, error, sizeof error) == 0 &&
                    model_import(project.content, source, name, &report, error, sizeof error);
    chibi_project_free(&project);
    if (!imported) {
        model_import_report_free(&report);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "prototype", name);
    yyjson_mut_val *files = yyjson_mut_obj_add_arr(result->doc, value, "files");
    for (int i = 0; i < acount(report.written); i++) {
        yyjson_mut_arr_add_strcpy(result->doc, files, report.written[i]);
    }
    yyjson_mut_val *warnings = yyjson_mut_obj_add_arr(result->doc, value, "warnings");
    for (int i = 0; i < acount(report.warnings); i++) {
        yyjson_mut_arr_add_strcpy(result->doc, warnings, report.warnings[i]);
    }
    model_import_report_free(&report);
    return 0;
}

static int handle_bake_model(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *name = request_string(request, "model");
    const char *destination = request_string(request, "destination");
    model_export_options options = { .bake = model_bake_defaults() };
    yyjson_val *settings = yyjson_obj_get(request, "options");
    bool valid = name && destination && (!settings || yyjson_is_obj(settings));
    const char *keys[] = { "cell_size",   "max_axis_cells", "max_triangles",
                           "max_samples", "max_work",       "edge_iterations" };
    for (int i = 0; valid && i < 6; i++) {
        yyjson_val *v = settings ? yyjson_obj_get(settings, keys[i]) : NULL;
        if (!v) {
            continue;
        }
        double n = yyjson_get_num(v);
        valid = yyjson_is_num(v) && isfinite(n) && n >= 0 &&
                (i == 0 ? n <= FLT_MAX
                        : floor(n) == n && n <= (i == 3 || i == 4 ? 9007199254740991.0
                                                                  : INT_MAX));
        if (!valid) {
            break;
        }
        switch (i) {
            case 0: options.bake.cell_size = (float)n; break;
            case 1: options.bake.max_axis_cells = (int)n; break;
            case 2: options.bake.max_triangles = (int)n; break;
            case 3: options.bake.max_samples = (uint64_t)n; break;
            case 4: options.bake.max_work = (uint64_t)n; break;
            case 5: options.bake.edge_iterations = (int)n; break;
        }
    }
    model_export_override overrides[SHADER_PARAMETER_MAX] = { 0 };
    yyjson_val *array = yyjson_obj_get(request, "overrides");
    valid &= !array || (yyjson_is_arr(array) &&
                        yyjson_arr_size(array) <= SHADER_PARAMETER_MAX);
    for (int i = 0; valid && array && i < (int)yyjson_arr_size(array); i++) {
        yyjson_val *entry = yyjson_arr_get(array, (size_t)i);
        yyjson_val *id = yyjson_obj_get(entry, "parameter_id");
        yyjson_val *value = yyjson_obj_get(entry, "value");
        overrides[i].asset = request_string(entry, "asset");
        overrides[i].scope = request_string(entry, "scope");
        valid = yyjson_is_obj(entry) && overrides[i].asset &&
                overrides[i].scope && yyjson_is_uint(id) && yyjson_is_arr(value) &&
                yyjson_arr_size(value) >= 1 && yyjson_arr_size(value) <= 4;
        if (!valid) {
            break;
        }
        overrides[i].parameter_id = yyjson_get_uint(id);
        overrides[i].width = (int)yyjson_arr_size(value);
        for (int j = 0; valid && j < overrides[i].width; j++) {
            yyjson_val *v = yyjson_arr_get(value, (size_t)j);
            valid = yyjson_is_num(v) && isfinite(yyjson_get_num(v)) &&
                    fabs(yyjson_get_num(v)) <= FLT_MAX;
            overrides[i].value[j] = (float)yyjson_get_num(v);
        }
    }
    if (!valid) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "Invalid bake_model model, destination, options or parameter overrides.");
        return -1;
    }
    options.overrides = overrides;
    options.override_count = array ? (int)yyjson_arr_size(array) : 0;
    model_export_summary summary = { 0 };
    char error[512] = { 0 };
    bool ok = model_export_bundle(
        chibi_automation_content(root),
        name,
        destination,
        &options,
        &summary,
        error,
        sizeof error
    );
    if (!ok) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "destination", destination);
    yyjson_mut_obj_add_int(result->doc, value, "triangles", summary.triangles);
    yyjson_mut_obj_add_uint(result->doc, value, "samples", summary.samples);
    yyjson_mut_obj_add_uint(result->doc, value, "work", summary.work);
    yyjson_mut_obj_add_real(result->doc, value, "cell_diagonal", summary.cell_diagonal);
    yyjson_mut_obj_add_real(result->doc, value, "max_sampled_residual", summary.max_residual);
    yyjson_mut_obj_add_str(result->doc, value, "appearance", "Live material references retained; OBJ appearance is not baked.");
    return 0;
}

/* Executable inspection consumes canonical facts and an optional presentation;
 * presentation nodes never enter the native graph compiler. */
static int inspect_source_asset(
    const char *root,
    yyjson_val *request,
    response *result,
    int *exit_code,
    const asset_ref *ref,
    const char *text,
    size_t bytes,
    uint64_t hash
)
{
    yyjson_mut_doc *bound = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *input = yyjson_val_mut_copy(bound, request);
    yyjson_mut_doc_set_root(bound, input);
    yyjson_mut_obj_put(input, yyjson_mut_str(bound, "source"), yyjson_mut_strcpy(bound, text));
    yyjson_mut_obj_put(input, yyjson_mut_str(bound, "path"), yyjson_mut_strcpy(bound, ref->relative));
    yyjson_mut_obj_put(input, yyjson_mut_str(bound, "inspect_source_graph"), yyjson_mut_bool(bound, true));
    yyjson_doc *immutable = yyjson_mut_doc_imut_copy(bound, NULL);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, ref, bytes, hash);
    char error[256] = "";
    int status = analyze_project_source(
        root,
        yyjson_doc_get_root(immutable),
        true,
        result->doc,
        value,
        error,
        sizeof error
    );
    yyjson_doc_free(immutable);
    yyjson_mut_doc_free(bound);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    yyjson_mut_val *document = yyjson_mut_obj_get(value, "document");
    const char *fields[] = { "variables", "domain", "version" };
    for (int i = 0; i < 3; i++) {
        yyjson_mut_val *field = document ? yyjson_mut_obj_get(document, fields[i])
                                         : NULL;
        if (field) {
            yyjson_mut_obj_add_val(
                result->doc,
                value,
                fields[i],
                yyjson_mut_val_mut_copy(result->doc, field)
            );
        }
    }
    yyjson_mut_obj_remove_key(value, "document");
    if (yyjson_get_bool(yyjson_obj_get(request, "include_uses"))) {
        add_graph_use_counts(root, ref, result->doc, value);
    }
    yyjson_mut_val *valid = yyjson_mut_obj_get(value, "valid");
    if (!valid || !yyjson_mut_get_bool(valid)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "source inspection has diagnostics");
        return -1;
    }
    return 0;
}

static int handle_inspect_graph(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char *json = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    char error[256] = { 0 };
    yyjson_val *layout_value = yyjson_obj_get(request, "include_layout");
    if (layout_value && !yyjson_is_bool(layout_value)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "include_layout must be a boolean");
        return -1;
    }
    bool include_layout = layout_value && yyjson_get_bool(layout_value);
    yyjson_val *pins_value = yyjson_obj_get(request, "include_pins");
    if (pins_value && !yyjson_is_bool(pins_value)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "include_pins must be a boolean");
        return -1;
    }
    bool include_pins = !pins_value || yyjson_get_bool(pins_value);
    yyjson_val *uses_value = yyjson_obj_get(request, "include_uses");
    if (uses_value && !yyjson_is_bool(uses_value)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "include_uses must be a boolean");
        return -1;
    }
    int status = load_asset(root, request, &ref, &json, &bytes, &hash, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    if (ref.family->loader != ASSET_GRAPH) {
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "asset does not contain graphs");
        return -1;
    }
    if (!strcmp(ref.family->extension, CODE_ASSET_EXTENSION)) {
        int inspected = inspect_source_asset(root, request, result, exit_code, &ref, json, bytes, hash);
        free(json);
        return inspected;
    }
    yyjson_doc *source_document = parse_project_asset_path(root, ref.relative, json);
    behavior_asset *asset = behavior_asset_load(json);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_obj_add_bool(result->doc, value, "layout_included", include_layout);
    if (!source_document || !asset) {
        yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
        add_load_diagnostic(result->doc, diagnostics, source_document ? graph_last_error() : "asset source is invalid");
        yyjson_mut_obj_add_bool(result->doc, value, "valid", false);
        if (source_document) {
            yyjson_doc_free(source_document);
        }
        if (asset) {
            behavior_asset_free(asset);
        }
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "graph inspection failed");
        return -1;
    }
    uint64_t requested_id = 0;
    bool has_requested_id = yyjson_obj_get(request, "graph_id") != NULL;
    if (has_requested_id && !request_uint(request, "graph_id", &requested_id)) {
        yyjson_doc_free(source_document);
        behavior_asset_free(asset);
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "graph_id must be an unsigned integer");
        return -1;
    }
    const char *requested_name = request_string(request, "graph_name");
    int selected = -1;
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *graph_value = asset->graphs[i];
        if (has_requested_id && graph_value->graph_id != requested_id) {
            continue;
        }
        if (requested_name &&
            strcmp(graph_value->name ? graph_value->name : "", requested_name)) {
            continue;
        }
        selected = i;
        break;
    }
    if ((has_requested_id || requested_name) && selected < 0) {
        char message[512];
        int used = snprintf(message, sizeof message, "graph not found in this asset; its graphs:");
        for (int i = 0; i < acount(asset->graphs) && used < (int)sizeof message; i++) {
            const graph *g = asset->graphs[i];
            if (g->math_expression) {
                continue;
            }
            used += snprintf(
                message + used,
                sizeof message - (size_t)used,
                "%s %llu \"%s\"",
                i ? "," : "",
                (unsigned long long)g->graph_id,
                g->name ? g->name : ""
            );
        }
        yyjson_doc_free(source_document);
        behavior_asset_free(asset);
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, message);
        return -1;
    }
    compile_report report;
    compile_report_init(&report);
    /* Calls into libraries resolve as they will at runtime. */
    char link_error[256] = { 0 };
    behavior_asset *linked = link_project_libraries(root, asset, link_error, sizeof link_error);
    compile_project_graph(root, linked ? linked : asset, &report);
    inspect_model_parameters(root, linked ? linked : asset, asset, result->doc, value);
    behavior_asset_free(linked);
    inspect_add_variables(
        result->doc,
        value,
        yyjson_obj_get(yyjson_doc_get_root(source_document), "variables")
    );
    yyjson_mut_val *graphs = yyjson_mut_obj_add_arr(result->doc, value, "graphs");
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *source = asset->graphs[i];
        yyjson_mut_val *summary = yyjson_mut_arr_add_obj(result->doc, graphs);
        yyjson_mut_obj_add_uint(result->doc, summary, "id", source->graph_id);
        const node *caller = inspect_expression_caller(asset, source);
        char *formula = caller ? graph_expression_display(asset, caller) : NULL;
        yyjson_mut_obj_add_strcpy(
            result->doc,
            summary,
            "name",
            formula ? formula : (source->name ? source->name : "")
        );
        free(formula);
        yyjson_mut_obj_add_strcpy(
            result->doc,
            summary,
            "kind",
            graph_kind_name(source->kind)
        );
        yyjson_mut_obj_add_int(result->doc, summary, "node_count", acount(source->nodes));
        yyjson_mut_obj_add_int(result->doc, summary, "edge_count", acount(source->edges));
    }
    /* Counting reads the whole project, so it is asked for. */
    if (uses_value && yyjson_get_bool(uses_value)) {
        add_graph_use_counts(root, &ref, result->doc, value);
    }
    if (selected >= 0) {
        yyjson_mut_obj_add_val(
            result->doc,
            value,
            "graph",
            inspect_graph_value(result->doc, asset, asset->graphs[selected], &report, include_layout, include_pins)
        );
    }
    inspect_add_validation(result->doc, value, asset, &report);
    int errors = report.error_count;
    compile_report_free(&report);
    yyjson_doc_free(source_document);
    behavior_asset_free(asset);
    free(json);
    if (errors) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "graph validation failed");
        return -1;
    }
    return 0;
}

static int handle_inspect_system(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char *text = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    char error[256] = "";
    int status = load_asset(root, request, &ref, &text, &bytes, &hash, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    if (ref.family->loader != ASSET_SYSTEM) {
        free(text);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "asset is not a system");
        return -1;
    }
    int inspected = inspect_source_asset(root, request, result, exit_code, &ref, text, bytes, hash);
    free(text);
    return inspected;
}

static const char *execution_backend_label(behavior_backend backend)
{
    switch (backend) {
        case BEHAVIOR_SLJIT: return "sljit";
        case BEHAVIOR_WASM: return "wasm";
        case BEHAVIOR_BYTECODE: return "bytecode";
        case BEHAVIOR_ORACLE: return "interpreter";
        case BEHAVIOR_UNAVAILABLE: return "unavailable";
    }
    return "unavailable";
}

static void add_execution_status(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *backend,
    bool ready,
    bool jit_available,
    bool jit,
    bool fully_jit,
    const char *reason
)
{
    yyjson_mut_val *execution = yyjson_mut_obj_add_obj(doc, object, "execution");
    yyjson_mut_obj_add_strcpy(doc, execution, "backend", backend);
    yyjson_mut_obj_add_bool(doc, execution, "ready", ready);
    yyjson_mut_obj_add_bool(doc, execution, "jit_available", jit_available);
    yyjson_mut_obj_add_bool(doc, execution, "jit", jit);
    yyjson_mut_obj_add_bool(doc, execution, "fully_jit", fully_jit);
    yyjson_mut_obj_add_strcpy(doc, execution, "reason", reason ? reason : "");
}

typedef struct
{
    unsigned backends;
    int instances, events, native_events, wasm_events;
    bool ready;
} behavior_execution_status;

static void add_behavior_execution_status(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const behavior_execution_status *status,
    bool custom_systems
)
{
    bool native = (status->backends & (1u << BEHAVIOR_SLJIT)) != 0;
    bool wasm = (status->backends & (1u << BEHAVIOR_WASM)) != 0;
    bool full = status->ready && status->events > 0 &&
                status->native_events + status->wasm_events == status->events &&
                !custom_systems;
    const char *backend = !status->instances                         ? "none"
                        : status->backends == (1u << BEHAVIOR_SLJIT) ? "sljit"
                        : status->backends == (1u << BEHAVIOR_WASM)  ? "wasm"
                        : status->backends == (1u << BEHAVIOR_BYTECODE)
                            ? "bytecode"
                        : status->backends == (1u << BEHAVIOR_ORACLE)
                            ? "interpreter"
                        : status->backends == (1u << BEHAVIOR_UNAVAILABLE)
                            ? "unavailable"
                            : "mixed";
    const char *
        reason = !status->ready ? "behavior publication is unavailable"
               : custom_systems ? "live behavior backends are reported; custom "
                                  "System execution coverage is unverified"
               : !status->instances ? "no live behavior attachments"
               : full && !wasm
                   ? "all behavior event entries have published native bodies"
               : full
                   ? "all behavior event entries have published compiled bodies"
               : wasm   ? "generated WebAssembly bodies are published; "
                          "whole-event coverage is incomplete"
               : native ? "native behavior blocks are published; whole-event "
                          "coverage is incomplete"
               : status->backends == (1u << BEHAVIOR_BYTECODE)
                   ? "behavior instances execute compiled bytecode"
               : status->backends == (1u << BEHAVIOR_ORACLE)
                   ? "behavior instances explicitly use the interpreter oracle"
                   : "live behavior instances use different execution backends";
    add_execution_status(
        doc,
        object,
        backend,
        status->ready,
        behavior_backend_available(BEHAVIOR_SLJIT) ||
            behavior_backend_available(BEHAVIOR_WASM),
        native || wasm,
        full,
        reason
    );
    yyjson_mut_val *execution = yyjson_mut_obj_get(object, "execution");
    yyjson_mut_obj_add_str(doc, execution, "scope", "behavior_events");
    yyjson_mut_obj_add_int(doc, execution, "instances", status->instances);
    yyjson_mut_obj_add_int(doc, execution, "event_entries", status->events);
    yyjson_mut_obj_add_int(doc, execution, "native_event_entries", status->native_events);
    yyjson_mut_obj_add_int(doc, execution, "wasm_event_entries", status->wasm_events);
    yyjson_mut_obj_add_bool(doc, execution, "custom_systems_unverified", custom_systems);
}

static int collect_source_execution(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    preview_world *preview,
    behavior_execution_status *status
)
{
    const authoring_sources *sources = preview_sources_applied(preview);
    yyjson_mut_val *modules = yyjson_mut_obj_add_arr(doc, object, "source_modules");
    ecs_world *world = preview->runtime->ecs;
    int count = 0;
    for (int i = 0; sources && i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (source->analysis->kind != SCRIPT_PROGRAM_BEHAVIOR &&
            source->analysis->kind != SCRIPT_PROGRAM_SYSTEM) {
            continue;
        }
        execution_program *
            program = source_program(preview->runtime->sources, source->analysis->name);
        if (!program) {
            continue;
        }
        count++;
        int instances = 0;
        for (uint32_t slot = 1; slot <= ecs_world_high_water_mark(world); slot++) {
            ecs_entity entity = { slot, ecs_world_generation(world, slot) };
            instances += ecs_entity_is_alive(world, entity) &&
                         execution_program_instance(program, ecs_entity_to_u64(entity)) !=
                             NULL;
        }
        behavior_backend backend = execution_module_backend(source->module);
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, modules);
        yyjson_mut_obj_add_strcpy(doc, entry, "name", source->analysis->name);
        yyjson_mut_obj_add_uint(doc, entry, "id", execution_module_id(source->module));
        yyjson_mut_obj_add_str(doc, entry, "backend", execution_backend_label(backend));
        yyjson_mut_obj_add_int(doc, entry, "instances", instances);
        if (!instances) {
            continue;
        }
        status->instances += instances;
        status->backends |= 1u << backend;
        for (int f = 0; f < execution_module_function_count(source->module); f++) {
            execution_module_function_info
                function = execution_module_function_at(source->module, f);
            if (function.kind != EXECUTION_FUNCTION_HANDLER &&
                function.kind != EXECUTION_FUNCTION_SYSTEM) {
                continue;
            }
            status->events += instances;
            if (backend == BEHAVIOR_SLJIT && behavior_function_entry(function.body)) {
                status->native_events += instances;
            }
            if (backend == BEHAVIOR_WASM) {
                status->wasm_events += instances;
            }
        }
    }
    return count;
}

static int handle_execution_status(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *kind = request_string(request, "kind");
    const char *name = request_string(request, "name");
    if (!kind || !name || !name[0] ||
        (strcmp(kind, "behavior") && strcmp(kind, "system"))) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "execution_status requires kind behavior or system and a name");
        return -1;
    }
    asset_ref ref = { 0 };
    char *text = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    char error[256] = { 0 };
    int status = load_asset(root, request, &ref, &text, &bytes, &hash, error, sizeof error);
    free(text);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    if (strcmp(ref.family->kind, kind) ||
        strcmp(ref.family->extension, CODE_ASSET_EXTENSION)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "execution_status kind does not match an executable source asset");
        return -1;
    }
    authoring_language_catalog *native = source_native_catalog(root, error, sizeof error);
    if (!native) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    script_lower_bindings bindings = authoring_language_bindings(native);
    authoring_sources *sources = authoring_sources_prepare(
        chibi_automation_content(root),
        authoring_language_catalog_view(native),
        &bindings,
        behavior_default_backend()
    );
    const authoring_source *selected = NULL;
    char *path = content_path(root, ref.relative);
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (!strcmp(source->path, path)) {
            selected = source;
            break;
        }
    }
    free(path);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
    for (int i = 0; i < authoring_sources_catalog_diagnostic_count(sources); i++) {
        const source_catalog_diagnostic
            *diagnostic = authoring_sources_catalog_diagnostic_at(sources, i);
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(result->doc, diagnostics);
        yyjson_mut_obj_add_strcpy(result->doc, entry, "path", diagnostic->path);
        yyjson_mut_obj_add_strcpy(result->doc, entry, "message", diagnostic->message);
    }
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        for (int j = 0; j < source->diagnostic_count; j++) {
            const script_diagnostic *diagnostic = &source->diagnostics[j];
            yyjson_mut_val *entry = yyjson_mut_arr_add_obj(result->doc, diagnostics);
            yyjson_mut_obj_add_strcpy(result->doc, entry, "path", source->path);
            yyjson_mut_obj_add_strcpy(result->doc, entry, "message", diagnostic->message);
            yyjson_mut_obj_add_int(result->doc, entry, "line", diagnostic->range.line);
            yyjson_mut_obj_add_int(
                result->doc,
                entry,
                "column",
                diagnostic->range.column
            );
        }
    }
    bool ready = authoring_sources_valid(sources) && selected &&
                 selected->valid && selected->module;
    behavior_backend backend = ready ? execution_module_backend(selected->module)
                                     : BEHAVIOR_UNAVAILABLE;
    bool native_jit = backend == BEHAVIOR_SLJIT;
    bool wasm_jit = backend == BEHAVIOR_WASM;
    bool jit = native_jit || wasm_jit;
    add_execution_status(
        result->doc,
        value,
        execution_backend_label(backend),
        ready,
        behavior_backend_available(BEHAVIOR_SLJIT) ||
            behavior_backend_available(BEHAVIOR_WASM),
        jit,
        jit,
        ready ? "canonical source module published; runtime services are "
                "checked when registered"
              : "source preparation has diagnostics"
    );
    yyjson_mut_val *execution = yyjson_mut_obj_get(value, "execution");
    yyjson_mut_obj_add_bool(result->doc, value, "valid", ready);
    yyjson_mut_obj_add_bool(result->doc, execution, "bytecode", backend == BEHAVIOR_BYTECODE);
    yyjson_mut_obj_add_bool(result->doc, execution, "typed_bytecode", ready);
    int events = 0, native_events = 0, wasm_events = 0;
    for (int i = 0; ready && i < execution_module_function_count(selected->module);
         i++) {
        execution_module_function_info
            function = execution_module_function_at(selected->module, i);
        if (function.kind == EXECUTION_FUNCTION_HANDLER ||
            function.kind == EXECUTION_FUNCTION_SYSTEM) {
            events++;
            native_events += native_jit &&
                             behavior_function_entry(function.body) != NULL;
            wasm_events += wasm_jit && !behavior_function_interpreted(function.body);
        }
    }
    yyjson_mut_obj_add_int(result->doc, execution, "events", events);
    yyjson_mut_obj_add_int(result->doc, execution, "native_events", native_events);
    yyjson_mut_obj_add_int(result->doc, execution, "wasm_events", wasm_events);
    yyjson_mut_obj_add_bool(result->doc, execution, "wasm", wasm_jit);
    authoring_sources_free(sources);
    authoring_language_catalog_free(native);
    if (!ready) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "source execution preparation failed");
        return -1;
    }
    return 0;
}

static int handle_status(const char *root, response *result, int *exit_code)
{
    asset_entry *entries = NULL;
    char error[256] = { 0 };
    if (enumerate_assets(root, &entries, error, sizeof error) != 0) {
        response_error(result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "project_root", root);
    /* The folder as the manifest names it: relative when below the root. */
    const char *content = chibi_automation_content(root);
    size_t length = strlen(root);
    bool below = !strncmp(content, root, length) && content[length] == '/';
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_root", below ? content + length + 1 : content);
    yyjson_mut_obj_add_uint(result->doc, value, "assets", (uint64_t)acount(entries));
    yyjson_mut_val *families = yyjson_mut_obj_add_obj(result->doc, value, "families");
    for (int family_index = 0;
         family_index < (int)(sizeof ASSET_FAMILIES / sizeof *ASSET_FAMILIES);
         family_index++) {
        int count = 0;
        for (int i = 0; i < acount(entries); i++) {
            count += entries[i].family == &ASSET_FAMILIES[family_index];
        }
        yyjson_mut_obj_add_int(
            result->doc,
            families,
            ASSET_FAMILIES[family_index].kind,
            count
        );
    }
    yyjson_mut_val *registry = yyjson_mut_obj_add_obj(result->doc, value, "registry");
    add_registry(result->doc, registry);
    yyjson_mut_val *operations = yyjson_mut_obj_add_arr(result->doc, value, "operations");
    for (int i = 0;
         i < (int)(sizeof CONTRACT_OPERATIONS / sizeof *CONTRACT_OPERATIONS);
         i++) {
        yyjson_mut_arr_add_strcpy(
            result->doc,
            operations,
            CONTRACT_OPERATIONS[i].name
        );
    }
    free_asset_entries(entries);
    return 0;
}

static int handle_list_assets(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    page_options page;
    if (!request_page(request, &page, result, exit_code)) {
        return -1;
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t content_hash_value = 0;
    char error[256] = { 0 };
    int cataloged = catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error);
    if (cataloged != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, cataloged, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "project_root", root);
    char content_hash[32];
    snprintf(content_hash, sizeof content_hash, "%016llx", (unsigned long long)content_hash_value);
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_hash", content_hash);
    yyjson_mut_obj_add_bool(result->doc, value, "summary", page.summary);
    uint64_t total = (uint64_t)acount(assets);
    uint64_t start = page.cursor < total ? page.cursor : total;
    uint64_t count = total - start;
    if (page.has_limit && page.limit < count) {
        count = page.limit;
    }
    add_page_metadata(result->doc, value, acount(assets), start, count);
    yyjson_mut_val *asset_array = yyjson_mut_obj_add_arr(result->doc, value, "assets");
    for (uint64_t i = 0; i < count; i++) {
        add_asset_entry(result->doc, asset_array, &assets[start + i].asset);
    }
    return 0;
}

/* Reads the file `ref` names, and names it as asset_name does. */
static int read_asset_file(
    const char *root,
    asset_ref *ref,
    char **json,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
)
{
    int directory_status = verify_asset_directory(root, ref->family, error, error_size);
    if (directory_status < 0) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    char *path = content_path(root, ref->relative);
    struct stat info;
    if (!directory_status || lstat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        free(path);
        return set_errorf(error, error_size, "asset not found: %s", ref->relative),
               CHIBI_AUTOMATION_NOT_FOUND;
    }
    *json = read_file(path, bytes, hash, error, error_size);
    free(path);
    if (!*json) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    char *name = asset_name(ref->family, ref->filename, *json);
    /* The player refuses a longer name, and a shorter copy would name another. */
    bool fits = strlen(name) < sizeof ref->name;
    snprintf(ref->name, sizeof ref->name, "%s", fits ? name : "");
    free(name);
    if (!fits) {
        free(*json);
        *json = NULL;
        snprintf(error, error_size, "%s declares a name longer than %d bytes", ref->relative, PROTOTYPE_NAME_MAX);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
}

static int load_asset(
    const char *root,
    yyjson_val *request,
    asset_ref *ref,
    char **json,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
)
{
    /* Declared identities need not match their files. */
    const asset_family *family = family_by_kind(request_string(request, "kind"));
    if (!request_string(request, "path") && family &&
        (family->loader == ASSET_PROTOTYPE || family->loader == ASSET_RELATIONSHIP)) {
        const char *name = request_string(request, "name");
        return load_named_asset(root, family, name, ref, json, bytes, hash, error, error_size);
    }
    int ref_status = resolve_asset_ref(request, ref, error, error_size);
    if (ref_status != 0) {
        return ref_status == -2 ? CHIBI_AUTOMATION_NOT_FOUND
                                : CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    return read_asset_file(root, ref, json, bytes, hash, error, error_size);
}

/* Search all declared identities so a same-named file cannot hide a duplicate. */
static int find_declared_asset(
    const char *root,
    const char *name,
    asset_ref *ref,
    char **json,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
)
{
    const asset_family *family = ref->family;
    int found = verify_asset_directory(root, family, error, error_size);
    if (found <= 0) {
        if (!found) {
            snprintf(error, error_size, "no %s is named '%s'", family->kind, name);
        }
        return found < 0 ? CHIBI_AUTOMATION_IO_ERROR : CHIBI_AUTOMATION_NOT_FOUND;
    }
    const char *content = chibi_automation_content(root);
    size_t size = strlen(content) + strlen(family->folder) + 2;
    char *folder = xmalloc(size);
    snprintf(folder, size, "%s/%s", content, family->folder);
    DIR *directory = opendir(folder);
    free(folder);
    if (!directory) {
        set_errorf(error, error_size, "could not open %s", family->folder);
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    int status = CHIBI_AUTOMATION_NOT_FOUND;
    for (;;) {
        errno = 0; /* readdir's end and its failure both return NULL */
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno) {
                snprintf(error, error_size, "could not read %s: %s", family->folder, strerror(errno));
                free(*json);
                *json = NULL;
                status = CHIBI_AUTOMATION_IO_ERROR;
            }
            break;
        }
        asset_ref candidate = { .family = family };
        if (!valid_asset_filename(family, entry->d_name) ||
            strlen(entry->d_name) >= sizeof candidate.filename) {
            continue;
        }
        snprintf(candidate.filename, sizeof candidate.filename, "%s", entry->d_name);
        snprintf(
            candidate.relative,
            sizeof candidate.relative,
            "content/%s/%s",
            family->folder,
            entry->d_name
        );
        char *text = NULL, reason[256];
        size_t candidate_bytes;
        uint64_t candidate_hash;
        int loaded = read_asset_file(root, &candidate, &text, &candidate_bytes, &candidate_hash, reason, sizeof reason);
        if (loaded == CHIBI_AUTOMATION_NOT_FOUND ||
            (loaded == CHIBI_AUTOMATION_OK && strcmp(candidate.name, name))) {
            free(text);
            continue;
        }
        /* A file the search cannot read could declare the name too. */
        if (loaded != CHIBI_AUTOMATION_OK) {
            set_error(error, error_size, reason);
            free(*json);
            *json = NULL;
            status = loaded;
            break;
        }
        if (status == CHIBI_AUTOMATION_OK) {
            snprintf(
                error,
                error_size,
                "%s and %s both declare the %s '%s'",
                ref->relative,
                candidate.relative,
                family->kind,
                name
            );
            free(text);
            free(*json);
            *json = NULL;
            status = CHIBI_AUTOMATION_CONFLICT;
            break;
        }
        *ref = candidate;
        *json = text;
        if (bytes) {
            *bytes = candidate_bytes;
        }
        if (hash) {
            *hash = candidate_hash;
        }
        status = CHIBI_AUTOMATION_OK;
    }
    closedir(directory);
    if (status == CHIBI_AUTOMATION_NOT_FOUND) {
        snprintf(error, error_size, "no %s is named '%s'", family->kind, name);
    }
    return status;
}

/* Prototypes and relationships resolve by declaration, other assets by file. */
static int load_named_asset(
    const char *root,
    const asset_family *family,
    const char *name,
    asset_ref *ref,
    char **json,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
)
{
    if (!family || !name || !name[0]) {
        return set_error(error, error_size, "asset name is invalid"),
               CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    memset(ref, 0, sizeof *ref);
    ref->family = family;
    *json = NULL;
    bool declared = family->loader == ASSET_PROTOTYPE ||
                    family->loader == ASSET_RELATIONSHIP;
    if (declared) {
        return find_declared_asset(root, name, ref, json, bytes, hash, error, error_size);
    }
    const char *refused = strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")
                            ? "asset name is invalid"
                            : name_asset_file(ref, name);
    if (refused) {
        return set_error(error, error_size, refused), CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    snprintf(ref->relative, sizeof ref->relative, "content/%s/%s", family->folder, ref->filename);
    return read_asset_file(root, ref, json, bytes, hash, error, error_size);
}

int chibi_automation_format_assets(const char *root, bool check)
{
    char error[512] = { 0 };
    native_capabilities *
        table = authoring_application_catalog_new(studio_project_is(root), error, sizeof error);
    if (!table) {
        fprintf(stderr, "chibi_ctl --format: %s\n", error);
        return -1;
    }
    int differing = authoring_project_format(root, table, check);
    native_capabilities_free(table);
    return differing;
}

/* A ui asset as readAsset shows it: its markup, and its styles when it has
 * them. */
static yyjson_doc *ui_asset_document(const char *root, const asset_ref *ref, const char *rml)
{
    char *name = asset_name_from_filename(ref->filename);
    char *rcss = ui_styles(root, name);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, object);
    yyjson_mut_obj_add_strcpy(doc, object, "rml", rml);
    if (rcss) {
        yyjson_mut_obj_add_strcpy(doc, object, "rcss", rcss);
    }
    yyjson_doc *document = yyjson_mut_doc_imut_copy(doc, NULL);
    yyjson_mut_doc_free(doc);
    free(rcss);
    free(name);
    return document;
}

static int read_asset_complete(
    const char *root,
    const asset_ref *ref,
    const char *text,
    yyjson_val *request,
    response *result,
    yyjson_mut_val *value,
    char *error,
    size_t error_size
)
{
    uint64_t line, column;
    if (!yyjson_is_obj(request) || !request_uint(request, "line", &line) ||
        !request_uint(request, "column", &column) || !line || !column) {
        set_error(error, error_size, "complete requires positive line and column");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    size_t offset = 0, current_line = 1;
    while (text[offset] && current_line < line) {
        if (text[offset++] == '\n') {
            current_line++;
        }
    }
    size_t current_column = 1;
    while (text[offset] && text[offset] != '\n' && current_column < column) {
        offset++;
        current_column++;
    }
    if (current_line != line || current_column != column) {
        set_error(error, error_size, "completion position is outside asset source");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (source_program_family(ref->family)) {
        yyjson_mut_doc *candidate = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *request_value = yyjson_mut_obj(candidate);
        yyjson_mut_doc_set_root(candidate, request_value);
        yyjson_mut_obj_add_str(candidate, request_value, "source", text);
        yyjson_mut_obj_add_str(candidate, request_value, "path", ref->relative);
        yyjson_mut_obj_add_uint(candidate, request_value, "offset", offset);
        yyjson_doc *immutable = yyjson_mut_doc_imut_copy(candidate, NULL);
        yyjson_mut_val *analysis = yyjson_mut_obj(result->doc);
        int status = analyze_project_source(
            root,
            yyjson_doc_get_root(immutable),
            false,
            result->doc,
            analysis,
            error,
            error_size
        );
        const char *fields[] = { "valid",       "completions", "hover",
                                 "diagnostics", "definition",  "references",
                                 "calls",       "spans",       "semantic_spans" };
        for (int i = 0; i < (int)(sizeof fields / sizeof *fields); i++) {
            yyjson_mut_val *field = yyjson_mut_obj_get(analysis, fields[i]);
            if (field) {
                yyjson_mut_obj_add_val(
                    result->doc,
                    value,
                    fields[i],
                    yyjson_mut_val_mut_copy(result->doc, field)
                );
            }
        }
        yyjson_doc_free(immutable);
        yyjson_mut_doc_free(candidate);
        return status;
    }
    if (code_filename(ref->relative) && (ref->family->loader == ASSET_PROTOTYPE ||
                                         ref->family->loader == ASSET_RELATIONSHIP)) {
        authoring_language_catalog *native = source_native_catalog(root, error, error_size);
        if (!native) {
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        char *path = content_path(root, ref->relative);
        const source_catalog_edit edit = { .path = path, .text = text };
        source_catalog *project = source_catalog_edited(
            chibi_automation_content(root),
            authoring_language_catalog_view(native),
            &edit,
            1
        );
        script_syntax *syntax = script_parse(text);
        authoring_completions completions = { 0 };
        authoring_asset_source_complete(syntax, project, (int)offset, &completions);
        yyjson_mut_val *list = yyjson_mut_obj_add_arr(result->doc, value, "completions");
        for (int i = 0; i < completions.count; i++) {
            const authoring_completion *completion = &completions.items[i];
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(result->doc, list);
            yyjson_mut_obj_add_strcpy(result->doc, item, "label", completion->label);
            yyjson_mut_obj_add_strcpy(
                result->doc,
                item,
                "kind",
                completion->kind == AUTHORING_COMPLETE_FIELD ? "field" : "type"
            );
            yyjson_mut_obj_add_strcpy(result->doc, item, "detail", completion->detail);
            yyjson_mut_obj_add_strcpy(result->doc, item, "insert", completion->insert);
            yyjson_mut_obj_add_int(result->doc, item, "start", completions.range.start);
            yyjson_mut_obj_add_int(result->doc, item, "end", completions.range.end);
        }
        yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
        for (int i = 0; i < syntax->diagnostic_count; i++) {
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(result->doc, diagnostics);
            source_range_json(result->doc, item, syntax->diagnostics[i].range);
            yyjson_mut_obj_add_strcpy(
                result->doc,
                item,
                "message",
                syntax->diagnostics[i].message
            );
        }
        yyjson_mut_obj_add_bool(
            result->doc,
            value,
            "valid",
            !syntax->diagnostic_count && !source_catalog_diagnostic_count(project)
        );
        authoring_completions_free(&completions);
        script_syntax_free(syntax);
        source_catalog_free(project);
        authoring_language_catalog_free(native);
        free(path);
        return CHIBI_AUTOMATION_OK;
    }
    asset_entry *entries = NULL;
    if (enumerate_assets(root, &entries, error, error_size)) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    char **names = NULL;
    for (int i = 0; i < acount(entries); i++) {
        apush(names, xstrdup(entries[i].name));
    }
    asset_source *source = asset_source_parse(text);
    asset_source_completions completions = { 0 };
    asset_source_complete(
        source,
        (int)offset,
        ref->relative,
        (const char *const *)names,
        acount(names),
        &completions
    );
    yyjson_mut_val *list = yyjson_mut_obj_add_arr(result->doc, value, "completions");
    for (int i = 0; i < acount(completions.items); i++) {
        const asset_source_completion *completion = &completions.items[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(result->doc, list);
        yyjson_mut_obj_add_strcpy(result->doc, item, "label", completion->label);
        yyjson_mut_obj_add_strcpy(
            result->doc,
            item,
            "kind",
            code_completion_kind_name(completion->kind)
        );
        yyjson_mut_obj_add_strcpy(
            result->doc,
            item,
            "detail",
            completion->detail ? completion->detail : ""
        );
        yyjson_mut_obj_add_strcpy(
            result->doc,
            item,
            "insert",
            completion->insert ? completion->insert : completion->label
        );
        yyjson_mut_obj_add_int(result->doc, item, "start", completions.start);
        yyjson_mut_obj_add_int(result->doc, item, "end", completions.end);
    }
    char *hover = asset_source_hover(source, (int)offset);
    if (hover) {
        yyjson_mut_obj_add_strcpy(result->doc, value, "hover", hover);
    }
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const asset_source_diagnostic *diagnostic = &source->diagnostics[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(result->doc, diagnostics);
        yyjson_mut_obj_add_strcpy(result->doc, item, "message", diagnostic->message);
        yyjson_mut_obj_add_int(result->doc, item, "start", diagnostic->start);
        yyjson_mut_obj_add_int(result->doc, item, "end", diagnostic->end);
        yyjson_mut_obj_add_int(result->doc, item, "line", diagnostic->line);
        yyjson_mut_obj_add_int(result->doc, item, "column", diagnostic->column);
    }
    free(hover);
    asset_source_completions_free(&completions);
    asset_source_free(source);
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
    free_asset_entries(entries);
    return CHIBI_AUTOMATION_OK;
}

static int handle_read_asset(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char *json = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    char error[256] = { 0 };
    int status = load_asset(root, request, &ref, &json, &bytes, &hash, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    yyjson_doc *document = ref.family->loader == ASSET_UI
                             ? ui_asset_document(root, &ref, json)
                             : parse_project_asset_path(root, ref.relative, json);
    yyjson_val *complete = yyjson_obj_get(request, "complete");
    if (!document && !complete) {
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "asset source is invalid");
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_obj_add_strcpy(result->doc, value, "source", json);
    yyjson_mut_obj_add_val(
        result->doc,
        value,
        "document",
        document ? yyjson_val_mut_copy(result->doc, yyjson_doc_get_root(document))
                 : yyjson_mut_null(result->doc)
    );
    if (complete) {
        status = ref.family->loader == ASSET_UI
                   ? CHIBI_AUTOMATION_INVALID_REQUEST
                   : read_asset_complete(root, &ref, json, complete, result, value, error, sizeof error);
        if (ref.family->loader == ASSET_UI) {
            set_error(error, sizeof error, "UI language assistance uses inspect_ui");
        }
    }
    yyjson_doc_free(document);
    free(json);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    return 0;
}

/* A copy of a field linked to the project's source libraries; NULL on error. */
static behavior_asset *link_source_libraries(
    const char *root,
    const behavior_asset *asset,
    char *error,
    size_t error_size
)
{
    authoring_language_catalog *native = source_native_catalog(root, error, error_size);
    if (!native) {
        return NULL;
    }
    script_lower_bindings bindings = authoring_language_bindings(native);
    authoring_sources *sources = authoring_sources_prepare(
        chibi_automation_content(root),
        authoring_language_catalog_view(native),
        &bindings,
        BEHAVIOR_BYTECODE
    );
    behavior_asset **libraries = authoring_source_graph_libraries(sources);
    behavior_asset *linked = behavior_asset_link_libraries(
        asset,
        (const behavior_asset *const *)libraries,
        acount(libraries),
        error,
        error_size
    );
    authoring_source_graph_libraries_free(libraries);
    authoring_sources_free(sources);
    authoring_language_catalog_free(native);
    return linked;
}

/* A copy of asset linked to the project's libraries (and, for behaviors,
   event declarations), so validation sees calls into them; NULL on error. */
static behavior_asset *link_project_libraries(
    const char *root,
    const behavior_asset *asset,
    char *error,
    size_t error_size
)
{
    behavior_asset **libraries = NULL;
    if (asset->domain == BEHAVIOR_DOMAIN_FIELD) {
        return link_source_libraries(root, asset, error, error_size);
    }
    if (asset->domain != BEHAVIOR_DOMAIN_MATERIAL &&
        asset->domain != BEHAVIOR_DOMAIN_SHAPE && asset->domain != BEHAVIOR_DOMAIN_MODEL &&
        asset->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        set_error(error, error_size, "Executable source cannot use graph library linking");
        return NULL;
    }
    int loaded = load_behavior_family_assets(root, family_by_kind("shader-library"), &libraries, error, error_size);
    behavior_asset *linked = loaded == CHIBI_AUTOMATION_OK
                               ? behavior_asset_link_libraries(
                                     asset,
                                     (const behavior_asset *const *)libraries,
                                     acount(libraries),
                                     error,
                                     error_size
                                 )
                               : NULL;
    free_behavior_assets(libraries);
    return linked;
}

static int load_model_catalog(const char *root, behavior_asset ***out, char *error, size_t error_size)
{
    int status = load_behavior_family_assets(root, family_by_kind("model"), out, error, error_size);
    behavior_asset **materials = NULL;
    if (status == CHIBI_AUTOMATION_OK) {
        status = load_behavior_family_assets(root, family_by_kind("material"), &materials, error, error_size);
    }
    for (int i = 0; status == CHIBI_AUTOMATION_OK && i < acount(materials); i++) {
        behavior_asset *linked = link_project_libraries(root, materials[i], error, error_size);
        if (!linked) {
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            break;
        }
        apush(*out, linked);
    }
    free_behavior_assets(materials);
    return status;
}

static int compile_project_graph(const char *root, const behavior_asset *asset, compile_report *report)
{
    if (asset->domain != BEHAVIOR_DOMAIN_MODEL) {
        return graph_asset_compile(asset, report);
    }
    behavior_asset **models = NULL;
    char load_error[256] = { 0 };
    int loaded = load_model_catalog(root, &models, load_error, sizeof load_error);
    model_program program;
    model_program_init(&program);
    shader_error error = { 0 };
    bool valid = loaded == CHIBI_AUTOMATION_OK &&
                 model_compile_with_catalog(
                     asset,
                     (const behavior_asset *const *)models,
                     acount(models),
                     &program,
                     &error
                 );
    if (valid) {
        valid = render_image_validate_volumes(&program.shader, &error);
    }
    model_program_free(&program);
    free_behavior_assets(models);
    if (!valid) {
        compile_diagnostic diagnostic = { .severity = CMPL_ERROR,
                                          .rule = CMPL_RULE_OPERATOR,
                                          .graph_id = asset->graph->graph_id,
                                          .node_id = error.node };
        snprintf(
            diagnostic.message,
            sizeof diagnostic.message,
            "%s",
            loaded == CHIBI_AUTOMATION_OK ? error.message : load_error
        );
        apush(report->items, diagnostic);
        report->error_count++;
    }
    return report->error_count;
}

static int validate_prototype_bindings(const char *, const char *, char *, size_t);
static int validate_scene_behavior(const char *, const char *, char *, size_t);
static int validate_scene_dependencies(const char *, const char *, char *, size_t);

static int handle_validate_asset(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char *json = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    char error[256] = { 0 };
    int status = load_asset(root, request, &ref, &json, &bytes, &hash, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    if (source_program_family(ref.family)) {
        int validated = inspect_source_asset(root, request, result, exit_code, &ref, json, bytes, hash);
        yyjson_mut_val *source_result = yyjson_mut_obj_get(result->root, "result");
        yyjson_mut_val *source_diagnostics = yyjson_mut_obj_get(source_result, "diagnostics");
        /* Severity 2 is a unit warning; every other diagnostic is an error. */
        int source_warnings = 0;
        yyjson_mut_arr_iter items = yyjson_mut_arr_iter_with(source_diagnostics);
        for (yyjson_mut_val *item; (item = yyjson_mut_arr_iter_next(&items));) {
            source_warnings += yyjson_mut_get_int(yyjson_mut_obj_get(item, "severity")) ==
                               2;
        }
        int source_errors = (int)yyjson_mut_arr_size(source_diagnostics) - source_warnings;
        yyjson_mut_obj_add_int(result->doc, source_result, "errors", source_errors);
        yyjson_mut_obj_add_int(result->doc, source_result, "warnings", source_warnings);
        yyjson_mut_obj_add_int(result->doc, source_result, "notes", 0);
        free(json);
        return validated;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
    int errors = 0;
    int warnings = 0;
    int notes = 0;
    if (ref.family->loader == ASSET_GRAPH) {
        behavior_asset *asset = load_project_behavior(root, json);
        if (!asset) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, graph_last_error());
        } else {
            compile_report report;
            compile_report_init(&report);
            behavior_asset *linked = link_project_libraries(root, asset, error, sizeof error);
            if (linked) {
                errors = compile_project_graph(root, linked, &report);
                errors += studio_project_is(root)
                            ? 0
                            : studio_nodes_check(asset, &report);
            } else {
                errors = 1;
                add_load_diagnostic(result->doc, diagnostics, error);
            }
            behavior_asset_free(linked);
            warnings = report.warning_count;
            notes = report.note_count;
            for (int i = 0; i < acount(report.items); i++) {
                add_diagnostic(result->doc, diagnostics, asset, &report.items[i]);
            }
            compile_report_free(&report);
            behavior_asset_free(asset);
        }
    } else if (ref.family->loader == ASSET_RELATIONSHIP) {
        ecs_relationship_desc *relationship = authoring_relationship_source_read(json, error, sizeof error);
        if (!relationship) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, error);
        }
        authoring_relationship_source_free(relationship);
    } else if (ref.family->loader == ASSET_PROTOTYPE) {
        prototype_includes includes = { .root = root };
        prototype_registry registry = registry_with_includes(&includes);
        char load_error[256] = { 0 };
        prototype *prototype = prototype_load(json, &registry, load_error, sizeof load_error);
        if (!prototype) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        } else {
            yyjson_mut_obj_add_strcpy(result->doc, value, "identity", prototype->identity);
            yyjson_mut_obj_add_int(
                result->doc,
                value,
                "components",
                acount(prototype->components)
            );
            prototype_free(prototype);
            if (validate_prototype_bindings(root, json, load_error, sizeof load_error) !=
                CHIBI_AUTOMATION_OK) {
                errors = 1;
                add_load_diagnostic(result->doc, diagnostics, load_error);
            }
        }
        prototype_includes_free(&includes);
    } else if (ref.family->loader == ASSET_UI) {
        char *name = asset_name_from_filename(ref.filename);
        errors = validate_ui(root, name, json, result->doc, diagnostics);
        free(name);
    } else if (ref.family->loader == ASSET_EDITOR || ref.family->loader == ASSET_SCENARIO) {
        char load_error[256] = { 0 };
        bool ok = ref.family->loader == ASSET_EDITOR
                    ? read_editor_asset(json, load_error, sizeof load_error)
                    : read_scenario_asset(json, load_error, sizeof load_error);
        if (!ok) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        }
    } else {
        char load_error[256] = { 0 };
        scene_document *scene = scene_document_load(json, load_error, sizeof load_error);
        if (!scene) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        } else {
            yyjson_mut_obj_add_strcpy(result->doc, value, "scene_name", scene->name);
            yyjson_mut_obj_add_int(result->doc, value, "entities", acount(scene->entities));
            scene_document_free(scene);
            if (validate_scene_dependencies(root, json, load_error, sizeof load_error) !=
                CHIBI_AUTOMATION_OK) {
                errors = 1;
                add_load_diagnostic(result->doc, diagnostics, load_error);
            }
        }
    }
    yyjson_mut_obj_add_bool(result->doc, value, "valid", errors == 0);
    yyjson_mut_obj_add_int(result->doc, value, "errors", errors);
    yyjson_mut_obj_add_int(result->doc, value, "warnings", warnings);
    yyjson_mut_obj_add_int(result->doc, value, "notes", notes);
    free(json);
    if (errors) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "asset validation failed");
        return -1;
    }
    return 0;
}

/* A cataloged asset's text, owned; NULL with why in `error`. */
static char *asset_text(const char *root, const catalog_asset *asset, char *error, size_t size)
{
    char *path = content_path(root, asset->asset.relative);
    char *text = read_file(path, NULL, NULL, error, size);
    free(path);
    return text;
}

/* Whether `path` is one of the cataloged scenes, which the asset checks cover. */
static bool cataloged_scene(const char *root, const catalog_asset *assets, const char *path)
{
    char *file = realpath(path, NULL);
    bool found = false;
    for (int i = 0; file && !found && i < acount(assets); i++) {
        if (assets[i].asset.family->loader != ASSET_SCENE) {
            continue;
        }
        char *scene = content_path(root, assets[i].asset.relative);
        char *other = realpath(scene, NULL);
        found = other && !strcmp(file, other);
        free(other);
        free(scene);
    }
    free(file);
    return found;
}

/* A start scene no scene asset covers loads, and so do its prototypes and
   behaviors; false with why in `error` when it does not. */
static bool valid_start_scene(const char *root, const char *path, char *error, size_t size)
{
    char *json = read_file(path, NULL, NULL, error, size);
    bool valid = json && (validate_scene_dependencies(root, json, error, size) ==
                              CHIBI_AUTOMATION_OK ||
                          catalog_fallout(error));
    free(json);
    return valid;
}

/* Preparation takes every markup entry at the top of the ui folder, and
 * follows links. The catalog checks only regular files, so any other entry
 * is unchecked here and refused; returns how many there are. */
static int validate_ui_folder(const char *root, yyjson_mut_doc *doc, yyjson_mut_val *diagnostics)
{
    char folder[1024], path[1600], message[600];
    ui_folder(root, folder, sizeof folder);
    DIR *dir = opendir(folder);
    int errors = 0;
    for (struct dirent *entry; dir && (entry = readdir(dir));) {
        const char *name = entry->d_name;
        size_t length = strlen(name), extension = strlen(UI_MARKUP_EXTENSION);
        if (name[0] == '.' || length <= extension ||
            strcmp(name + length - extension, UI_MARKUP_EXTENSION)) {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", folder, name);
        struct stat info;
        if (!lstat(path, &info) && S_ISREG(info.st_mode)) {
            continue;
        }
        snprintf(
            message,
            sizeof message,
            "UI document '%.*s' is not a regular file",
            (int)(length - extension),
            name
        );
        add_load_diagnostic(doc, diagnostics, message);
        errors++;
    }
    if (dir) {
        closedir(dir);
    }
    return errors;
}

/* Sounds are not cataloged assets, so the first one preparation refuses is
   a problem of the project; returns how many, at most one. */
static int validate_sounds(const char *root, yyjson_mut_doc *doc, yyjson_mut_val *project)
{
    player_asset *sounds = NULL;
    char error[512];
    bool valid = authoring_sounds_prepare(chibi_automation_content(root), &sounds, error, sizeof error);
    authoring_visual_prepared_free(sounds);
    if (!valid) {
        add_load_diagnostic(doc, project, error);
    }
    return !valid;
}

/* Prepares the project as chibi-compile does, which refuses more than the
   checks before it, such as resources that only overflow the player's limits
   together; the refusal is a problem of the project. Returns how many, at
   most one. */
static int validate_preparation(const char *root, yyjson_mut_doc *doc, yyjson_mut_val *project)
{
    char error[512] = { 0 };
    native_capabilities *native = authoring_application_catalog_new(
        studio_project_is(root),
        error,
        sizeof error
    );
    authoring_project_options options = { .project = root,
                                          .native = native,
                                          .components = { REGISTRY_COMPONENTS,
                                                          REGISTRY_COMPONENT_COUNT },
                                          .modules = WORLD_MODULES_ALL };
    authoring_project_artifact artifact = { 0 };
    bool valid = native &&
                 authoring_project_prepare(&options, &artifact, error, sizeof error);
    authoring_project_artifact_free(&artifact);
    native_capabilities_free(native);
    if (!valid) {
        add_load_diagnostic(doc, project, error);
    }
    return !valid;
}

/* The manifest opens at this build's version and names a start scene that
   exists and loads, a UI document that exists, and a title and icon that
   preparation accepts, and the ui folder holds only regular markup files;
   adds a diagnostic for each problem and returns how many. */
static int validate_manifest(
    const char *root,
    const catalog_asset *assets,
    yyjson_mut_doc *doc,
    yyjson_mut_val *diagnostics
)
{
    chibi_project project = { 0 };
    char error[512] = { 0 };
    /* Validation never upgrades the project, so an old one fails here. */
    if (chibi_project_read(&project, root, error, sizeof error) != 0) {
        add_load_diagnostic(doc, diagnostics, error);
        return 1;
    }
    char *scene = chibi_project_scene_path(&project, NULL);
    assert(scene); /* an open project has a content folder and start scene */
    struct stat info;
    char message[1024];
    int errors = 0;
    if (stat(scene, &info) != 0 || !S_ISREG(info.st_mode)) {
        snprintf(message, sizeof message, "start scene not found: %s", project.start_scene);
        add_load_diagnostic(doc, diagnostics, message);
        errors++;
    } else if (!cataloged_scene(root, assets, scene) && !valid_start_scene(root, scene, error, sizeof error)) {
        snprintf(message, sizeof message, "start scene %s: %s", project.start_scene, error);
        add_load_diagnostic(doc, diagnostics, message);
        errors++;
    }
    if (project.ui && !ui_exists(root, project.ui)) {
        snprintf(message, sizeof message, "Unknown UI document '%s'", project.ui);
        add_load_diagnostic(doc, diagnostics, message);
        errors++;
    }
    if (!authoring_project_application_valid(&project, error, sizeof error)) {
        add_load_diagnostic(doc, diagnostics, error);
        errors++;
    }
    errors += validate_ui_folder(root, doc, diagnostics);
    free(scene);
    chibi_project_free(&project);
    return errors;
}

/* What validate_asset checks beyond the catalog: a scene's prototypes and
   behaviors, and a prototype's shader binding and UI document. */
static int validate_asset_dependencies(const char *root, const catalog_asset *asset, char *error, size_t size)
{
    int loader = asset->asset.family->loader;
    if (asset->errors || (loader != ASSET_SCENE && loader != ASSET_PROTOTYPE)) {
        return CHIBI_AUTOMATION_OK;
    }
    char *json = asset_text(root, asset, error, size);
    if (!json) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    int status = loader == ASSET_SCENE
                   ? validate_scene_dependencies(root, json, error, size)
                   : validate_prototype_bindings(root, json, error, size);
    free(json);
    return status;
}

/* validate_asset's markup, style and RmlUi checks for a UI asset the catalog
   loaded; returns the error count. */
static int validate_catalog_ui(
    const char *root,
    const catalog_asset *asset,
    yyjson_mut_doc *doc,
    yyjson_mut_val *diagnostics
)
{
    if (asset->errors || asset->asset.family->loader != ASSET_UI) {
        return 0;
    }
    char error[256] = { 0 };
    char *rml = asset_text(root, asset, error, sizeof error);
    if (!rml) {
        add_load_diagnostic(doc, diagnostics, error);
        return 1;
    }
    int errors = validate_ui(root, asset->name, rml, doc, diagnostics);
    free(rml);
    return errors;
}

static void add_range_diagnostic(yyjson_mut_doc *doc, yyjson_mut_val *array, const source_catalog_diagnostic *diagnostic)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "severity", "error");
    yyjson_mut_obj_add_strcpy(doc, object, "rule_name", "CATALOG");
    yyjson_mut_obj_add_strcpy(doc, object, "message", diagnostic->message);
    if (diagnostic->range.line) {
        yyjson_mut_obj_add_int(doc, object, "line", diagnostic->range.line);
        yyjson_mut_obj_add_int(doc, object, "column", diagnostic->range.column);
    }
}

/* The project's declaration catalog, whose diagnostics name the file they are
   about, such as a scene naming a missing prototype. Without it every source
   reports only that the catalog is incomplete. NULL with why in `error`. */
static source_catalog *declaration_catalog(const char *root, char *error, size_t size)
{
    authoring_language_catalog *native = source_native_catalog(root, error, size);
    if (!native) {
        return NULL;
    }
    source_catalog *catalog = source_catalog_new(
        chibi_automation_content(root),
        authoring_language_catalog_view(native)
    );
    authoring_language_catalog_free(native);
    return catalog;
}

/* Keep the loader's concrete cause instead of adding catalog fallout for the
 * same legacy declaration. Unreferenced catalog errors remain visible. */
static bool declaration_explained(
    const source_catalog_diagnostic *diagnostic,
    const catalog_asset *asset,
    const char (*dependency_errors)[256],
    int count
)
{
    for (int i = 0; i < acount(asset->diagnostics); i++) {
        const catalog_diagnostic *load = &asset->diagnostics[i];
        const char *prefix = "prototype include ", *suffix = " is unavailable";
        size_t length = strlen(load->message), head = strlen(prefix),
               tail = strlen(suffix);
        if (!strcmp(load->rule_name, "LOAD") && length > head + tail &&
            !strncmp(load->message, prefix, head) &&
            !strcmp(load->message + length - tail, suffix)) {
            char expected[192];
            int written = snprintf(
                expected,
                sizeof expected,
                "Unknown prototype '%.*s'",
                (int)(length - head - tail),
                load->message + head
            );
            if (written > 0 && written < (int)sizeof expected &&
                !strcmp(diagnostic->message, expected)) {
                return true;
            }
        }
    }
    if (!strcmp(diagnostic->message, "Duplicate project capability identity")) {
        size_t size = strlen(asset->asset.family->kind) + strlen(asset->name) + 32;
        char *cause = xmalloc(size);
        snprintf(
            cause,
            size,
            "both declare the %s '%s'",
            asset->asset.family->kind,
            asset->name
        );
        bool found = false;
        for (int i = 0; i < count; i++) {
            found |= strstr(dependency_errors[i], cause) != NULL;
        }
        free(cause);
        return found;
    }
    return false;
}

static int handle_validate_project(const char *root, response *result, int *exit_code)
{
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t hash = 0;
    char error[256] = { 0 }, catalog_error[256] = { 0 };
    /* Colliding names leave no catalog, but they are a problem to list. */
    int cataloged = catalog_snapshot_get(root, &assets, &revision, &hash, catalog_error, sizeof catalog_error);
    if (cataloged == CHIBI_AUTOMATION_IO_ERROR) {
        response_error(result, exit_code, cataloged, catalog_error);
        return -1;
    }
    source_catalog *catalog = declaration_catalog(root, error, sizeof error);
    if (!catalog) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    int declarations = source_catalog_diagnostic_count(catalog);
    bool *placed = xcalloc((size_t)declarations + 1, sizeof *placed);
    yyjson_mut_doc *doc = result->doc;
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, result->root, "result");
    yyjson_mut_val *project = yyjson_mut_obj_add_arr(doc, value, "project");
    int errors = validate_manifest(root, assets, doc, project);
    if (cataloged != CHIBI_AUTOMATION_OK) {
        add_load_diagnostic(doc, project, catalog_error);
        errors++;
    }
    int warnings = 0;
    yyjson_mut_val *problems = yyjson_mut_obj_add_arr(doc, value, "problems");
    int asset_count = acount(assets);
    char (*dependency_errors)[256] = xcalloc((size_t)asset_count + 1, sizeof *dependency_errors);
    bool *dependencies_valid = xcalloc((size_t)asset_count + 1, sizeof *dependencies_valid);
    for (int i = 0; i < asset_count; i++) {
        char *path = content_path(root, assets[i].asset.relative);
        dependencies_valid[i] = validate_asset_dependencies(
                                    root,
                                    &assets[i],
                                    dependency_errors[i],
                                    sizeof dependency_errors[i]
                                ) == CHIBI_AUTOMATION_OK ||
                                catalog_fallout(dependency_errors[i]) ||
                                catalog_lists(catalog, path, dependency_errors[i]);
        free(path);
    }
    bool *explained = xcalloc((size_t)declarations + 1, sizeof *explained);
    for (int j = 0; j < declarations; j++) {
        const source_catalog_diagnostic *diagnostic = source_catalog_diagnostic_at(catalog, j);
        for (int i = 0; diagnostic->path && i < asset_count; i++) {
            char *path = content_path(root, assets[i].asset.relative);
            if (!strcmp(diagnostic->path, path)) {
                explained[j] = declaration_explained(diagnostic, &assets[i], dependency_errors, asset_count);
                placed[j] = explained[j];
            }
            free(path);
        }
    }
    for (int i = 0; i < acount(assets); i++) {
        const catalog_asset *asset = &assets[i];
        error[0] = '\0';
        char *path = content_path(root, asset->asset.relative);
        /* The declaration diagnostics below take the place of the fallout,
           in sources and in scenes whose behaviors are sources, and of the
           errors they repeat with lines. */
        bool *omitted = catalog_omitted(catalog, asset, path);
        int asset_errors = asset->errors;
        for (int j = 0; j < acount(asset->diagnostics); j++) {
            asset_errors -= omitted[j];
        }
        bool dependencies = !dependencies_valid[i];
        snprintf(error, sizeof error, "%s", dependency_errors[i]);
        /* Kept apart until the asset turns out to have problems. */
        yyjson_mut_val *ui = yyjson_mut_arr(doc);
        asset_errors += validate_catalog_ui(root, asset, doc, ui);
        for (int j = 0; j < declarations; j++) {
            const source_catalog_diagnostic *diagnostic = source_catalog_diagnostic_at(catalog, j);
            if (!explained[j] && diagnostic->path && !strcmp(diagnostic->path, path)) {
                placed[j] = true;
                asset_errors++;
            }
        }
        asset_errors += dependencies;
        errors += asset_errors;
        warnings += asset->warnings;
        if (!asset_errors && !asset->warnings) {
            free(omitted);
            free(path);
            continue;
        }
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, problems);
        yyjson_mut_obj_add_strcpy(doc, object, "kind", asset->asset.family->kind);
        yyjson_mut_obj_add_strcpy(doc, object, "name", asset->name);
        yyjson_mut_obj_add_strcpy(doc, object, "path", asset->asset.relative);
        yyjson_mut_obj_add_int(doc, object, "errors", asset_errors);
        yyjson_mut_obj_add_int(doc, object, "warnings", asset->warnings);
        add_catalog_diagnostics(doc, object, asset, omitted);
        free(omitted);
        yyjson_mut_val *diagnostics = yyjson_mut_obj_get(object, "diagnostics");
        for (int j = 0; j < declarations; j++) {
            const source_catalog_diagnostic *diagnostic = source_catalog_diagnostic_at(catalog, j);
            if (!explained[j] && diagnostic->path && !strcmp(diagnostic->path, path)) {
                add_range_diagnostic(doc, diagnostics, diagnostic);
            }
        }
        if (dependencies) {
            add_load_diagnostic(doc, diagnostics, error);
        }
        for (yyjson_mut_val *item; (item = yyjson_mut_arr_remove_first(ui));) {
            yyjson_mut_arr_append(diagnostics, item);
        }
        free(path);
    }
    /* A catalog diagnostic about a file no asset family lists stays visible. */
    for (int j = 0; j < declarations; j++) {
        if (!placed[j]) {
            add_range_diagnostic(doc, project, source_catalog_diagnostic_at(catalog, j));
            errors++;
        }
    }
    free(placed);
    free(explained);
    free(dependencies_valid);
    free(dependency_errors);
    source_catalog_free(catalog);
    /* Preparation, which checks the sounds too, runs only once nothing else
       is wrong, since it stops at the first problem and would repeat one
       listed above; the project then opened at this build's version, so it
       never upgrades its files. Otherwise the sounds are checked alone. */
    errors += errors ? validate_sounds(root, doc, project)
                     : validate_preparation(root, doc, project);
    yyjson_mut_obj_add_bool(doc, value, "valid", errors == 0);
    yyjson_mut_obj_add_int(doc, value, "assets", acount(assets));
    yyjson_mut_obj_add_int(doc, value, "errors", errors);
    yyjson_mut_obj_add_int(doc, value, "warnings", warnings);
    if (errors) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "project validation failed");
        return -1;
    }
    return 0;
}

/* The status a scaffold writer's errno names: a refused request, a taken
 * name, or storage that failed. */
static int scaffold_status()
{
    switch (errno) {
        case EINVAL: return CHIBI_AUTOMATION_INVALID_REQUEST;
        case EEXIST: return CHIBI_AUTOMATION_CONFLICT;
        default: return CHIBI_AUTOMATION_IO_ERROR;
    }
}

static int handle_new_project(yyjson_val *request, response *result, int *exit_code)
{
    const char *path = request_string(request, "path");
    char error[512] = { 0 };
    struct stat info;
    if (path && stat(path, &info) == 0) {
        snprintf(error, sizeof error, "Project already exists: %s", path);
        response_error(result, exit_code, CHIBI_AUTOMATION_CONFLICT, error);
        return -1;
    }
    chibi_project project = { 0 };
    if (chibi_project_create(path, request_string(request, "name"), error, sizeof error) !=
        0) {
        response_error(result, exit_code, scaffold_status(), error);
        return -1;
    }
    if (open_authored_project(&project, path, error, sizeof error) != 0) {
        response_error(result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
        return -1;
    }
    char *scene = chibi_project_scene_path(&project, NULL);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "project_root", project.root);
    yyjson_mut_obj_add_str(result->doc, value, "manifest", CHIBI_PROJECT_MANIFEST);
    yyjson_mut_obj_add_strcpy(
        result->doc,
        value,
        "start_scene",
        scene + strlen(project.root) + 1
    );
    free(scene);
    chibi_project_free(&project);
    return 0;
}

/* The new-asset kind whose folder is the family's, or -1 when the family has
   no new form. */
static int new_asset_kind_of(const asset_family *family)
{
    for (int i = 0; family && i < NEW_ASSET_KIND_COUNT; i++) {
        if (!strcmp(new_asset_folder((new_asset_kind)i), family->folder)) {
            return i;
        }
    }
    return -1;
}

static int handle_new_asset(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *name = request_string(request, "name");
    const asset_family *family = family_by_kind(request_string(request, "kind"));
    int kind = new_asset_kind_of(family);
    if (kind < 0 || !name) {
        response_error(
            result,
            exit_code,
            CHIBI_AUTOMATION_INVALID_REQUEST,
            kind < 0 ? "kind must name an asset family that has a new form" : "name is required"
        );
        return -1;
    }
    char error[512] = { 0 };
    /* A prototype's name is the one it declares, which a file named
     * otherwise may already hold. */
    if (family->loader == ASSET_PROTOTYPE) {
        asset_ref ref;
        char *text = NULL;
        int found = load_named_asset(root, family, name, &ref, &text, NULL, NULL, error, sizeof error);
        free(text);
        if (found == CHIBI_AUTOMATION_OK) {
            snprintf(error, sizeof error, "%s already declares the prototype '%s'", ref.relative, name);
        }
        /* A lookup that failed to read could have missed one. */
        if (found != CHIBI_AUTOMATION_NOT_FOUND) {
            response_error(
                result,
                exit_code,
                found == CHIBI_AUTOMATION_OK ? CHIBI_AUTOMATION_CONFLICT : found,
                error
            );
            return -1;
        }
    }
    const char *content = chibi_automation_content(root);
    errno = 0;
    char *path = new_asset_create(content, (new_asset_kind)kind, name, error, sizeof error);
    if (!path) {
        response_error(result, exit_code, scaffold_status(), error);
        return -1;
    }
    char relative[1024];
    snprintf(relative, sizeof relative, "content%s", path + strlen(content));
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "kind", family->kind);
    yyjson_mut_obj_add_strcpy(result->doc, value, "name", name);
    yyjson_mut_obj_add_strcpy(result->doc, value, "path", relative);
    free(path);
    return 0;
}

static void free_behavior_assets(behavior_asset **assets)
{
    for (int i = 0; i < acount(assets); i++) {
        behavior_asset_free(assets[i]);
    }
    afree(assets);
}

static int load_behavior_family_assets(
    const char *root,
    const asset_family *family,
    behavior_asset ***out,
    char *error,
    size_t error_size
)
{
    asset_entry *entries = NULL;
    if (enumerate_assets(root, &entries, error, error_size) != 0) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    behavior_asset **assets = NULL;
    for (int i = 0; i < acount(entries); i++) {
        if (entries[i].family != family) {
            continue;
        }
        char *path = content_path(root, entries[i].relative);
        char read_error[256] = { 0 };
        char *json = read_file(path, NULL, NULL, read_error, sizeof read_error);
        if (!json) {
            free(path);
            free_asset_entries(entries);
            free_behavior_assets(assets);
            set_error(error, error_size, read_error);
            return CHIBI_AUTOMATION_IO_ERROR;
        }
        behavior_asset *asset = behavior_asset_load(json);
        free(path);
        free(json);
        if (!asset) {
            char relative[512];
            snprintf(relative, sizeof relative, "%s", entries[i].relative);
            free_asset_entries(entries);
            free_behavior_assets(assets);
            snprintf(error, error_size, "could not load %s: %s", relative, graph_last_error());
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        apush(assets, asset);
    }
    free_asset_entries(entries);
    *out = assets;
    return CHIBI_AUTOMATION_OK;
}

/* Scenarios use the application's canonical declaration and source pipeline. */
static int register_shared_assets(const char *root, prototype_preview *preview, char *error, size_t error_size)
{
    const char *content = chibi_automation_content(root);
    /* The preview's live world is the only world, so nothing registers twice. */
    int loaded = app_behavior_fixtures_load(prototype_preview_live(preview), NULL, content);
    bool sounds = loaded >= 0 && authoring_sounds_load(
                                     content,
                                     engine_audio(prototype_preview_runtime(preview)),
                                     error,
                                     error_size
                                 );
    if (loaded < 0) {
        set_error(error, error_size, "project source or native asset preparation failed; see preparation diagnostics");
    }
    return sounds ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_VALIDATION_ERROR;
}

static int add_scenario_timing(yyjson_mut_doc *doc, yyjson_mut_val *object, uint64_t frames, double fixed_dt)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "timing");
    yyjson_mut_obj_add_uint(doc, value, "frame", frames);
    yyjson_mut_obj_add_uint(doc, value, "simulation_steps", frames);
    yyjson_mut_obj_add_real(doc, value, "simulation_time", frames * fixed_dt);
    yyjson_mut_obj_add_real(doc, value, "fixed_dt", fixed_dt);
    return 0;
}

#define SCENARIO_FIXED_DT (1.0 / 60.0)

static uint64_t scenario_hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void scenario_hash_value(uint64_t *hash, pin_type type, const char *type_name, const pin_value *value)
{
    *hash = scenario_hash_bytes(*hash, &type, sizeof type);
    const char *name = type_name ? type_name : "";
    uint64_t name_size = strlen(name);
    *hash = scenario_hash_bytes(*hash, &name_size, sizeof name_size);
    *hash = scenario_hash_bytes(*hash, name, (size_t)name_size);
    switch (type) {
        case PIN_BOOL:
            *hash = scenario_hash_bytes(*hash, &value->b, sizeof value->b);
            break;
        case PIN_I8:
            *hash = scenario_hash_bytes(*hash, &value->i8, sizeof value->i8);
            break;
        case PIN_U8:
            *hash = scenario_hash_bytes(*hash, &value->u8, sizeof value->u8);
            break;
        case PIN_I16:
            *hash = scenario_hash_bytes(*hash, &value->i16, sizeof value->i16);
            break;
        case PIN_U16:
            *hash = scenario_hash_bytes(*hash, &value->u16, sizeof value->u16);
            break;
        case PIN_I32:
            *hash = scenario_hash_bytes(*hash, &value->i, sizeof value->i);
            break;
        case PIN_U32:
            *hash = scenario_hash_bytes(*hash, &value->u, sizeof value->u);
            break;
        case PIN_I64:
            *hash = scenario_hash_bytes(*hash, &value->i64, sizeof value->i64);
            break;
        case PIN_U64:
        case PIN_HANDLE:
            *hash = scenario_hash_bytes(*hash, &value->u64, sizeof value->u64);
            break;
        case PIN_F32:
            *hash = scenario_hash_bytes(*hash, &value->f, sizeof value->f);
            break;
        case PIN_F64:
            *hash = scenario_hash_bytes(*hash, &value->d, sizeof value->d);
            break;
        case PIN_ENUM:
            *hash = scenario_hash_bytes(*hash, &value->e, sizeof value->e);
            break;
        case PIN_NAME:
        case PIN_STR: {
            pin_string text = pin_text(*value);
            uint64_t text_size = text.length;
            *hash = scenario_hash_bytes(*hash, &text_size, sizeof text_size);
            *hash = scenario_hash_bytes(*hash, text.bytes, (size_t)text_size);
            break;
        }
        case PIN_STRUCT: {
            const type_desc *struct_type = type_catalog_find(type_name);
            if (!struct_type || !value->ptr) {
                break;
            }
            for (int i = 0; i < struct_type->field_count; i++) {
                const type_field *field = &struct_type->fields[i];
                uint64_t field_size = strlen(field->name ? field->name : "");
                *hash = scenario_hash_bytes(*hash, &field_size, sizeof field_size);
                *hash = scenario_hash_bytes(*hash, field->name, (size_t)field_size);
                arena storage = { 0 };
                pin_value field_value;
                const char *error = pin_read_field(&storage, value->ptr, field, &field_value);
                assert(!error);
                if (!error) {
                    scenario_hash_value(hash, field->type, field->type_name, &field_value);
                }
                arena_free(&storage);
            }
            break;
        }
        case PIN_QUERY:
        case PIN_POINTER:
        case PIN_TASK:
        case PIN_UNION:
            assert(false && "Runtime references, tasks and unions cannot be ECS fields");
            break;
        case PIN_FLOW: break;
    }
}

static uint64_t scenario_world_hash(const ecs_world *world)
{
    uint64_t hash = UINT64_C(1099511628211);
    uint32_t high_water = ecs_world_high_water_mark(world);
    for (uint32_t slot = 1; slot <= high_water; slot++) {
        ecs_entity entity = { slot, ecs_world_generation(world, slot) };
        if (!ecs_entity_is_alive(world, entity)) {
            continue;
        }
        hash = scenario_hash_bytes(hash, &entity, sizeof entity);
        for (int i = 0; i < ecs_world_component_count(world); i++) {
            const ecs_component_desc *component = ecs_world_component_at(world, i);
            if (!ecs_component_has(world, entity, component->id)) {
                continue;
            }
            hash = scenario_hash_bytes(hash, &component->id, sizeof component->id);
            const void *data = ecs_component_data_const(world, entity, component->id);
            if (!component->type) {
                hash = scenario_hash_bytes(hash, data, component->element_size);
                continue;
            }
            for (int field_index = 0; field_index < component->type->field_count;
                 field_index++) {
                const type_field *field = &component->type->fields[field_index];
                uint64_t field_size = strlen(field->name ? field->name : "");
                hash = scenario_hash_bytes(hash, &field_size, sizeof field_size);
                hash = scenario_hash_bytes(hash, field->name, (size_t)field_size);
                arena storage = { 0 };
                pin_value value;
                const char *error = pin_read_field(&storage, data, field, &value);
                assert(!error);
                if (!error) {
                    scenario_hash_value(&hash, field->type, field->type_name, &value);
                }
                arena_free(&storage);
            }
        }
    }
    return hash;
}

static char *scenario_prototype_text(const char *text, uint64_t hash)
{
    char error[256];
    prototype_registry registry = project_registry();
    av_doc *document = authoring_prototype_document_read(text, &registry, error, sizeof error);
    if (!document) {
        return NULL;
    }
    av_value *root = av_doc_root(document);
    if (!av_is_obj(root) || av_obj_get(root, "identity")) {
        char *copy = xstrdup(text);
        av_doc_free(document);
        return copy;
    }
    char identity[32];
    snprintf(identity, sizeof identity, "scenario-%016llx", (unsigned long long)hash);
    av_obj_add_strcpy(document, root, "identity", identity);
    char *copy = av_doc_write(document);
    av_doc_free(document);
    return copy;
}

typedef struct
{
    bool scene_target;
    bool owns_file_system;
    char *root; /* a scene target's project, for the scenes scripts load */
    asset_ref prototype_ref;
    size_t prototype_bytes;
    uint64_t prototype_hash;
    prototype_registry registry;
    prototype_includes includes;
    prototype *prototype;
    scene_document *scene;
    int loads; /* scenes started by Load Scene */
    prototype **scene_prototypes;
    prototype_preview_scene_entity *scene_entities;
    prototype_preview *preview;
    asset_entry *runtime_assets;
    uint64_t runtime_hash;
    char *initial_saved_state;
    const av_value *scenario_saved_state; /* a replayed scenario's, borrowed */
    CF_V2 aim;
    bool aim_firing, aim_point, devices_sampled;
    input_pointer_ray pointer;
    uint64_t controls;
    input_controller_state controllers[INPUT_CONTROLLER_MAX];
} scenario_context;

static scene_flow *scenario_scenes(const scenario_context *context)
{
    return world_scenes(prototype_preview_live(context->preview)->runtime->owner);
}

/* Mixes in the current scene's name: two scenes can leave the same state
 * yet answer Current Scene differently. Prototype scenarios have no scene. */
static uint64_t scenario_scene_hash(const scenario_context *context, uint64_t hash)
{
    const char *scene = scene_flow_current(scenario_scenes(context));
    return scene ? scenario_hash_bytes(hash, scene, strlen(scene)) : hash;
}

static uint64_t scenario_state_hash(scenario_context *context)
{
    uint64_t hash = scenario_world_hash(prototype_preview_world(context->preview));
    char *saved = save_export(prototype_preview_live(context->preview)->runtime->save);
    hash = scenario_hash_bytes(hash, saved, strlen(saved));
    free(saved);
    engine *runtime = prototype_preview_runtime(context->preview);
    Fog fog;
    const unsigned char *cells = visibility_cells(engine_visibility(runtime), &fog);
    if (cells) {
        hash = scenario_hash_bytes(
            hash,
            cells,
            (size_t)fog.columns * (size_t)fog.rows * 4
        );
    }
    uint64_t parameters = render_shader_overrides_hash(
        prototype_preview_world(context->preview)
    );
    hash = scenario_hash_bytes(hash, &parameters, sizeof parameters);
    uint64_t sounds = audio_hash(engine_audio(runtime));
    if (materials_enabled(engine_materials(runtime))) {
        material_state materials = materials_inspect(engine_materials(runtime));
        hash = scenario_hash_bytes(hash, &materials.hash, sizeof materials.hash);
    }
    return scenario_scene_hash(context, scenario_hash_bytes(hash, &sounds, sizeof sounds));
}

typedef struct
{
    uint64_t ecs, saved_state, visibility, shader_overrides, materials, audio, scene;
    uint64_t physics_bodies, physics_contacts, physics_events;
    material_hash_parts material;
} scenario_hash_parts;

static scenario_hash_parts scenario_state_hash_parts(scenario_context *context)
{
    const uint64_t seed = UINT64_C(1099511628211);
    scenario_hash_parts parts = {
        .ecs = scenario_world_hash(prototype_preview_world(context->preview)),
        .saved_state = seed,
        .visibility = seed
    };
    char *saved = save_export(prototype_preview_live(context->preview)->runtime->save);
    parts.saved_state = scenario_hash_bytes(parts.saved_state, saved, strlen(saved));
    free(saved);
    engine *runtime = prototype_preview_runtime(context->preview);
    Fog fog;
    const unsigned char *cells = visibility_cells(engine_visibility(runtime), &fog);
    if (cells) {
        parts.visibility = scenario_hash_bytes(
            parts.visibility,
            cells,
            (size_t)fog.columns * (size_t)fog.rows * 4
        );
    }
    parts.shader_overrides = render_shader_overrides_hash(
        prototype_preview_world(context->preview)
    );
    if (materials_enabled(engine_materials(runtime))) {
        parts.material = materials_hash_parts(engine_materials(runtime));
        parts.materials = parts.material.hash;
    }
    parts.audio = audio_hash(engine_audio(runtime));
    parts.scene = scenario_scene_hash(context, seed);
    physics_hashes physics = physics_hash(
        prototype_preview_live(context->preview)->runtime->physics
    );
    parts.physics_bodies = physics.body_state;
    parts.physics_contacts = physics.contact_pairs;
    parts.physics_events = physics.contact_events;
    return parts;
}

static void add_scenario_hash_parts(yyjson_mut_doc *doc, yyjson_mut_val *value, scenario_context *context)
{
    scenario_hash_parts parts = scenario_state_hash_parts(context);
    yyjson_mut_val *hashes = yyjson_mut_obj_add_obj(doc, value, "hash_parts");
    add_hash_named(doc, hashes, "ecs", parts.ecs);
    add_hash_named(doc, hashes, "saved_state", parts.saved_state);
    add_hash_named(doc, hashes, "visibility", parts.visibility);
    add_hash_named(doc, hashes, "shader_overrides", parts.shader_overrides);
    add_hash_named(doc, hashes, "materials", parts.materials);
    add_hash_named(doc, hashes, "audio", parts.audio);
    add_hash_named(doc, hashes, "scene", parts.scene);
    yyjson_mut_val *physics = yyjson_mut_obj_add_obj(doc, hashes, "physics");
    add_hash_named(doc, physics, "body_state", parts.physics_bodies);
    add_hash_named(doc, physics, "contact_pairs", parts.physics_contacts);
    add_hash_named(doc, physics, "contact_events", parts.physics_events);
    yyjson_mut_val *material_parts = yyjson_mut_obj_add_obj(doc, hashes, "material_parts");
    add_hash_named(doc, material_parts, "after_flow", parts.material.after_flow);
    add_hash_named(doc, material_parts, "after_chunks", parts.material.after_chunks);
    add_hash_named(doc, material_parts, "after_particles", parts.material.after_particles);
    add_hash_named(doc, material_parts, "after_masks", parts.material.after_masks);
    yyjson_mut_val *particle_parts = yyjson_mut_obj_add_obj(doc, hashes, "particle_parts");
    add_hash_named(doc, particle_parts, "position", parts.material.particle_position);
    add_hash_named(doc, particle_parts, "velocity", parts.material.particle_velocity);
    add_hash_named(doc, particle_parts, "parameters", parts.material.particle_parameters);
    add_hash_named(doc, particle_parts, "appearance", parts.material.particle_appearance);
}

static void add_presentation_state(yyjson_mut_doc *doc, yyjson_mut_val *value, const scenario_context *context)
{
    engine *runtime = prototype_preview_runtime(context->preview);
    yyjson_mut_obj_add_uint(doc, value, "sound_cues", audio_count(engine_audio(runtime)));
    if (materials_enabled(engine_materials(runtime))) {
        material_state state = materials_inspect(engine_materials(runtime));
        yyjson_mut_val *material = yyjson_mut_obj_add_obj(doc, value, "materials");
        yyjson_mut_obj_add_int(doc, material, "particles", state.particles);
        yyjson_mut_obj_add_int(doc, material, "active_particles", state.active_particles);
        yyjson_mut_val *bounds = yyjson_mut_obj_add_obj(doc, material, "particle_bounds");
        yyjson_mut_obj_add_real(doc, bounds, "x", state.particle_bounds.x);
        yyjson_mut_obj_add_real(doc, bounds, "y", state.particle_bounds.y);
        yyjson_mut_obj_add_real(doc, bounds, "width", state.particle_bounds.width);
        yyjson_mut_obj_add_real(doc, bounds, "height", state.particle_bounds.height);
        yyjson_mut_val *stream = yyjson_mut_obj_add_obj(doc, material, "streaming");
        yyjson_mut_obj_add_int(doc, stream, "resident", state.streaming.resident);
        yyjson_mut_obj_add_int(doc, stream, "stored", state.streaming.stored);
        yyjson_mut_obj_add_int(doc, stream, "loaded", state.streaming.loaded);
        yyjson_mut_obj_add_int(doc, stream, "unloaded", state.streaming.unloaded);
        yyjson_mut_obj_add_uint(doc, stream, "resident_bytes", state.streaming.resident_bytes);
        yyjson_mut_obj_add_uint(doc, stream, "stored_bytes", state.streaming.stored_bytes);
        yyjson_mut_obj_add_int(doc, material, "fuel_particles", state.fuel_particles);
        yyjson_mut_obj_add_int(doc, material, "burning_particles", state.burning_particles);
        yyjson_mut_obj_add_int(doc, material, "thermal_particles", state.thermal_particles);
        yyjson_mut_obj_add_real(doc, material, "fuel_remaining", state.fuel_remaining);
        yyjson_mut_obj_add_real(doc, material, "thermal_energy", state.thermal_energy);
        yyjson_mut_obj_add_int(doc, material, "elastic_particles", state.elastic_particles);
        yyjson_mut_obj_add_real(doc, material, "volume_min", state.volume_min);
        yyjson_mut_obj_add_real(doc, material, "volume_max", state.volume_max);
        yyjson_mut_obj_add_real(doc, material, "strain_max", state.strain_max);
        yyjson_mut_val *mpm = yyjson_mut_obj_add_obj(doc, material, "mpm");
        yyjson_mut_obj_add_int(doc, mpm, "substeps", state.mpm.substeps);
        yyjson_mut_obj_add_int(doc, mpm, "nodes", state.mpm.nodes);
        yyjson_mut_obj_add_real(doc, mpm, "advanced_time", state.mpm.advanced_time);
        yyjson_mut_obj_add_bool(doc, mpm, "limited", state.mpm.limited);
        yyjson_mut_obj_add_int(doc, material, "solids", state.solids);
        yyjson_mut_obj_add_int(doc, material, "cells", state.cells);
        yyjson_mut_obj_add_int(doc, material, "fragments", state.fragments);
        yyjson_mut_obj_add_int(doc, material, "chunks", state.chunks);
        add_hash_named(doc, material, "hash", state.hash);
    }
    Fog fog;
    const unsigned char *cells = visibility_cells(engine_visibility(runtime), &fog);
    yyjson_mut_val *sight = yyjson_mut_obj_add_obj(doc, value, "visibility");
    yyjson_mut_obj_add_bool(doc, sight, "enabled", cells != NULL);
    if (!cells) {
        return;
    }
    int visible = 0, explored = 0;
    for (int i = 0; i < fog.columns * fog.rows; i++) {
        visible += cells[i * 4] != 0;
        explored += cells[i * 4 + 1] != 0;
    }
    yyjson_mut_obj_add_int(doc, sight, "visible_cells", visible);
    yyjson_mut_obj_add_int(doc, sight, "explored_cells", explored);
    yyjson_mut_obj_add_int(doc, sight, "columns", fog.columns);
    yyjson_mut_obj_add_int(doc, sight, "rows", fog.rows);
}

static int scenario_saved_state(scenario_context *context, yyjson_val *request, char *error, size_t size)
{
    yyjson_val *state = yyjson_obj_get(request, "saved_state");
    yyjson_val *path_value = yyjson_obj_get(request, "save_path");
    save *save = prototype_preview_live(context->preview)->runtime->save;
    if (path_value) {
        const char *path = yyjson_get_str(path_value);
        if (!path || path[0] != '/' || strlen(path) != yyjson_get_len(path_value) ||
            state || context->scenario_saved_state) {
            set_error(error, size, "save_path must be an absolute path and cannot accompany saved_state");
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        if (!save_open(save, path)) {
            set_error(error, size, save_error(save));
            return CHIBI_AUTOMATION_IO_ERROR;
        }
        free(context->initial_saved_state);
        context->initial_saved_state = save_export(save);
        return CHIBI_AUTOMATION_OK;
    }
    char *json = state ? yyjson_val_write(state, 0, NULL)
               : context->scenario_saved_state
                   ? av_json_write(context->scenario_saved_state)
                   : xstrdup("{}");
    bool valid = save_import(save, json);
    free(json);
    if (!valid) {
        set_error(error, size, save_error(save));
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    free(context->initial_saved_state);
    context->initial_saved_state = save_export(save);
    return CHIBI_AUTOMATION_OK;
}

static void add_saved_state(yyjson_mut_doc *doc, yyjson_mut_val *value, const scenario_context *context)
{
    char *json = save_export(prototype_preview_live(context->preview)->runtime->save);
    yyjson_doc *state = yyjson_read(json, strlen(json), 0);
    yyjson_mut_obj_add_val(doc, value, "saved_state", yyjson_val_mut_copy(doc, yyjson_doc_get_root(state)));
    yyjson_doc_free(state);
    free(json);
}

static int advance_scenario(
    scenario_context *context,
    uint64_t frames,
    double fixed_dt,
    char *error,
    size_t error_size
);

static int advance_request_scenario(
    scenario_context *context,
    yyjson_val *request,
    uint64_t frames,
    double fixed_dt,
    char *error,
    size_t error_size
);

static void clear_scenario_scene(scenario_context *context)
{
    for (int i = 0; i < acount(context->scene_prototypes); i++) {
        prototype_free(context->scene_prototypes[i]);
    }
    afree(context->scene_prototypes);
    afree(context->scene_entities);
    scene_document_free(context->scene);
    context->scene = NULL;
}

static void scenario_context_free(scenario_context *context)
{
    if (!context) {
        return;
    }
    free(context->initial_saved_state);
    free_asset_entries(context->runtime_assets);
    prototype_preview_free(context->preview);
    prototype_free(context->prototype);
    clear_scenario_scene(context);
    prototype_includes_free(&context->includes);
    free(context->root);
    if (context->owns_file_system) {
        authoring_shader_catalog_clear();
        cf_fs_destroy();
    }
    *context = (scenario_context){ 0 };
}

static int scenario_mount_content(const char *root, scenario_context *context, char *error, size_t error_size)
{
    /* Headless scenarios sample image-backed materials too. GPU previews
       already own the virtual filesystem; only release one created here. */
    if (!cf_fs_get_base_directory()) {
        CF_Result initialized = cf_fs_init(NULL);
        if (cf_is_error(initialized)) {
            set_error(error, error_size, initialized.details);
            return CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
        context->owns_file_system = true;
        authoring_shader_catalog_clear();
    }
    const char *content = chibi_automation_content(root);
    bool mounted = render_image_mount_content(content, error, error_size) &&
                   authoring_mesh_mount_content(content, error, error_size);
    return mounted ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_EXECUTION_ERROR;
}

static int register_preview_behavior_name(
    const char *,
    prototype_preview *preview,
    const char *name,
    char *error,
    size_t error_size
)
{
    if (source_registered(prototype_preview_sources(preview), name)) {
        return CHIBI_AUTOMATION_OK;
    }
    snprintf(error, error_size, "prepared behavior module %s is not registered", name);
    return CHIBI_AUTOMATION_VALIDATION_ERROR;
}

static bool scenario_runtime_asset(const scenario_context *context, const asset_entry *entry)
{
    const char *kind = entry->family->kind;
    return !strcmp(kind, "prototype") || !strcmp(kind, "behavior") ||
           !strcmp(kind, "library") || !strcmp(kind, "event") ||
           !strcmp(kind, "field") || !strcmp(kind, "system") ||
           (context->scene_target && !strcmp(kind, "scene"));
}

static int register_scenario_prototypes(const char *root, scenario_context *context, char *error, size_t error_size)
{
    asset_entry *entries = NULL;
    int status = enumerate_assets(root, &entries, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }
    for (int i = 0; i < acount(entries) && status == CHIBI_AUTOMATION_OK; i++) {
        const char *kind = entries[i].family->kind;
        if (!strcmp(kind, "field") || !strcmp(kind, "system")) {
            continue;
        }
        if (strcmp(entries[i].family->kind, "prototype")) {
            continue;
        }
        char *json = NULL;
        asset_ref ref;
        uint64_t hash;
        status = load_named_asset(root, entries[i].family, entries[i].name, &ref, &json, NULL, &hash, error, error_size);
        if (status != CHIBI_AUTOMATION_OK) {
            free(json);
            break;
        }
        char *normalized = scenario_prototype_text(json, hash);
        free(json);
        prototype *prototype = normalized
                                 ? prototype_load(normalized, &context->registry, error, error_size)
                                 : NULL;
        free(normalized);
        if (!prototype) {
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            break;
        }
        status = prototype_preview_register_prototype(context->preview, prototype) == 0
                   ? CHIBI_AUTOMATION_OK
                   : CHIBI_AUTOMATION_EXECUTION_ERROR;
        for (int j = 0; j < acount(prototype->graphs) && status == CHIBI_AUTOMATION_OK;
             j++) {
            status = register_preview_behavior_name(
                root,
                context->preview,
                prototype->graphs[j],
                error,
                error_size
            );
        }
        prototype_free(prototype);
    }
    context->runtime_assets = entries;
    context->runtime_hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < acount(entries); i++) {
        if (!scenario_runtime_asset(context, &entries[i])) {
            continue;
        }
        catalog_hash_text(&context->runtime_hash, entries[i].relative);
        char hash[17];
        snprintf(hash, sizeof hash, "%016llx", (unsigned long long)entries[i].hash);
        catalog_hash_text(&context->runtime_hash, hash);
    }
    /* Sounds are no catalog asset, but which ones loaded decides what
     * play_sound finds, so a replay checks them too. */
    const audio *audio = engine_audio(prototype_preview_runtime(context->preview));
    if (audio_sound_count(audio)) {
        char hash[17];
        snprintf(hash, sizeof hash, "%016llx", (unsigned long long)audio_library_hash(audio));
        catalog_hash_text(&context->runtime_hash, hash);
    }
    return status;
}

static int prepare_scenario(
    const char *root,
    const char *name,
    scenario_context *context,
    char *error,
    size_t error_size
)
{
    *context = (scenario_context){ 0 };
    const asset_family *prototype_family = family_by_kind("prototype");
    char *prototype_json = NULL;
    int status = load_named_asset(
        root,
        prototype_family,
        name,
        &context->prototype_ref,
        &prototype_json,
        &context->prototype_bytes,
        &context->prototype_hash,
        error,
        error_size
    );
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }

    context->includes = (prototype_includes){ .root = root };
    context->registry = registry_with_includes(&context->includes);
    char *scenario_json = scenario_prototype_text(prototype_json, context->prototype_hash);
    context->prototype = scenario_json
                           ? prototype_load(scenario_json, &context->registry, error, error_size)
                           : NULL;
    free(scenario_json);
    free(prototype_json);
    if (!context->prototype) {
        scenario_context_free(context);
        if (!error[0]) {
            set_error(error, error_size, "prototype validation failed");
        }
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }

    context->preview = prototype_preview_new(&context->registry);
    if (!context->preview) {
        scenario_context_free(context);
        set_error(error, error_size, "could not create prototype preview");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }

    int result = scenario_mount_content(root, context, error, error_size);
    if (result == CHIBI_AUTOMATION_OK) {
        result = register_shared_assets(root, context->preview, error, error_size);
    }
    if (result != CHIBI_AUTOMATION_OK) {
        scenario_context_free(context);
        return result;
    }

    for (int i = 0; i < acount(context->prototype->graphs); i++) {
        status = register_preview_behavior_name(
            root,
            context->preview,
            context->prototype->graphs[i],
            error,
            error_size
        );
        if (status != CHIBI_AUTOMATION_OK) {
            scenario_context_free(context);
            return status;
        }
    }
    status = register_scenario_prototypes(root, context, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(context);
    }
    return status;
}

/* Reads a scene document, applies its physics settings and readies the
 * entities it starts with. */
static int enter_scenario_scene(scenario_context *context, const char *json, char *error, size_t error_size)
{
    context->scene = scene_document_load(json, error, error_size);
    if (!context->scene) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    /* The document checked the settings, and no scene bodies remain. */
    int configured = prototype_preview_configure_physics(
        context->preview,
        context->scene->physics
    );
    assert(configured == 0);
    (void)configured;
    for (int i = 0; i < acount(context->scene->entities); i++) {
        scene_entity *entity = &context->scene->entities[i];
        asset_ref prototype_ref = { 0 };
        char *prototype_json = NULL;
        uint64_t prototype_hash = 0;
        int status = load_named_asset(
            context->root,
            family_by_kind("prototype"),
            entity->prototype,
            &prototype_ref,
            &prototype_json,
            NULL,
            &prototype_hash,
            error,
            error_size
        );
        if (status != CHIBI_AUTOMATION_OK) {
            free(prototype_json);
            return status;
        }
        char *scenario_json = scenario_prototype_text(prototype_json, prototype_hash);
        prototype *prototype = scenario_json
                                 ? prototype_load(scenario_json, &context->registry, error, error_size)
                                 : NULL;
        free(scenario_json);
        free(prototype_json);
        if (!prototype) {
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        if (prototype_preview_register_prototype(context->preview, prototype) != 0) {
            prototype_free(prototype);
            set_error(error, error_size, "could not register scene prototype");
            return CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
        apush(context->scene_prototypes, prototype);
        apush(
            context->scene_entities,
            ((prototype_preview_scene_entity){
                .name = entity->name,
                .prototype = prototype,
                .parent = entity->parent,
                .components = entity->components,
                .behaviors = (const char *const *)entity->behaviors,
                .behavior_count = acount(entity->behaviors),
                .own_behaviors = entity->own_behaviors,
            })
        );
        for (int behavior = 0; behavior < acount(prototype->graphs); behavior++) {
            status = register_preview_behavior_name(
                context->root,
                context->preview,
                prototype->graphs[behavior],
                error,
                error_size
            );
            if (status != CHIBI_AUTOMATION_OK) {
                return status;
            }
        }
        for (int behavior = 0; behavior < acount(entity->behaviors); behavior++) {
            status = register_preview_behavior_name(
                context->root,
                context->preview,
                entity->behaviors[behavior],
                error,
                error_size
            );
            if (status != CHIBI_AUTOMATION_OK) {
                return status;
            }
        }
    }
    return CHIBI_AUTOMATION_OK;
}

/* Load Scene can start any scene in the project, as in the player. */
static void list_scenario_scenes(scenario_context *context, const char *start)
{
    char **names = NULL;
    for (int i = 0; i < acount(context->runtime_assets); i++) {
        if (!strcmp(context->runtime_assets[i].family->kind, "scene")) {
            apush(names, asset_name_from_filename(context->runtime_assets[i].filename));
        }
    }
    scene_flow *flow = scenario_scenes(context);
    scene_flow_set(flow, (const char *const *)names, acount(names));
    scene_flow_enter(flow, start);
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

static int prepare_scene_scenario(
    const char *root,
    const char *name,
    scenario_context *context,
    char *error,
    size_t error_size
)
{
    *context = (scenario_context){ .scene_target = true };
    char *scene_json = NULL;
    int status = load_named_asset(
        root,
        family_by_kind("scene"),
        name,
        &context->prototype_ref,
        &scene_json,
        &context->prototype_bytes,
        &context->prototype_hash,
        error,
        error_size
    );
    if (status != CHIBI_AUTOMATION_OK) {
        free(scene_json);
        return status;
    }
    context->root = xstrdup(root);
    context->includes = (prototype_includes){ .root = context->root };
    context->registry = registry_with_includes(&context->includes);
    context->preview = prototype_preview_new(&context->registry);
    if (!context->preview) {
        free(scene_json);
        scenario_context_free(context);
        set_error(error, error_size, "could not create scene preview");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    status = scenario_mount_content(root, context, error, error_size);
    if (status == CHIBI_AUTOMATION_OK) {
        status = register_shared_assets(root, context->preview, error, error_size);
    }
    if (status == CHIBI_AUTOMATION_OK) {
        status = enter_scenario_scene(context, scene_json, error, error_size);
    }
    /* A scene a script loads may start empty; a scenario needs something to run. */
    if (status == CHIBI_AUTOMATION_OK && !acount(context->scene->entities)) {
        set_error(error, error_size, "scene has no entities");
        status = CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    free(scene_json);
    if (status == CHIBI_AUTOMATION_OK) {
        status = register_scenario_prototypes(root, context, error, error_size);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(context);
        return status;
    }
    /* A request may name the file, but the scene runs under its own name. */
    list_scenario_scenes(context, context->prototype_ref.name);
    return status;
}

static void add_behavior_failures(yyjson_mut_doc *doc, yyjson_mut_val *value, const scenario_context *context);

/* A request's `scenario` names a scenarios asset, which supplies the scene,
 * the frames the request leaves out, and the input the engine replays. NULL
 * with CHIBI_AUTOMATION_OK in `status` when the request names none. */
static scenario_document *request_scenario(
    const char *root,
    yyjson_val *request,
    int *status,
    char *error,
    size_t error_size
)
{
    *status = CHIBI_AUTOMATION_OK;
    const char *name = request_string(request, "scenario");
    if (!name) {
        return NULL;
    }
    if (yyjson_obj_get(request, "name") || yyjson_obj_get(request, "kind") ||
        yyjson_obj_get(request, "inputs") ||
        yyjson_obj_get(request, "input_timeline")) {
        set_error(error, error_size, "a scenario supplies the scene and its input; omit name, kind and inputs");
        *status = CHIBI_AUTOMATION_INVALID_REQUEST;
        return NULL;
    }
    asset_ref ref;
    char *json = NULL;
    size_t bytes;
    uint64_t hash;
    *status = load_named_asset(root, family_by_kind("scenario"), name, &ref, &json, &bytes, &hash, error, error_size);
    scenario_document *scenario = *status == CHIBI_AUTOMATION_OK
                                    ? scenario_document_load(json, error, error_size)
                                    : NULL;
    free(json);
    if (*status == CHIBI_AUTOMATION_OK && !scenario) {
        *status = CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    return scenario;
}

/* Replay a scenario's input from the launch the scenario runs next, starting
 * from its saved progress unless the request gives its own. */
static void replay_scenario_input(scenario_context *context, const scenario_document *scenario)
{
    if (scenario) {
        context->scenario_saved_state = scenario->saved_state;
        engine_replay_input(
            prototype_preview_runtime(context->preview),
            scenario->keys,
            acount(scenario->keys),
            scenario->seed
        );
    }
}

static int handle_scene_scenario(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *hash_parts = yyjson_obj_get(request, "hash_parts");
    if (hash_parts && !yyjson_is_bool(hash_parts)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "hash_parts must be a boolean");
        return -1;
    }
    char error[256] = { 0 };
    int status;
    scenario_document *scenario = request_scenario(root, request, &status, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    const char *name = scenario ? scenario->scene : request_string(request, "name");
    if (!name || !name[0]) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "scene name is required");
        return -1;
    }
    uint64_t frames = scenario ? (uint64_t)scenario->frames : SCENARIO_DEFAULT_FRAMES;
    yyjson_val *frames_value = yyjson_obj_get(request, "frames");
    if (frames_value && (!request_uint(request, "frames", &frames) ||
                         frames > SCENARIO_RUN_MAX_FRAMES)) {
        scenario_document_free(scenario);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "frames must be a non-negative integer no greater than 108000");
        return -1;
    }
    double fixed_dt = SCENARIO_FIXED_DT;
    yyjson_val *fixed_dt_value = yyjson_obj_get(request, "fixed_dt");
    if (fixed_dt_value) {
        if (!yyjson_is_num(fixed_dt_value) ||
            !isfinite(yyjson_get_num(fixed_dt_value)) ||
            fabs(yyjson_get_num(fixed_dt_value) - SCENARIO_FIXED_DT) > 1e-12) {
            scenario_document_free(scenario);
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "fixed_dt must equal the engine fixed timestep (1/60)");
            return -1;
        }
        fixed_dt = yyjson_get_num(fixed_dt_value);
    }
    scenario_context context = { 0 };
    const char *kind = request_string(request, "kind");
    status = scenario || (kind && !strcmp(kind, "scene"))
               ? prepare_scene_scenario(root, name, &context, error, sizeof error)
               : prepare_scenario(root, name, &context, error, sizeof error);
    if (status == CHIBI_AUTOMATION_OK) {
        replay_scenario_input(&context, scenario);
        status = advance_request_scenario(&context, request, frames, fixed_dt, error, sizeof error);
    }
    scenario_document_free(scenario);
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(&context);
        response_error(result, exit_code, status, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(
        result->doc,
        value,
        &context.prototype_ref,
        context.prototype_bytes,
        context.prototype_hash
    );
    yyjson_mut_obj_add_uint(result->doc, value, "frames", frames);
    yyjson_mut_obj_add_real(result->doc, value, "fixed_dt", fixed_dt);
    add_hash_named(result->doc, value, "world_hash", scenario_state_hash(&context));
    if (hash_parts && yyjson_get_bool(hash_parts)) {
        add_scenario_hash_parts(result->doc, value, &context);
    }
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "live_entities",
        ecs_world_live_count(prototype_preview_world(context.preview))
    );
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "authored_entities",
        prototype_preview_entity_count(context.preview)
    );
    add_scenario_timing(result->doc, value, frames, fixed_dt);
    add_saved_state(result->doc, value, &context);
    add_presentation_state(result->doc, value, &context);
    add_behavior_failures(result->doc, value, &context);
    scenario_context_free(&context);
    return 0;
}

/* Runs that stopped on an error, grouped by node and message, so a scenario
   that looks fine but aborts a Tick every frame says so. */
/* Each program retains its first failure until explicitly cleared. */
static void add_behavior_failures(yyjson_mut_doc *doc, yyjson_mut_val *value, const scenario_context *context)
{
    preview_world *preview = prototype_preview_live(context->preview);
    const authoring_sources *sources = preview_sources_applied(preview);
    yyjson_mut_val *failures = yyjson_mut_obj_add_arr(doc, value, "behavior_failures");
    int count = 0;
    for (int i = 0; sources && i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        execution_program *
            program = source_program(preview->runtime->sources, source->analysis->name);
        const execution_program_failure *error = program
                                                   ? execution_program_error(program)
                                                   : NULL;
        if (!error) {
            continue;
        }
        if (count++ == 16) {
            yyjson_mut_obj_add_bool(doc, value, "behavior_failures_truncated", true);
            break;
        }
        yyjson_mut_val *failure = yyjson_mut_arr_add_obj(doc, failures);
        yyjson_mut_obj_add_strcpy(doc, failure, "behavior", source->analysis->name);
        yyjson_mut_obj_add_strcpy(doc, failure, "message", error->message);
        yyjson_mut_obj_add_uint(doc, failure, "node", error->node_id);
        yyjson_mut_obj_add_uint(doc, failure, "function", error->function_id);
        yyjson_mut_obj_add_uint(doc, failure, "owner", error->owner);
        yyjson_mut_obj_add_int(doc, failure, "status", error->status);
        yyjson_mut_obj_add_int(doc, failure, "start", error->start);
        yyjson_mut_obj_add_int(doc, failure, "end", error->end);
        yyjson_mut_obj_add_str(doc, failure, "scope", "first retained program failure");
    }
}

static int handle_run_scenario(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *kind = request_string(request, "kind");
    if (kind && strcmp(kind, "prototype") && strcmp(kind, "scene")) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "run_scenario supports prototype or scene targets");
        return -1;
    }
    return handle_scene_scenario(root, request, result, exit_code);
}

#define RUNTIME_SNAPSHOT_MAX_ENTITIES 256
#define RUNTIME_SNAPSHOT_DEFAULT_ENTITIES 32
#define RUNTIME_SNAPSHOT_MAX_COMPONENTS 64
#define RUNTIME_SNAPSHOT_MAX_FIELDS 128
#define RUNTIME_SNAPSHOT_MAX_BEHAVIORS 64
#define RUNTIME_SNAPSHOT_MAX_LOGS 128

static bool runtime_snapshot_limit(
    yyjson_val *request,
    const char *key,
    uint64_t default_value,
    uint64_t maximum,
    uint64_t *out
)
{
    yyjson_val *value = yyjson_obj_get(request, key);
    if (!value) {
        *out = default_value;
        return true;
    }
    return request_uint(request, key, out) && *out > 0 && *out <= maximum;
}

static int launch_scenario(scenario_context *context, char *error, size_t error_size)
{
    char launch_error[256] = { 0 };
    int launch = context->scene_target
                   ? prototype_preview_launch_scene(
                         context->preview,
                         context->scene_entities,
                         acount(context->scene_entities),
                         launch_error,
                         sizeof launch_error
                     )
                   : prototype_preview_launch(context->preview, context->prototype, launch_error, sizeof launch_error);
    if (launch != ECS_OK) {
        set_error(error, error_size, launch_error[0] ? launch_error : "could not launch scenario");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
}

/* Starts the scene a script asked for during the last frame, as the player
 * does between frames: the clock, input and recordings go on. */
static int follow_scenario_scene(scenario_context *context, char *error, size_t error_size)
{
    char *next = context->scene_target ? scene_flow_take(scenario_scenes(context))
                                       : NULL;
    if (!next) {
        return CHIBI_AUTOMATION_OK;
    }
    char *json = NULL;
    asset_ref ref;
    char reason[256] = { 0 };
    int status = load_named_asset(
        context->root,
        family_by_kind("scene"),
        next,
        &ref,
        &json,
        NULL,
        NULL,
        reason,
        sizeof reason
    );
    if (status == CHIBI_AUTOMATION_OK) {
        /* Begin runs inside start, so the new name must be current first. */
        scene_flow_enter(scenario_scenes(context), next);
        prototype_preview_end_scene(context->preview);
        clear_scenario_scene(context);
        status = enter_scenario_scene(context, json, reason, sizeof reason);
    }
    if (status == CHIBI_AUTOMATION_OK && prototype_preview_start_scene(
                                             context->preview,
                                             context->scene_entities,
                                             acount(context->scene_entities),
                                             reason,
                                             sizeof reason
                                         ) != ECS_OK) {
        status = CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    if (status != CHIBI_AUTOMATION_OK) {
        snprintf(error, error_size, "scene %s: %s", next, reason[0] ? reason : "could not start");
    }
    context->loads += status == CHIBI_AUTOMATION_OK;
    free(json);
    free(next);
    return status;
}

static int tick_scenario(scenario_context *context, double now, char *error, size_t error_size)
{
    if (prototype_preview_tick(context->preview, now) != 0) {
        const char *message = engine_error(prototype_preview_runtime(context->preview));
        set_error(error, error_size, message ? message : "scenario frame failed");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    return follow_scenario_scene(context, error, error_size);
}

static int advance_scenario(
    scenario_context *context,
    uint64_t frames,
    double fixed_dt,
    char *error,
    size_t error_size
)
{
    int status = launch_scenario(context, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }
    /* Like the player's first frame, this one runs no step and follows no
     * scene. The player begins its start scene in the next frame's step,
     * with its first Tick, and follows a scene Begin asked for after that. */
    if (prototype_preview_tick(context->preview, 0) != 0) {
        set_error(error, error_size, "could not initialize scenario clock");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    for (uint64_t frame = 0; frame < frames && status == CHIBI_AUTOMATION_OK; frame++) {
        status = tick_scenario(context, (double)(frame + 1) * fixed_dt, error, error_size);
    }
    return status;
}

#define SCENARIO_PROFILE_MAX_TOTAL SCENARIO_RUN_MAX_FRAMES
#define SCENARIO_PROFILE_WORST_FRAMES 5

typedef struct
{
    uint64_t frames;
    uint64_t steps;
    uint64_t command_apply_count;
    double dropped_seconds;
    double behavior_ms;
    double physics_ms;
    double events_ms;
    double systems_ms;
    double rendering_ms;
    physics_profile physics_profile;
} scenario_profile_timing;

typedef struct
{
    uint64_t calls;
    uint64_t requested_bytes;
} scenario_profile_allocations;

/* One sampled frame, so hitches remain visible beside the totals. */
typedef struct
{
    int frame;
    int steps;
    double behavior_ms;
    double physics_ms;
    double systems_ms;
    double rendering_ms;
    physics_profile physics_profile;
} scenario_profile_frame;

static void add_box2d_profile(yyjson_mut_doc *doc, yyjson_mut_val *object, const physics_box2d_profile *profile)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "box2d_ms");
#define ADD_PROFILE(name, field)                                               \
    yyjson_mut_obj_add_real(doc, value, name, profile->field)
    ADD_PROFILE("step", step_ms);
    ADD_PROFILE("pairs", pairs_ms);
    ADD_PROFILE("collide", collide_ms);
    ADD_PROFILE("solve", solve_ms);
    ADD_PROFILE("merge_islands", merge_islands_ms);
    ADD_PROFILE("prepare_stages", prepare_stages_ms);
    ADD_PROFILE("solve_constraints", solve_constraints_ms);
    ADD_PROFILE("prepare_constraints", prepare_constraints_ms);
    ADD_PROFILE("integrate_velocities", integrate_velocities_ms);
    ADD_PROFILE("warm_start", warm_start_ms);
    ADD_PROFILE("solve_impulses", solve_impulses_ms);
    ADD_PROFILE("integrate_positions", integrate_positions_ms);
    ADD_PROFILE("relax_impulses", relax_impulses_ms);
    ADD_PROFILE("apply_restitution", apply_restitution_ms);
    ADD_PROFILE("store_impulses", store_impulses_ms);
    ADD_PROFILE("split_islands", split_islands_ms);
    ADD_PROFILE("transforms", transforms_ms);
    ADD_PROFILE("hit_events", hit_events_ms);
    ADD_PROFILE("refit", refit_ms);
    ADD_PROFILE("bullets", bullets_ms);
    ADD_PROFILE("sleep_islands", sleep_islands_ms);
    ADD_PROFILE("sensors", sensors_ms);
#undef ADD_PROFILE
}

static void sum_box2d_profile(physics_box2d_profile *total, const physics_box2d_profile *sample)
{
#define SUM_PROFILE(field) total->field += sample->field
    SUM_PROFILE(step_ms);
    SUM_PROFILE(pairs_ms);
    SUM_PROFILE(collide_ms);
    SUM_PROFILE(solve_ms);
    SUM_PROFILE(merge_islands_ms);
    SUM_PROFILE(prepare_stages_ms);
    SUM_PROFILE(solve_constraints_ms);
    SUM_PROFILE(prepare_constraints_ms);
    SUM_PROFILE(integrate_velocities_ms);
    SUM_PROFILE(warm_start_ms);
    SUM_PROFILE(solve_impulses_ms);
    SUM_PROFILE(integrate_positions_ms);
    SUM_PROFILE(relax_impulses_ms);
    SUM_PROFILE(apply_restitution_ms);
    SUM_PROFILE(store_impulses_ms);
    SUM_PROFILE(split_islands_ms);
    SUM_PROFILE(transforms_ms);
    SUM_PROFILE(hit_events_ms);
    SUM_PROFILE(refit_ms);
    SUM_PROFILE(bullets_ms);
    SUM_PROFILE(sleep_islands_ms);
    SUM_PROFILE(sensors_ms);
#undef SUM_PROFILE
}

static void add_physics_profile(yyjson_mut_doc *doc, yyjson_mut_val *object, const physics_profile *profile)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "physics_detail_ms");
    yyjson_mut_obj_add_real(doc, value, "synchronize", profile->synchronize_ms);
    yyjson_mut_obj_add_real(doc, value, "solve", profile->solve_ms);
    yyjson_mut_obj_add_real(doc, value, "sweeps", profile->sweeps_ms);
    yyjson_mut_obj_add_real(doc, value, "pairs", profile->pairs_ms);
    yyjson_mut_obj_add_real(doc, value, "writeback", profile->writeback_ms);
    yyjson_mut_obj_add_real(doc, value, "dispatch", profile->dispatch_ms);
    yyjson_mut_obj_add_real(doc, value, "contact_collection", profile->contact_collection_ms);
    yyjson_mut_obj_add_real(doc, value, "contact_enqueue", profile->contact_enqueue_ms);
    yyjson_mut_obj_add_real(doc, value, "contact_join", profile->contact_join_ms);
    yyjson_mut_obj_add_real(doc, value, "contact_merge", profile->contact_merge_ms);
    yyjson_mut_obj_add_real(doc, value, "position_clamp", profile->position_clamp_ms);
    yyjson_mut_obj_add_real(doc, value, "fluid", profile->fluid_ms);
    yyjson_mut_obj_add_real(doc, value, "fluid_prepare", profile->fluid_prepare_ms);
    yyjson_mut_obj_add_real(doc, value, "material_tick", profile->material_tick_ms);
    yyjson_mut_obj_add_real(doc, value, "fluid_impulse", profile->fluid_impulse_ms);
    yyjson_mut_val *material_fluid = yyjson_mut_obj_add_obj(doc, value, "material_fluid_ms");
    yyjson_mut_obj_add_real(doc, material_fluid, "mpm", profile->material_fluid.mpm_ms);
    yyjson_mut_obj_add_real(
        doc,
        material_fluid,
        "reactions",
        profile->material_fluid.reactions_ms
    );
    yyjson_mut_obj_add_real(
        doc,
        material_fluid,
        "gas_flow",
        profile->material_fluid.gas_flow_ms
    );
    yyjson_mut_obj_add_real(
        doc,
        material_fluid,
        "index",
        profile->material_fluid.index_ms
    );
    yyjson_mut_obj_add_real(
        doc,
        material_fluid,
        "density",
        profile->material_fluid.density_ms
    );
    yyjson_mut_obj_add_real(
        doc,
        material_fluid,
        "relaxation",
        profile->material_fluid.relaxation_ms
    );
    yyjson_mut_obj_add_real(
        doc,
        material_fluid,
        "collisions",
        profile->material_fluid.collisions_ms
    );
    yyjson_mut_obj_add_real(
        doc,
        material_fluid,
        "collision_index",
        profile->material_fluid.collision_index_ms
    );
    yyjson_mut_val *work = yyjson_mut_obj_add_obj(doc, object, "material_fluid_work");
    yyjson_mut_obj_add_uint(doc, work, "index_builds", profile->material_fluid.index_builds);
    yyjson_mut_obj_add_uint(doc, work, "mpm_substeps", profile->material_fluid.mpm_substeps);
    yyjson_mut_obj_add_uint(
        doc,
        work,
        "mpm_limited_steps",
        profile->material_fluid.mpm_limited_steps
    );
    yyjson_mut_val *physics_work = yyjson_mut_obj_add_obj(doc, object, "physics_work");
    yyjson_mut_obj_add_uint(doc, physics_work, "body_creations", profile->body_creations);
    yyjson_mut_obj_add_uint(doc, physics_work, "shape_creations", profile->shape_creations);
    yyjson_mut_obj_add_uint(doc, physics_work, "shape_updates", profile->shape_updates);
    yyjson_mut_obj_add_uint(doc, physics_work, "shape_removals", profile->shape_removals);
    yyjson_mut_obj_add_uint(doc, physics_work, "mass_updates", profile->mass_updates);
    add_box2d_profile(doc, value, &profile->box2d);
}

static void sum_physics_profile(physics_profile *total, const physics_profile *sample)
{
    total->synchronize_ms += sample->synchronize_ms;
    total->solve_ms += sample->solve_ms;
    total->sweeps_ms += sample->sweeps_ms;
    total->pairs_ms += sample->pairs_ms;
    total->writeback_ms += sample->writeback_ms;
    total->dispatch_ms += sample->dispatch_ms;
    total->contact_collection_ms += sample->contact_collection_ms;
    total->contact_enqueue_ms += sample->contact_enqueue_ms;
    total->contact_join_ms += sample->contact_join_ms;
    total->contact_merge_ms += sample->contact_merge_ms;
    total->position_clamp_ms += sample->position_clamp_ms;
    total->fluid_ms += sample->fluid_ms;
    total->fluid_prepare_ms += sample->fluid_prepare_ms;
    total->material_tick_ms += sample->material_tick_ms;
    total->fluid_impulse_ms += sample->fluid_impulse_ms;
    total->material_fluid.reactions_ms += sample->material_fluid.reactions_ms;
    total->material_fluid.gas_flow_ms += sample->material_fluid.gas_flow_ms;
    total->material_fluid.index_ms += sample->material_fluid.index_ms;
    total->material_fluid.density_ms += sample->material_fluid.density_ms;
    total->material_fluid.relaxation_ms += sample->material_fluid.relaxation_ms;
    total->material_fluid.collisions_ms += sample->material_fluid.collisions_ms;
    total->material_fluid.collision_index_ms += sample->material_fluid.collision_index_ms;
    total->material_fluid.mpm_ms += sample->material_fluid.mpm_ms;
    total->material_fluid.index_builds += sample->material_fluid.index_builds;
    total->material_fluid.mpm_substeps += sample->material_fluid.mpm_substeps;
    total->material_fluid.mpm_limited_steps += sample->material_fluid.mpm_limited_steps;
    total->body_creations += sample->body_creations;
    total->shape_creations += sample->shape_creations;
    total->shape_updates += sample->shape_updates;
    total->shape_removals += sample->shape_removals;
    total->mass_updates += sample->mass_updates;
    sum_box2d_profile(&total->box2d, &sample->box2d);
}

static double frame_total_ms(const scenario_profile_frame *frame)
{
    return frame->behavior_ms + frame->physics_ms + frame->systems_ms + frame->rendering_ms;
}

static void profile_add_timing(
    scenario_profile_timing *total,
    scenario_profile_frame **frames,
    const engine_frame_timing *timing,
    const physics_profile *physics,
    uint64_t previous_steps,
    uint64_t previous_commands,
    double previous_dropped,
    xalloc_stats previous_allocations,
    scenario_profile_allocations *allocations
)
{
    xalloc_stats current_allocations;
    xalloc_stats_get(&current_allocations);
    apush(
        *frames,
        ((scenario_profile_frame){
            .frame = (int)total->frames,
            .steps = (int)(timing->simulation_steps - previous_steps),
            .behavior_ms = timing->behavior_ms,
            .physics_ms = timing->physics_ms,
            .systems_ms = timing->systems_ms,
            .rendering_ms = timing->rendering_ms,
            .physics_profile = *physics,
        })
    );
    sum_physics_profile(&total->physics_profile, physics);
    total->frames++;
    total->steps += timing->simulation_steps - previous_steps;
    total->command_apply_count += timing->command_apply_count - previous_commands;
    total->dropped_seconds += timing->dropped_seconds - previous_dropped;
    total->behavior_ms += timing->behavior_ms;
    total->physics_ms += timing->physics_ms;
    total->events_ms += timing->events_ms;
    total->systems_ms += timing->systems_ms;
    total->rendering_ms += timing->rendering_ms;
    allocations->calls += current_allocations.calls - previous_allocations.calls;
    allocations->requested_bytes += current_allocations.requested_bytes -
                                    previous_allocations.requested_bytes;
}

static int compare_frame_cost(const void *left, const void *right)
{
    double a = frame_total_ms(left), b = frame_total_ms(right);
    if (a != b) {
        return a > b ? -1 : 1;
    }
    return ((const scenario_profile_frame *)left)->frame -
           ((const scenario_profile_frame *)right)->frame;
}

/* Nearest-rank percentile of frames sorted from most to least costly. */
static double costliest_percentile(const scenario_profile_frame *sorted, int count, double percentile)
{
    int rank = (int)ceil(percentile / 100 * count);
    return frame_total_ms(&sorted[count - (rank < 1 ? 1 : rank)]);
}

static void add_profile_frames(yyjson_mut_doc *doc, yyjson_mut_val *timing, scenario_profile_frame *frames)
{
    int count = acount(frames);
    if (count > 1) {
        qsort(frames, (size_t)count, sizeof *frames, compare_frame_cost);
    }
    yyjson_mut_val *distribution = yyjson_mut_obj_add_obj(doc, timing, "frame_ms");
    if (count) {
        yyjson_mut_obj_add_real(doc, distribution, "p50", costliest_percentile(frames, count, 50));
        yyjson_mut_obj_add_real(doc, distribution, "p95", costliest_percentile(frames, count, 95));
        yyjson_mut_obj_add_real(doc, distribution, "p99", costliest_percentile(frames, count, 99));
        yyjson_mut_obj_add_real(doc, distribution, "max", frame_total_ms(&frames[0]));
    }
    yyjson_mut_val *worst = yyjson_mut_obj_add_arr(doc, timing, "worst_frames");
    for (int i = 0; i < count && i < SCENARIO_PROFILE_WORST_FRAMES; i++) {
        yyjson_mut_val *value = yyjson_mut_arr_add_obj(doc, worst);
        yyjson_mut_obj_add_int(doc, value, "frame", frames[i].frame);
        yyjson_mut_obj_add_int(doc, value, "simulation_steps", frames[i].steps);
        yyjson_mut_obj_add_real(doc, value, "total_ms", frame_total_ms(&frames[i]));
        yyjson_mut_obj_add_real(doc, value, "behavior_ms", frames[i].behavior_ms);
        yyjson_mut_obj_add_real(doc, value, "physics_ms", frames[i].physics_ms);
        yyjson_mut_obj_add_real(doc, value, "systems_ms", frames[i].systems_ms);
        yyjson_mut_obj_add_real(doc, value, "rendering_ms", frames[i].rendering_ms);
        add_physics_profile(doc, value, &frames[i].physics_profile);
        yyjson_mut_obj_add_int(
            doc,
            value,
            "physics_bodies",
            frames[i].physics_profile.bodies
        );
        yyjson_mut_obj_add_int(
            doc,
            value,
            "physics_pairs",
            frames[i].physics_profile.pairs
        );
    }
}

static void add_profile_timing(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const scenario_profile_timing *timing,
    scenario_profile_frame *frames
)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "timing");
    yyjson_mut_obj_add_uint(doc, value, "sampled_frames", timing->frames);
    yyjson_mut_obj_add_uint(doc, value, "simulation_steps", timing->steps);
    yyjson_mut_obj_add_uint(doc, value, "command_apply_count", timing->command_apply_count);
    yyjson_mut_obj_add_real(doc, value, "dropped_seconds", timing->dropped_seconds);
    yyjson_mut_obj_add_real(doc, value, "behavior_ms", timing->behavior_ms);
    yyjson_mut_obj_add_real(doc, value, "physics_ms", timing->physics_ms);
    yyjson_mut_obj_add_real(doc, value, "events_ms", timing->events_ms);
    yyjson_mut_obj_add_real(doc, value, "systems_ms", timing->systems_ms);
    yyjson_mut_obj_add_real(doc, value, "rendering_ms", timing->rendering_ms);
    add_physics_profile(doc, value, &timing->physics_profile);
    double total_ms = timing->behavior_ms + timing->physics_ms +
                      timing->systems_ms + timing->rendering_ms;
    yyjson_mut_obj_add_real(doc, value, "total_ms", total_ms);
    yyjson_mut_obj_add_real(
        doc,
        value,
        "average_frame_ms",
        timing->frames ? total_ms / timing->frames : 0
    );
    add_profile_frames(doc, value, frames);
}

static void add_profile_allocations(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const scenario_profile_allocations *allocations,
    uint64_t frames
)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "allocations");
    yyjson_mut_obj_add_strcpy(doc, value, "scope", "xalloc wrappers");
    yyjson_mut_obj_add_uint(doc, value, "sampled_frames", frames);
    yyjson_mut_obj_add_uint(doc, value, "calls", allocations->calls);
    yyjson_mut_obj_add_uint(doc, value, "requested_bytes", allocations->requested_bytes);
    yyjson_mut_obj_add_real(
        doc,
        value,
        "average_calls_per_frame",
        frames ? (double)allocations->calls / (double)frames : 0
    );
    yyjson_mut_obj_add_real(
        doc,
        value,
        "average_requested_bytes_per_frame",
        frames ? (double)allocations->requested_bytes / (double)frames : 0
    );
}

typedef struct
{
    const char *name;
    bool pressed;
} scenario_action;

typedef struct
{
    double movement_x;
    double movement_y;
    double aim_x;
    double aim_y;
    bool aim_firing;
    bool aim_point;
    bool has_aim, has_movement;
    bool has_pointer;
    input_pointer_ray pointer;
    yyjson_val *events;
    scenario_action actions[SCENARIO_MAX_ACTIONS];
    int action_count;
    bool has_controls, has_controllers;
    uint64_t controls;
    input_controller_state controllers[INPUT_CONTROLLER_MAX];
    yyjson_val *ui;
} scenario_input;

typedef struct
{
    scenario_input *inputs;
    uint64_t *at;
    size_t count, next;
    uint64_t frame;
    bool sampled;
} scenario_sequence;

static void free_sequence(scenario_sequence *sequence);
static int prepare_sequence(
    yyjson_val *request,
    uint64_t frames,
    scenario_sequence *sequence,
    char *error,
    size_t error_size
);
static int step_sequence(
    scenario_context *context,
    scenario_sequence *sequence,
    double fixed_dt,
    char *error,
    size_t error_size
);

static void add_source_profile(
    yyjson_mut_doc *doc,
    yyjson_mut_val *value,
    const source_registry *registry,
    bool enabled,
    bool registered
)
{
    yyjson_mut_val *profile = yyjson_mut_obj_add_obj(doc, value, "source_profile");
    yyjson_mut_obj_add_bool(doc, profile, "available", enabled);
    yyjson_mut_obj_add_bool(doc, profile, "enabled", enabled);
    yyjson_mut_obj_add_bool(doc, profile, "registered", registered);
    yyjson_mut_obj_add_uint(doc, profile, "dropped_samples", source_profile_dropped(registry));
    yyjson_mut_obj_add_int(doc, profile, "row_limit", SOURCE_PROFILE_ROWS_MAX);
    yyjson_mut_obj_add_str(doc, profile, "scope", "actual function activation entries and resumes; wait polling excluded");
    yyjson_mut_obj_add_str(doc, profile, "timing", "inclusive process CPU time; nested samples overlap and synchronous outlined calls remain in their parent sample");
    yyjson_mut_val *rows = yyjson_mut_obj_add_arr(doc, profile, "rows");
    for (int i = 0; i < source_profile_count(registry); i++) {
        const source_profile_row *sample = source_profile_at(registry, i);
        yyjson_mut_val *row = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, row, "module_id", sample->module_id);
        yyjson_mut_obj_add_uint(doc, row, "function_id", sample->function_id);
        yyjson_mut_obj_add_uint(doc, row, "owner", sample->owner);
        yyjson_mut_obj_add_strcpy(doc, row, "name", sample->name);
        yyjson_mut_obj_add_bool(doc, row, "internal", sample->internal);
        if (!sample->internal) {
            yyjson_mut_obj_add_int(doc, row, "kind", sample->kind);
        }
        if (sample->source) {
            yyjson_mut_obj_add_strcpy(doc, row, "source", sample->source);
            yyjson_mut_obj_add_int(doc, row, "start", sample->start);
            yyjson_mut_obj_add_int(doc, row, "end", sample->end);
            yyjson_mut_obj_add_int(doc, row, "line", sample->line);
            yyjson_mut_obj_add_int(doc, row, "column", sample->column);
        }
        yyjson_mut_obj_add_uint(doc, row, "entries", sample->entries);
        yyjson_mut_obj_add_uint(doc, row, "resumes", sample->resumes);
        yyjson_mut_obj_add_int(doc, row, "last_status", sample->last_status);
        yyjson_mut_obj_add_bool(doc, row, "cpu_available", sample->cpu_available);
        if (sample->cpu_available) {
            yyjson_mut_obj_add_real(doc, row, "cpu_seconds", sample->cpu_seconds);
        }
        yyjson_mut_arr_append(rows, row);
    }
}

static int profile_scenario(
    const char *root,
    yyjson_val *request,
    const scenario_document *scenario,
    response *result,
    int *exit_code
)
{
    yyjson_val *details_value = yyjson_obj_get(request, "behavior_details");
    if (details_value && !yyjson_is_bool(details_value)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "behavior_details must be a boolean");
        return -1;
    }
    bool behavior_details = !details_value || yyjson_get_bool(details_value);
    const char *kind = scenario ? "scene" : request_string(request, "kind");
    if (!kind) {
        kind = "prototype";
    }
    if (strcmp(kind, "prototype") && strcmp(kind, "prototypes") &&
        strcmp(kind, "scene") && strcmp(kind, "scenes")) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "profile_scenario supports prototype or scene targets");
        return -1;
    }
    const char *name = scenario ? scenario->scene : request_string(request, "name");
    if (!name || !name[0]) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "profile target name is required");
        return -1;
    }
    uint64_t frames = scenario ? (uint64_t)scenario->frames : SCENARIO_DEFAULT_FRAMES;
    uint64_t warmup = 0;
    if ((yyjson_obj_get(request, "frames") && !request_uint(request, "frames", &frames)) ||
        (yyjson_obj_get(request, "warmup_frames") &&
         !request_uint(request, "warmup_frames", &warmup)) ||
        warmup > SCENARIO_PROFILE_MAX_TOTAL ||
        frames > SCENARIO_PROFILE_MAX_TOTAL - warmup) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "frames and warmup_frames must be non-negative integers totaling at most 108000");
        return -1;
    }
    const uint64_t total_frames = warmup + frames;
    const bool sampled_inputs = yyjson_obj_get(request, "inputs") ||
                                yyjson_obj_get(request, "input_timeline");
    scenario_sequence sequence = { 0 };
    double fixed_dt = SCENARIO_FIXED_DT;
    yyjson_val *fixed_dt_value = yyjson_obj_get(request, "fixed_dt");
    if (fixed_dt_value) {
        if (!yyjson_is_num(fixed_dt_value) ||
            !isfinite(yyjson_get_num(fixed_dt_value)) ||
            fabs(yyjson_get_num(fixed_dt_value) - SCENARIO_FIXED_DT) > 1e-12) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "fixed_dt must equal the engine fixed timestep (1/60)");
            return -1;
        }
        fixed_dt = yyjson_get_num(fixed_dt_value);
    }
    if (sampled_inputs) {
        char input_error[256] = { 0 };
        int input_status = prepare_sequence(request, total_frames, &sequence, input_error, sizeof input_error);
        if (input_status != CHIBI_AUTOMATION_OK) {
            free_sequence(&sequence);
            response_error(result, exit_code, input_status, input_error);
            return -1;
        }
    }
    scenario_context context = { 0 };
    char error[256] = { 0 };
    bool scene = !strcmp(kind, "scene") || !strcmp(kind, "scenes");
    int status = scene
                   ? prepare_scene_scenario(root, name, &context, error, sizeof error)
                   : prepare_scenario(root, name, &context, error, sizeof error);
    if (status == CHIBI_AUTOMATION_OK) {
        replay_scenario_input(&context, scenario);
        status = scenario_saved_state(&context, request, error, sizeof error);
    }
    scenario_profile_timing timing = { 0 };
    scenario_profile_frame *samples = NULL;
    scenario_profile_allocations allocations = { 0 };
    if (status == CHIBI_AUTOMATION_OK) {
        status = launch_scenario(&context, error, sizeof error);
    }
    if (status == CHIBI_AUTOMATION_OK) {
        physics_set_profiling(prototype_preview_live(context.preview)->runtime->physics, true);
    }
    if (status == CHIBI_AUTOMATION_OK &&
        prototype_preview_tick(context.preview, 0) != 0) {
        snprintf(error, sizeof error, "could not initialize scenario clock");
        status = CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    uint64_t previous_steps = 0;
    uint64_t previous_commands = 0;
    double previous_dropped = 0;
    xalloc_stats previous_allocations = { 0 };
    if (status == CHIBI_AUTOMATION_OK) {
        const engine_frame_timing *initial = engine_last_timing(
            prototype_preview_runtime(context.preview)
        );
        previous_steps = initial->simulation_steps;
        previous_commands = initial->command_apply_count;
        previous_dropped = initial->dropped_seconds;
        xalloc_stats_get(&previous_allocations);
        for (uint64_t frame = 0; frame < total_frames; frame++) {
            if (behavior_details && frame == warmup) {
                source_profile_enable(prototype_preview_sources(context.preview), true);
            }
            int step_status = CHIBI_AUTOMATION_OK;
            if (sampled_inputs) {
                step_status = step_sequence(&context, &sequence, fixed_dt, error, sizeof error);
            } else {
                step_status = tick_scenario(&context, (double)(frame + 1) * fixed_dt, error, sizeof error);
            }
            if (step_status != CHIBI_AUTOMATION_OK) {
                status = step_status;
                break;
            }
            if (frame < warmup) {
                const engine_frame_timing *sample = engine_last_timing(
                    prototype_preview_runtime(context.preview)
                );
                previous_steps = sample->simulation_steps;
                previous_commands = sample->command_apply_count;
                previous_dropped = sample->dropped_seconds;
                xalloc_stats_get(&previous_allocations);
                continue;
            }
            const engine *runtime = prototype_preview_runtime(context.preview);
            const engine_frame_timing *sample = engine_last_timing(runtime);
            profile_add_timing(
                &timing,
                &samples,
                sample,
                physics_last_profile(
                    prototype_preview_live(context.preview)->runtime->physics
                ),
                previous_steps,
                previous_commands,
                previous_dropped,
                previous_allocations,
                &allocations
            );
            previous_steps = sample->simulation_steps;
            previous_commands = sample->command_apply_count;
            previous_dropped = sample->dropped_seconds;
            xalloc_stats_get(&previous_allocations);
        }
    }
    if (status != CHIBI_AUTOMATION_OK) {
        afree(samples);
        free_sequence(&sequence);
        scenario_context_free(&context);
        response_error(result, exit_code, status, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(
        result->doc,
        value,
        &context.prototype_ref,
        context.prototype_bytes,
        context.prototype_hash
    );
    yyjson_mut_obj_add_uint(result->doc, value, "frames", frames);
    yyjson_mut_obj_add_bool(result->doc, value, "behavior_details", behavior_details);
    yyjson_mut_obj_add_uint(result->doc, value, "warmup_frames", warmup);
    yyjson_mut_obj_add_real(result->doc, value, "fixed_dt", fixed_dt);
    add_hash_named(result->doc, value, "world_hash", scenario_state_hash(&context));
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "live_entities",
        ecs_world_live_count(prototype_preview_world(context.preview))
    );
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "authored_entities",
        prototype_preview_entity_count(context.preview)
    );
    behavior_execution_status execution = { .ready = true };
    int source_modules = collect_source_execution(
        result->doc,
        value,
        prototype_preview_live(context.preview),
        &execution
    );
    add_behavior_execution_status(result->doc, value, &execution, false);
    source_registry *source_registry = prototype_preview_sources(context.preview);
    add_source_profile(result->doc, value, source_registry, behavior_details, source_modules > 0);
    source_profile_enable(source_registry, false);
    add_profile_timing(result->doc, value, &timing, samples);
    add_profile_allocations(result->doc, value, &allocations, timing.frames);
    afree(samples);
    free_sequence(&sequence);
    scenario_context_free(&context);
    return 0;
}

static int handle_profile_scenario(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    char error[256] = { 0 };
    int status;
    scenario_document *scenario = request_scenario(root, request, &status, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    int reply = profile_scenario(root, request, scenario, result, exit_code);
    scenario_document_free(scenario);
    return reply;
}

typedef struct
{
    uint64_t world_hash[SCENARIO_MAX_FRAMES];
    uint32_t live_entities[SCENARIO_MAX_FRAMES];
    bool has_live_entities;
} scenario_observations;

static bool scenario_input_number(yyjson_val *object, const char *key, double *out, char *error, size_t error_size)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    if (!value) {
        return true;
    }
    if (!yyjson_is_num(value) || !isfinite(yyjson_get_num(value)) ||
        fabs(yyjson_get_num(value)) > FLT_MAX) {
        snprintf(error, error_size, "%s must be a finite number within float range", key);
        return false;
    }
    *out = yyjson_get_num(value);
    return true;
}

static bool scenario_input_bool(yyjson_val *object, const char *key, bool *out, char *error, size_t error_size)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    if (!value) {
        return true;
    }
    if (!yyjson_is_bool(value)) {
        snprintf(error, error_size, "%s must be a boolean", key);
        return false;
    }
    *out = yyjson_get_bool(value);
    return true;
}

static bool add_scenario_action(
    scenario_input *input,
    yyjson_val *value,
    const char *name,
    char *error,
    size_t error_size
)
{
    if (!name || !name[0]) {
        snprintf(error, error_size, "scenario action name is required");
        return false;
    }
    if (input->action_count == SCENARIO_MAX_ACTIONS) {
        snprintf(error, error_size, "scenario inputs support at most %d actions", SCENARIO_MAX_ACTIONS);
        return false;
    }
    for (int i = 0; i < input->action_count; i++) {
        if (!strcmp(input->actions[i].name, name)) {
            snprintf(error, error_size, "scenario action is repeated: %s", name);
            return false;
        }
    }
    bool pressed = false;
    if (yyjson_is_bool(value)) {
        pressed = yyjson_get_bool(value);
    } else if (yyjson_is_obj(value)) {
        if (yyjson_obj_get(value, "value")) {
            snprintf(error, error_size, "analog scenario action values are not supported");
            return false;
        }
        if (!yyjson_obj_get(value, "pressed") ||
            !scenario_input_bool(value, "pressed", &pressed, error, error_size)) {
            snprintf(error, error_size, "scenario action %s must contain pressed", name);
            return false;
        }
    } else {
        snprintf(error, error_size, "scenario action %s must contain pressed", name);
        return false;
    }
    input->actions[input->action_count++] = (scenario_action){ name, pressed };
    return true;
}

static bool parse_scenario_action_array(yyjson_val *actions, scenario_input *input, char *error, size_t error_size)
{
    if (yyjson_arr_size(actions) > SCENARIO_MAX_ACTIONS) {
        snprintf(error, error_size, "scenario inputs support at most %d actions", SCENARIO_MAX_ACTIONS);
        return false;
    }
    size_t count = yyjson_arr_size(actions);
    for (size_t i = 0; i < count; i++) {
        yyjson_val *action = yyjson_arr_get(actions, i);
        const char *name = action && yyjson_is_obj(action)
                             ? request_string(action, "name")
                             : NULL;
        yyjson_val *pressed = action && yyjson_is_obj(action)
                                ? yyjson_obj_get(action, "pressed")
                                : NULL;
        if (!name || !pressed || yyjson_obj_get(action, "value") ||
            !add_scenario_action(input, pressed, name, error, error_size)) {
            if (!error[0]) {
                snprintf(error, error_size, "scenario actions require name and pressed");
            }
            return false;
        }
    }
    return true;
}

static ecs_entity scenario_entity(const scenario_context *context, const char *name)
{
    if (!name) {
        return prototype_preview_entity(context->preview);
    }
    if (!context->scene_target) {
        return (ecs_entity){ 0 };
    }
    int index = scene_document_entity_index(context->scene, name);
    return index < 0 ? (ecs_entity){ 0 }
                     : prototype_preview_entity_at(context->preview, index);
}

static bool scenario_event_value(
    arena *storage,
    const scenario_context *context,
    yyjson_val *item,
    pin_value *out
)
{
    if (!yyjson_is_obj(item)) {
        return false;
    }
    const char *type = request_string(item, "type");
    yyjson_val *type_value = yyjson_obj_get(item, "type");
    yyjson_val *value = yyjson_obj_get(item, "value");
    if (!type || !value || memchr(type, 0, yyjson_get_len(type_value))) {
        return false;
    }
    if (!strcmp(type, "bool") && yyjson_is_bool(value)) {
        *out = (pin_value){ .type = PIN_BOOL, .b = yyjson_get_bool(value) };
        return true;
    }
    if ((!strcmp(type, "name") || !strcmp(type, "string")) &&
        yyjson_is_str(value) && yyjson_get_len(value) <= 4096) {
        const char *bytes = yyjson_get_str(value);
        size_t length = yyjson_get_len(value);
        if (!strcmp(type, "string")) {
            return !pin_value_text(storage, out, bytes, length);
        }
        if (memchr(bytes, 0, length)) {
            return false;
        }
        *out = (pin_value){ .type = PIN_NAME, .str = bytes };
        return true;
    }
    if (!strcmp(type, "entity") && yyjson_is_str(value) && yyjson_get_len(value) &&
        !memchr(yyjson_get_str(value), 0, yyjson_get_len(value))) {
        ecs_entity entity = context
                              ? scenario_entity(context, yyjson_get_str(value))
                              : (ecs_entity){ 0 };
        if (context &&
            !ecs_entity_is_alive(prototype_preview_world(context->preview), entity)) {
            return false;
        }
        *out = (pin_value){ .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .u64 = ecs_entity_to_u64(entity) };
        return true;
    }
    if (!yyjson_is_num(value) || !isfinite(yyjson_get_num(value))) {
        return false;
    }
    double number = yyjson_get_num(value);
    if ((!strcmp(type, "int") || !strcmp(type, "i32")) &&
        yyjson_is_int(value) && number >= INT32_MIN && number <= INT32_MAX) {
        *out = (pin_value){ .type = PIN_I32, .i = (int32_t)number };
        return true;
    }
    if (!strcmp(type, "u32") && yyjson_is_int(value) && number >= 0 &&
        number <= UINT32_MAX) {
        *out = (pin_value){ .type = PIN_U32, .u = (uint32_t)number };
        return true;
    }
    if ((!strcmp(type, "number") || !strcmp(type, "f32")) && fabs(number) <= FLT_MAX) {
        *out = (pin_value){ .type = PIN_F32, .f = (float)number };
        return true;
    }
    if (!strcmp(type, "f64")) {
        *out = (pin_value){ .type = PIN_F64, .d = number };
        return true;
    }
    return false;
}

static bool parse_scenario_controllers(yyjson_val *value, scenario_input *out, char *error, size_t size)
{
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) > INPUT_CONTROLLER_MAX) {
        snprintf(error, size, "controllers must contain at most four slot states");
        return false;
    }
    out->has_controllers = true;
    for (size_t i = 0; i < yyjson_arr_size(value); i++) {
        yyjson_val *state = yyjson_arr_get(value, i);
        yyjson_val *connected = yyjson_obj_get(state, "connected");
        yyjson_val *buttons = yyjson_obj_get(state, "buttons");
        if (!yyjson_is_obj(state) || !yyjson_is_bool(connected) ||
            yyjson_obj_size(state) != 1 + (buttons != NULL) ||
            (buttons &&
             (!yyjson_is_arr(buttons) || yyjson_arr_size(buttons) >= PAD_COUNT))) {
            snprintf(error, size, "controller slots need connected and optional buttons");
            return false;
        }
        out->controllers[i].connected = yyjson_get_bool(connected);
        for (size_t j = 0; j < yyjson_arr_size(buttons); j++) {
            yyjson_val *button = yyjson_arr_get(buttons, j);
            int code = 0;
            if (yyjson_is_str(button)) {
                type_catalog_enum_value("PadControl", yyjson_get_str(button), &code);
            } else if (yyjson_is_int(button)) {
                int64_t number = yyjson_get_sint(button);
                if (number > PAD_NONE && number < PAD_COUNT) {
                    code = (int)number;
                }
            }
            if (!out->controllers[i].connected || code <= PAD_NONE || code >= PAD_COUNT) {
                snprintf(error, size, "controller buttons need a connected slot and PadControl labels or values");
                return false;
            }
            out->controllers[i].buttons |= UINT32_C(1) << code;
        }
    }
    return true;
}

static bool scenario_vector3(yyjson_val *value, CF_V3 *out)
{
    yyjson_val *x = yyjson_obj_get(value, "x"), *y = yyjson_obj_get(value, "y"),
               *z = yyjson_obj_get(value, "z");
    if (!yyjson_is_obj(value) || yyjson_obj_size(value) != 3 ||
        !yyjson_is_num(x) || !yyjson_is_num(y) || !yyjson_is_num(z)) {
        return false;
    }
    double vx = yyjson_get_num(x), vy = yyjson_get_num(y), vz = yyjson_get_num(z);
    if (!isfinite(vx) || !isfinite(vy) || !isfinite(vz) || fabs(vx) > FLT_MAX ||
        fabs(vy) > FLT_MAX || fabs(vz) > FLT_MAX) {
        return false;
    }
    *out = (CF_V3){ (float)vx, (float)vy, (float)vz };
    return true;
}

static bool scenario_pointer(yyjson_val *value, input_pointer_ray *out)
{
    if (yyjson_is_null(value)) {
        return true;
    }
    if (!yyjson_is_obj(value) || yyjson_obj_size(value) != 2 ||
        !scenario_vector3(yyjson_obj_get(value, "origin"), &out->origin) ||
        !scenario_vector3(yyjson_obj_get(value, "direction"), &out->direction)) {
        return false;
    }
    CF_V3 d = out->direction;
    out->active = d.x || d.y || d.z;
    return out->active;
}

static bool parse_scenario_input(yyjson_val *value, scenario_input *out, char *error, size_t error_size)
{
    if (!value || !yyjson_is_obj(value)) {
        snprintf(error, error_size, "each scenario input must be an object");
        return false;
    }
    *out = (scenario_input){ 0 };
    yyjson_val *pointer = yyjson_obj_get(value, "pointer");
    out->has_pointer = pointer != NULL;
    if (pointer && !scenario_pointer(pointer, &out->pointer)) {
        snprintf(error, error_size, "pointer needs finite origin and nonzero direction x/y/z vectors, or null");
        return false;
    }
    yyjson_val *movement = yyjson_obj_get(value, "movement");
    out->has_movement = movement != NULL;
    if (movement &&
        (!yyjson_is_obj(movement) || !yyjson_obj_get(movement, "x") ||
         !yyjson_obj_get(movement, "y") ||
         !scenario_input_number(movement, "x", &out->movement_x, error, error_size) ||
         !scenario_input_number(movement, "y", &out->movement_y, error, error_size))) {
        if (!error[0]) {
            snprintf(error, error_size, "movement must contain numeric x and y values");
        }
        return false;
    }
    yyjson_val *aim = yyjson_obj_get(value, "aim");
    if (aim &&
        (!yyjson_is_obj(aim) || !yyjson_obj_get(aim, "x") || !yyjson_obj_get(aim, "y") ||
         !scenario_input_number(aim, "x", &out->aim_x, error, error_size) ||
         !scenario_input_number(aim, "y", &out->aim_y, error, error_size) ||
         !scenario_input_bool(aim, "firing", &out->aim_firing, error, error_size) ||
         !scenario_input_bool(aim, "point", &out->aim_point, error, error_size))) {
        if (!error[0]) {
            snprintf(error, error_size, "aim must contain numeric x and y values and boolean firing and point values");
        }
        return false;
    }
    if (fabs(out->movement_x) > 1 || fabs(out->movement_y) > 1 ||
        (!out->aim_point && (fabs(out->aim_x) > 1 || fabs(out->aim_y) > 1))) {
        snprintf(error, error_size, "movement and directional aim must be within [-1, 1]");
        return false;
    }
    out->has_aim = aim != NULL;
    yyjson_val *actions = yyjson_obj_get(value, "actions");
    if (actions && yyjson_is_arr(actions)) {
        if (!parse_scenario_action_array(actions, out, error, error_size)) {
            return false;
        }
    } else if (actions) {
        snprintf(error, error_size, "actions must be an array");
        return false;
    }
    yyjson_val *events = yyjson_obj_get(value, "events");
    if (events) {
        if (!yyjson_is_arr(events) || yyjson_arr_size(events) > 32) {
            snprintf(error, error_size, "events must contain at most 32 named events");
            return false;
        }
        size_t count = yyjson_arr_size(events);
        for (size_t i = 0; i < count; i++) {
            yyjson_val *event = yyjson_arr_get(events, i);
            const char *name = event && yyjson_is_obj(event)
                                 ? request_string(event, "name")
                                 : NULL;
            yyjson_val *values = event && yyjson_is_obj(event)
                                   ? yyjson_obj_get(event, "values")
                                   : NULL;
            yyjson_val *name_value = event ? yyjson_obj_get(event, "name") : NULL;
            if (!name || !name[0] || yyjson_get_len(name_value) > 128 ||
                memchr(name, 0, yyjson_get_len(name_value)) ||
                (values && (!yyjson_is_arr(values) || yyjson_arr_size(values) > 16))) {
                snprintf(error, error_size, "events require a bounded name and at most 16 typed payload values");
                return false;
            }
            yyjson_val *target = yyjson_obj_get(event, "target");
            if (target && (!yyjson_is_str(target) || !yyjson_get_len(target) ||
                           yyjson_get_len(target) > 512 ||
                           memchr(yyjson_get_str(target), 0, yyjson_get_len(target)))) {
                snprintf(error, error_size, "event target must be a scene entity name");
                return false;
            }
            for (size_t j = 0; j < yyjson_arr_size(values); j++) {
                arena storage = { 0 };
                pin_value parsed;
                bool valid = scenario_event_value(&storage, NULL, yyjson_arr_get(values, j), &parsed);
                arena_free(&storage);
                if (!valid) {
                    snprintf(error, error_size, "event payload requires a supported type and matching value");
                    return false;
                }
            }
        }
    }
    yyjson_val *controls = yyjson_obj_get(value, "controls");
    if (controls) {
        if (!yyjson_is_arr(controls) || yyjson_arr_size(controls) >= CONTROL_COUNT) {
            snprintf(error, error_size, "controls must be an array of Control values");
            return false;
        }
        out->has_controls = true;
        for (size_t i = 0; i < yyjson_arr_size(controls); i++) {
            yyjson_val *source = yyjson_arr_get(controls, i);
            int64_t code = yyjson_get_sint(source);
            if (!yyjson_is_int(source) || code <= CONTROL_NONE || code >= CONTROL_COUNT) {
                snprintf(error, error_size, "control code is outside Control");
                return false;
            }
            out->controls |= UINT64_C(1) << code;
        }
    }
    yyjson_val *controllers = yyjson_obj_get(value, "controllers");
    if (controllers &&
        !parse_scenario_controllers(controllers, out, error, error_size)) {
        return false;
    }
    yyjson_val *ui = yyjson_obj_get(value, "ui");
    if (ui) {
        if (!yyjson_is_arr(ui) || yyjson_arr_size(ui) > 32) {
            snprintf(error, error_size, "ui must contain at most 32 interactions");
            return false;
        }
        for (size_t i = 0; i < yyjson_arr_size(ui); i++) {
            yyjson_val *action = yyjson_arr_get(ui, i);
            const char *fields[] = { request_string(action, "target"),
                                     request_string(action, "element"),
                                     request_string(action, "event") };
            bool named = true;
            for (size_t f = 0; f < sizeof fields / sizeof *fields; f++) {
                named &= fields[f] && fields[f][0] && strlen(fields[f]) <= 512;
            }
            double number = 0;
            if (!named ||
                !scenario_input_number(action, "value", &number, error, error_size)) {
                snprintf(error, error_size, "ui requires a target, element and event, and an optional numeric value");
                return false;
            }
        }
    }
    out->ui = ui;
    out->events = events;
    return true;
}

static yyjson_mut_val *add_scenario_input(yyjson_mut_doc *doc, yyjson_mut_val *array, const scenario_input *input)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    if (input->has_controls) {
        yyjson_mut_val *controls = yyjson_mut_obj_add_arr(doc, object, "controls");
        for (int i = 1; i < CONTROL_COUNT; i++) {
            if (input->controls & (UINT64_C(1) << i)) {
                yyjson_mut_arr_add_int(doc, controls, i);
            }
        }
    }
    if (input->has_controllers) {
        yyjson_mut_val *controllers = yyjson_mut_obj_add_arr(doc, object, "controllers");
        for (int i = 0; i < INPUT_CONTROLLER_MAX; i++) {
            yyjson_mut_val *state = yyjson_mut_arr_add_obj(doc, controllers);
            yyjson_mut_obj_add_bool(
                doc,
                state,
                "connected",
                input->controllers[i].connected
            );
            yyjson_mut_val *buttons = yyjson_mut_obj_add_arr(doc, state, "buttons");
            for (int button = PAD_NONE + 1; button < PAD_COUNT; button++) {
                if (input->controllers[i].buttons & (UINT32_C(1) << button)) {
                    yyjson_mut_arr_add_int(doc, buttons, button);
                }
            }
        }
    }
    if (input->ui) {
        yyjson_mut_obj_add_val(doc, object, "ui", yyjson_val_mut_copy(doc, input->ui));
    }
    if (input->has_movement) {
        yyjson_mut_val *movement = yyjson_mut_obj_add_obj(doc, object, "movement");
        yyjson_mut_obj_add_real(doc, movement, "x", input->movement_x);
        yyjson_mut_obj_add_real(doc, movement, "y", input->movement_y);
    }
    if (input->has_aim) {
        yyjson_mut_val *aim = yyjson_mut_obj_add_obj(doc, object, "aim");
        yyjson_mut_obj_add_real(doc, aim, "x", input->aim_x);
        yyjson_mut_obj_add_real(doc, aim, "y", input->aim_y);
        yyjson_mut_obj_add_bool(doc, aim, "firing", input->aim_firing);
        yyjson_mut_obj_add_bool(doc, aim, "point", input->aim_point);
    }
    if (input->has_pointer) {
        if (input->pointer.active) {
            yyjson_mut_val *pointer = yyjson_mut_obj_add_obj(doc, object, "pointer");
            yyjson_mut_val *origin = yyjson_mut_obj_add_obj(doc, pointer, "origin");
            yyjson_mut_obj_add_real(doc, origin, "x", input->pointer.origin.x);
            yyjson_mut_obj_add_real(doc, origin, "y", input->pointer.origin.y);
            yyjson_mut_obj_add_real(doc, origin, "z", input->pointer.origin.z);
            yyjson_mut_val *direction = yyjson_mut_obj_add_obj(doc, pointer, "direction");
            yyjson_mut_obj_add_real(
                doc,
                direction,
                "x",
                input->pointer.direction.x
            );
            yyjson_mut_obj_add_real(
                doc,
                direction,
                "y",
                input->pointer.direction.y
            );
            yyjson_mut_obj_add_real(
                doc,
                direction,
                "z",
                input->pointer.direction.z
            );
        } else {
            yyjson_mut_obj_add_null(doc, object, "pointer");
        }
    }
    yyjson_mut_val *actions = yyjson_mut_obj_add_arr(doc, object, "actions");
    for (int i = 0; i < input->action_count; i++) {
        yyjson_mut_val *action = yyjson_mut_arr_add_obj(doc, actions);
        yyjson_mut_obj_add_strcpy(doc, action, "name", input->actions[i].name);
        yyjson_mut_obj_add_bool(doc, action, "pressed", input->actions[i].pressed);
    }
    if (input->events && yyjson_arr_size(input->events)) {
        yyjson_mut_obj_add_val(
            doc,
            object,
            "events",
            yyjson_val_mut_copy(doc, input->events)
        );
    }
    return object;
}

static int apply_scenario_input(
    scenario_context *context,
    const scenario_input *input,
    char *error,
    size_t error_size
)
{
    engine *runtime = prototype_preview_runtime(context->preview);
    if (input->has_movement &&
        engine_sample_movement(runtime, (float)input->movement_x, (float)input->movement_y) <
            0) {
        snprintf(error, error_size, "scenario movement input failed");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    if (input->has_aim) {
        context->aim = (CF_V2){ (float)input->aim_x, (float)input->aim_y };
        context->aim_point = input->aim_point;
        context->aim_firing = input->aim_firing;
    }
    if (input->has_pointer) {
        context->pointer = input->pointer;
    }
    if (engine_sample_pointer_ray(runtime, context->pointer) < 0) {
        snprintf(error, error_size, "scenario pointer input failed");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    if (input->has_controls) {
        context->controls = input->controls;
        context->devices_sampled = true;
    }
    if (input->has_controllers) {
        memcpy(context->controllers, input->controllers, sizeof context->controllers);
        context->devices_sampled = true;
    }
    bool firing = false;
    if (context->devices_sampled &&
        engine_sample_devices(runtime, context->controls, context->controllers, &firing) <
            0) {
        snprintf(error, error_size, "invalid authored action binding");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    if (engine_sample_aim(
            runtime,
            context->aim,
            context->aim_firing || firing,
            context->aim_point
        ) < 0) {
        snprintf(error, error_size, "scenario aim input failed");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    /* A UI activation as the presenter delivers it: a value first, then the
       event the element emits to its Ui entity. */
    for (size_t i = 0; i < yyjson_arr_size(input->ui); i++) {
        yyjson_val *action = yyjson_arr_get(input->ui, i);
        ecs_entity target = scenario_entity(context, request_string(action, "target"));
        yyjson_val *value = yyjson_obj_get(action, "value");
        if (!Ui_has(prototype_preview_world(context->preview), target)) {
            snprintf(error, error_size, "UI target must be an entity with Ui");
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        if (value) {
            ui_set_value(
                engine_ui(runtime),
                ecs_entity_to_u64(target),
                request_string(action, "element"),
                (float)yyjson_get_num(value)
            );
        }
        entities_request_event(
            prototype_preview_entities(context->preview),
            target,
            request_string(action, "event"),
            (execution_arguments){ 0 }
        );
    }
    preview_runtime *behaviors = prototype_preview_live(context->preview)->runtime;
    engine_input_failure flow = { 0 };
    if (input->events) {
        size_t count = yyjson_arr_size(input->events);
        for (size_t i = 0; i < count; i++) {
            yyjson_val *event = yyjson_arr_get(input->events, i);
            const char *name = request_string(event, "name");
            ecs_entity entity = scenario_entity(context, request_string(event, "target"));
            if (!ecs_entity_is_alive(prototype_preview_world(context->preview), entity)) {
                snprintf(error, error_size, "event target is missing or no longer alive");
                return CHIBI_AUTOMATION_INVALID_REQUEST;
            }
            yyjson_val *values = yyjson_obj_get(event, "values");
            arena storage = { 0 };
            pin_value payload[16];
            int value_count = (int)yyjson_arr_size(values);
            for (int j = 0; j < value_count; j++) {
                if (!scenario_event_value(
                        &storage,
                        context,
                        yyjson_arr_get(values, (size_t)j),
                        &payload[j]
                    )) {
                    arena_free(&storage);
                    snprintf(error, error_size, "event payload contains an unresolved entity");
                    return CHIBI_AUTOMATION_INVALID_REQUEST;
                }
            }
            if (source_dispatch(
                    behaviors->sources,
                    entity,
                    name,
                    payload,
                    value_count,
                    behaviors->now,
                    behaviors->delta
                ) < 0) {
                arena_free(&storage);
                snprintf(error, error_size, "%s", source_error(behaviors->sources));
                return CHIBI_AUTOMATION_EXECUTION_ERROR;
            }
            arena_free(&storage);
        }
    }

    for (int i = 0; i < input->action_count; i++) {
        flow = (engine_input_failure){ 0 };
        if (prototype_preview_input(
                context->preview,
                input->actions[i].name,
                input->actions[i].pressed ? 1 : 0,
                input->actions[i].pressed ? 1 : 0,
                &flow
            ) < 0) {
            snprintf(error, error_size, "%s", flow.message ? flow.message : "scenario action failed");
            return CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
    }
    return CHIBI_AUTOMATION_OK;
}

static int run_scenario_inputs(
    scenario_context *context,
    const scenario_input *inputs,
    uint64_t frames,
    double fixed_dt,
    scenario_observations *observations,
    char *error,
    size_t error_size
)
{
    int launch = advance_scenario(context, 0, fixed_dt, error, error_size);
    if (launch != CHIBI_AUTOMATION_OK) {
        return launch;
    }
    for (uint64_t frame = 0; frame < frames; frame++) {
        int status = apply_scenario_input(context, &inputs[frame], error, error_size);
        if (status == CHIBI_AUTOMATION_OK) {
            status = tick_scenario(context, (double)(frame + 1) * fixed_dt, error, error_size);
        }
        if (status != CHIBI_AUTOMATION_OK) {
            return status;
        }
        observations->world_hash[frame] = scenario_state_hash(context);
        observations->live_entities[frame] = ecs_world_live_count(
            prototype_preview_world(context->preview)
        );
    }
    observations->has_live_entities = true;
    return CHIBI_AUTOMATION_OK;
}

static void free_sequence(scenario_sequence *sequence)
{
    free(sequence->inputs);
    free(sequence->at);
}

static int prepare_sequence(
    yyjson_val *request,
    uint64_t frames,
    scenario_sequence *sequence,
    char *error,
    size_t error_size
)
{
    yyjson_val *values = yyjson_obj_get(request, "inputs");
    yyjson_val *timeline = yyjson_obj_get(request, "input_timeline");
    sequence->sampled = values || timeline;
    if (!values && !timeline) {
        return CHIBI_AUTOMATION_OK;
    }
    yyjson_val *entries = timeline ? timeline : values;
    size_t count = yyjson_arr_size(entries);
    if ((values && timeline) || !yyjson_is_arr(entries) ||
        count > SCENARIO_MAX_FRAMES || count > frames) {
        set_error(error, error_size, "Use inputs or input_timeline with at most 1800 entries and no more entries than frames");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    sequence->count = count;
    sequence->inputs = xcalloc(count + 1, sizeof *sequence->inputs);
    sequence->at = xcalloc(count + 1, sizeof *sequence->at);
    for (size_t i = 0; i < count; i++) {
        yyjson_val *entry = yyjson_arr_get(entries, i);
        sequence->at[i] = i;
        bool valid = !timeline || (request_uint(entry, "frame", &sequence->at[i]) &&
                                   sequence->at[i] < frames &&
                                   (!i || sequence->at[i] > sequence->at[i - 1]));
        if (!valid || !parse_scenario_input(
                          timeline ? yyjson_obj_get(entry, "input") : entry,
                          &sequence->inputs[i],
                          error,
                          error_size
                      )) {
            if (!valid) {
                set_error(error, error_size, "Timeline frames must be unique, increasing and below frames");
            }
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
    }
    return CHIBI_AUTOMATION_OK;
}

static int step_sequence(
    scenario_context *context,
    scenario_sequence *sequence,
    double fixed_dt,
    char *error,
    size_t error_size
)
{
    const scenario_input *sample = sequence->next < sequence->count &&
                                           sequence->at[sequence->next] ==
                                               sequence->frame
                                     ? &sequence->inputs[sequence->next++]
                                     : &(scenario_input){ 0 };
    int status = sequence->sampled
                   ? apply_scenario_input(context, sample, error, error_size)
                   : CHIBI_AUTOMATION_OK;
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }
    return tick_scenario(context, (double)(++sequence->frame) * fixed_dt, error, error_size);
}

static int advance_request_scenario(
    scenario_context *context,
    yyjson_val *request,
    uint64_t frames,
    double fixed_dt,
    char *error,
    size_t error_size
)
{
    int status = scenario_saved_state(context, request, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        return status;
    }
    /* Preserve the ordinary scenario path when no input was requested. */
    if (!yyjson_obj_get(request, "inputs") &&
        !yyjson_obj_get(request, "input_timeline")) {
        return advance_scenario(context, frames, fixed_dt, error, error_size);
    }
    scenario_sequence sequence = { 0 };
    status = prepare_sequence(request, frames, &sequence, error, error_size);
    if (status == CHIBI_AUTOMATION_OK) {
        status = advance_scenario(context, 0, fixed_dt, error, error_size);
    }
    while (status == CHIBI_AUTOMATION_OK && sequence.frame < frames) {
        status = step_sequence(context, &sequence, fixed_dt, error, error_size);
    }
    free_sequence(&sequence);
    return status;
}

struct chibi_preview_run
{
    scenario_context context;
    scenario_sequence sequence;
    declared_components *declared;
    yyjson_doc *request;
    char *page;       /* the project's default root ui document, or NULL */
    bool perspective; /* at launch */
};

prototype_preview *chibi_preview_runtime(chibi_preview_run *run)
{
    return run->context.preview;
}

const char *chibi_preview_page(const chibi_preview_run *run)
{
    const scene_document *scene = run->context.scene;
    return scene && scene->ui ? scene->ui : run->page;
}

bool chibi_preview_perspective(const chibi_preview_run *run)
{
    return run->context.scene ? run->context.scene->perspective : run->perspective;
}

int chibi_preview_loads(const chibi_preview_run *run)
{
    return run->context.loads;
}

int chibi_preview_step(chibi_preview_run *run, char *error, size_t size)
{
    return step_sequence(&run->context, &run->sequence, 1.0 / 60, error, size);
}

char *chibi_preview_state(chibi_preview_run *run)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *state = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, state);
    add_hash_named(doc, state, "world_hash", scenario_state_hash(&run->context));
    yyjson_mut_obj_add_uint(doc, state, "frame", run->sequence.frame);
    add_presentation_state(doc, state, &run->context);
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

void chibi_preview_free(chibi_preview_run *run)
{
    if (!run) {
        return;
    }
    free_sequence(&run->sequence);
    scenario_context_free(&run->context);
    declared_components_free(run->declared);
    yyjson_doc_free(run->request);
    free(run->page);
    free(run);
}

int chibi_automation_preview(
    const char *root,
    const char *request,
    chibi_preview_run **out,
    char *error,
    size_t error_size
)
{
    *out = NULL;
    yyjson_doc *doc = yyjson_read(request, strlen(request), 0);
    yyjson_val *value = doc ? yyjson_doc_get_root(doc) : NULL;
    const char *kind = request_string(value, "kind"),
               *name = request_string(value, "name");
    yyjson_val *frame_value = yyjson_obj_get(value, "frames");
    uint64_t frames = frame_value ? yyjson_get_uint(frame_value) : 1;
    if (!kind || (strcmp(kind, "scene") && strcmp(kind, "prototype")) ||
        !name || !name[0] ||
        (frame_value && (!yyjson_is_int(frame_value) || yyjson_get_sint(frame_value) < 0 ||
                         frames > SCENARIO_MAX_FRAMES))) {
        set_error(error, error_size, "preview_scene requires scene/prototype kind, name and 0-1800 frames");
        yyjson_doc_free(doc);
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (!open_content(root, error, error_size)) {
        yyjson_doc_free(doc);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    chibi_preview_run *run = xcalloc(1, sizeof *run);
    run->request = doc;
    if (!load_declared(root, &run->declared, error, error_size)) {
        chibi_preview_free(run);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    scenario_context *context = &run->context;
    /* Preparing registers the shader libraries, materials and shapes the
       renderer draws with, as every scenario shares them. */
    int status = !strcmp(kind, "scene")
                   ? prepare_scene_scenario(root, name, context, error, error_size)
                   : prepare_scenario(root, name, context, error, error_size);
    if (status == CHIBI_AUTOMATION_OK) {
        uint64_t rendered = yyjson_get_uint(yyjson_obj_get(value, "render_frames"));
        uint64_t total = frames +
                         (rendered > 2 && !yyjson_get_bool(yyjson_obj_get(value, "freeze_simulation"))
                              ? rendered
                              : 0);
        status = prepare_sequence(value, total, &run->sequence, error, error_size);
        if (status == CHIBI_AUTOMATION_OK) {
            status = scenario_saved_state(context, value, error, error_size);
        }
        if (status == CHIBI_AUTOMATION_OK) {
            status = advance_scenario(context, 0, 1.0 / 60, error, error_size);
        }
        while (status == CHIBI_AUTOMATION_OK && run->sequence.frame < frames) {
            status = chibi_preview_step(run, error, error_size);
        }
    }
    if (status == CHIBI_AUTOMATION_OK) {
        run->page = manifest_string(root, "ui");
        run->perspective = context->scene
                             ? context->scene->perspective
                             : Geometry3D_has(
                                   prototype_preview_world(context->preview),
                                   prototype_preview_entity(context->preview)
                               );
        *out = run;
        return status;
    }
    chibi_preview_free(run);
    return status;
}

static bool safe_recording_path(const char *path)
{
    if (!path ||
        strncmp(path, SCENARIO_RECORDING_PATH_PREFIX, strlen(SCENARIO_RECORDING_PATH_PREFIX)) !=
            0 ||
        !path[strlen(SCENARIO_RECORDING_PATH_PREFIX)] ||
        strchr(path + strlen(SCENARIO_RECORDING_PATH_PREFIX), '/')) {
        return false;
    }
    char *resolved = realpath(path, NULL);
    const char *resolved_prefix = "/private" SCENARIO_RECORDING_PATH_PREFIX;
    const char *resolved_name = resolved ? strrchr(resolved, '/') : NULL;
    const char *path_name = strrchr(path, '/');
    bool safe = resolved && resolved_name && path_name &&
                (!strncmp(resolved, SCENARIO_RECORDING_PATH_PREFIX, strlen(SCENARIO_RECORDING_PATH_PREFIX)) ||
                 !strncmp(resolved, resolved_prefix, strlen(resolved_prefix))) &&
                !strcmp(resolved_name, path_name);
    free(resolved);
    return safe;
}

/* Recordings serve the replays and reads that follow them; a day on, they
   are removed instead of accumulating in /tmp. */
static void prune_recordings()
{
    enum
    {
        RECORDING_LIFETIME = 24 * 60 * 60
    };
    const char *prefix = strrchr(SCENARIO_RECORDING_PATH_PREFIX, '/') + 1;
    DIR *dir = opendir("/tmp");
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        char path[PATH_MAX];
        struct stat info;
        if (strncmp(entry->d_name, prefix, strlen(prefix)) ||
            snprintf(path, sizeof path, "/tmp/%s", entry->d_name) >= (int)sizeof path ||
            lstat(path, &info) || !S_ISREG(info.st_mode) ||
            time(NULL) - info.st_mtime < RECORDING_LIFETIME) {
            continue;
        }
        unlink(path);
    }
    if (dir) {
        closedir(dir);
    }
}

static int create_recording_path(char *path, size_t path_size, char *error, size_t error_size)
{
    prune_recordings();
    char template[] = SCENARIO_RECORDING_PATH_PREFIX "XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        snprintf(error, error_size, "could not create recording artifact: %s", strerror(errno));
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    close(fd);
    if (snprintf(path, path_size, "%s", template) >= (int)path_size) {
        unlink(template);
        snprintf(error, error_size, "recording artifact path is too long");
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
}

static int write_recording_artifact(const char *path, yyjson_mut_val *recording, char *error, size_t error_size)
{
    size_t bytes = 0;
    char *json = yyjson_mut_val_write(recording, 0, &bytes);
    if (!json || bytes > SCENARIO_RECORDING_MAX_BYTES) {
        free(json);
        unlink(path);
        snprintf(error, error_size, "recording artifact exceeds the supported size");
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        free(json);
        unlink(path);
        snprintf(error, error_size, "could not write recording artifact: %s", strerror(errno));
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    bool ok = fwrite(json, 1, bytes, file) == bytes;
    if (fclose(file) != 0) {
        ok = false;
    }
    free(json);
    if (!ok) {
        unlink(path);
        snprintf(error, error_size, "could not write recording artifact");
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
}

static yyjson_doc *read_recording_artifact(const char *path, char *error, size_t error_size)
{
    if (!safe_recording_path(path)) {
        snprintf(error, error_size, "recording_path must refer to a generated /tmp recording artifact");
        return NULL;
    }
    struct stat info = { 0 };
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        (uintmax_t)info.st_size > SCENARIO_RECORDING_MAX_BYTES) {
        snprintf(error, error_size, "recording artifact is missing or exceeds the supported size");
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "could not read recording artifact: %s", strerror(errno));
        return NULL;
    }
    size_t bytes = (size_t)info.st_size;
    char *json = xmalloc(bytes + 1);
    bool ok = fread(json, 1, bytes, file) == bytes;
    fclose(file);
    if (!ok) {
        free(json);
        snprintf(error, error_size, "could not read recording artifact");
        return NULL;
    }
    json[bytes] = '\0';
    yyjson_doc *document = yyjson_read(json, bytes, 0);
    free(json);
    if (!document || !yyjson_is_obj(yyjson_doc_get_root(document))) {
        yyjson_doc_free(document);
        snprintf(error, error_size, "recording artifact is not a JSON object");
        return NULL;
    }
    return document;
}

static yyjson_mut_val *add_recording(
    yyjson_mut_doc *doc,
    yyjson_mut_val *parent,
    const scenario_context *context,
    const scenario_input *inputs,
    const scenario_observations *observations,
    uint64_t frames,
    double fixed_dt,
    const char *artifact_path
)
{
    yyjson_mut_val *recording = yyjson_mut_obj_add_obj(doc, parent, "recording");
    yyjson_mut_obj_add_strcpy(doc, recording, "format", SCENARIO_RECORDING_FORMAT);
    yyjson_mut_obj_add_uint(doc, recording, "version", SCENARIO_RECORDING_VERSION);
    yyjson_mut_obj_add_uint(doc, recording, "contract_version", CHIBI_AUTOMATION_CONTRACT);
    const char *id = artifact_path
                       ? artifact_path + strlen(SCENARIO_RECORDING_PATH_PREFIX)
                       : "";
    yyjson_mut_obj_add_strcpy(doc, recording, "id", id);
    yyjson_mut_obj_add_strcpy(doc, recording, "kind", context->scene_target ? "scene" : "prototype");
    yyjson_mut_obj_add_strcpy(doc, recording, "name", context->prototype_ref.name);
    add_hash_named(doc, recording, "content_hash", context->prototype_hash);
    add_hash_named(doc, recording, "runtime_content_hash", context->runtime_hash);
    yyjson_doc *saved = yyjson_read(context->initial_saved_state, strlen(context->initial_saved_state), 0);
    yyjson_mut_obj_add_val(
        doc,
        recording,
        "saved_state",
        yyjson_val_mut_copy(doc, yyjson_doc_get_root(saved))
    );
    yyjson_doc_free(saved);
    yyjson_mut_val *assets = yyjson_mut_obj_add_arr(doc, recording, "assets");
    for (int i = 0; i < acount(context->runtime_assets); i++) {
        if (scenario_runtime_asset(context, &context->runtime_assets[i])) {
            add_asset_entry(doc, assets, &context->runtime_assets[i]);
        }
    }
    yyjson_mut_obj_add_uint(doc, recording, "frames", frames);
    yyjson_mut_obj_add_real(doc, recording, "fixed_dt", fixed_dt);
    if (artifact_path) {
        yyjson_mut_obj_add_strcpy(doc, recording, "artifact_path", artifact_path);
    }
    yyjson_mut_val *input_values = yyjson_mut_obj_add_arr(doc, recording, "inputs");
    for (uint64_t frame = 0; frame < frames; frame++) {
        add_scenario_input(doc, input_values, &inputs[frame]);
    }
    yyjson_mut_val *frame_hashes = yyjson_mut_obj_add_arr(doc, recording, "frame_hashes");
    yyjson_mut_val *live_entities = yyjson_mut_obj_add_arr(doc, recording, "live_entities");
    for (uint64_t frame = 0; frame < frames; frame++) {
        char frame_hash[32];
        snprintf(
            frame_hash,
            sizeof frame_hash,
            "%016llx",
            (unsigned long long)observations->world_hash[frame]
        );
        yyjson_mut_arr_add_strcpy(doc, frame_hashes, frame_hash);
        yyjson_mut_arr_add_uint(doc, live_entities, observations->live_entities[frame]);
    }
    if (frames) {
        add_hash_named(doc, recording, "world_hash", observations->world_hash[frames - 1]);
    } else {
        add_hash_named(doc, recording, "world_hash", 0);
    }
    return recording;
}

static bool recording_hash(yyjson_val *object, const char *key, uint64_t *out)
{
    const char *text = request_string(object, key);
    if (!text || !text[0]) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 16);
    if (errno || end == text || *end || strlen(text) > 16) {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool recording_hash_value(yyjson_val *value, uint64_t *out)
{
    if (!value || !yyjson_is_str(value)) {
        return false;
    }
    const char *text = yyjson_get_str(value);
    char *end = NULL;
    errno = 0;
    unsigned long long hash = strtoull(text, &end, 16);
    if (errno || end == text || *end || strlen(text) > 16) {
        return false;
    }
    *out = (uint64_t)hash;
    return true;
}

static void add_snapshot_component(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *component,
    const prototype_registry *registry,
    yyjson_val *field_selectors,
    uint64_t field_limit
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_uint(doc, object, "id", component->id);
    yyjson_mut_obj_add_strcpy(doc, object, "key", component->key ? component->key : "");
    yyjson_mut_obj_add_strcpy(doc, object, "name", component->name ? component->name : "");
    yyjson_mut_obj_add_uint(doc, object, "bytes", component->element_size);
    yyjson_mut_obj_add_uint(doc, object, "layout_hash", component->layout_hash);
    if (component->type && component->type->name) {
        yyjson_mut_obj_add_strcpy(doc, object, "type", component->type->name);
    }

    yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
    const type_desc *type = component->type;
    if (!type) {
        yyjson_mut_obj_add_int(doc, object, "field_count", 0);
        yyjson_mut_obj_add_bool(doc, object, "fields_truncated", false);
        return;
    }

    av_value *component_value = prototype_entity_component_value(world, entity, component, registry);
    char *fields_json = component_value ? av_json_write(component_value) : NULL;
    av_value_free(component_value);
    yyjson_doc *fields_doc = fields_json
                               ? yyjson_read(fields_json, strlen(fields_json), 0)
                               : NULL;
    yyjson_val *field_values = fields_doc ? yyjson_doc_get_root(fields_doc) : NULL;
    if (field_values && yyjson_is_obj(field_values)) {
        field_values = yyjson_obj_get(
            field_values,
            component->key ? component->key : component->name
        );
    }
    int *order = type->field_count
                   ? xmalloc((size_t)type->field_count * sizeof *order)
                   : NULL;
    int order_count = 0;
    for (int i = 0; i < type->field_count; i++) {
        const char *field_name = type->fields[i].name ? type->fields[i].name : "";
        bool selected = !field_selectors;
        size_t component_size = strlen(component->name ? component->name : "");
        size_t field_size = strlen(field_name);
        size_t selector_size = component_size + 1 + field_size + 1;
        char *selector = xmalloc(selector_size);
        snprintf(selector, selector_size, "%s.%s", component->name ? component->name : "", field_name);
        if (field_selectors) {
            size_t selector_count = yyjson_arr_size(field_selectors);
            for (size_t selector_index = 0; selector_index < selector_count;
                 selector_index++) {
                yyjson_val *value = yyjson_arr_get(field_selectors, selector_index);
                const char *text = yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
                if (text && (!strcmp(text, selector) || !strcmp(text, field_name))) {
                    selected = true;
                    break;
                }
            }
        }
        free(selector);
        if (selected) {
            order[order_count++] = i;
        }
    }

    for (int i = 1; i < order_count; i++) {
        int current = order[i];
        int j = i;
        while (j > 0 &&
               strcmp(
                   type->fields[order[j - 1]].name ? type->fields[order[j - 1]].name : "",
                   type->fields[current].name ? type->fields[current].name : ""
               ) > 0) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = current;
    }
    for (int i = 0; i < order_count && (uint64_t)i < field_limit; i++) {
        const type_field *field = &type->fields[order[i]];
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, fields);
        yyjson_mut_obj_add_strcpy(doc, entry, "name", field->name ? field->name : "");
        yyjson_mut_obj_add_strcpy(doc, entry, "type", pin_json_name(field->type));
        if (field->type_name) {
            yyjson_mut_obj_add_strcpy(doc, entry, "type_name", field->type_name);
        }
        yyjson_val *field_value = field_values && yyjson_is_obj(field_values)
                                    ? yyjson_obj_get(field_values, field->name)
                                    : NULL;
        if (field->type == PIN_HANDLE) {
            pin_value handle;
            pin_read_field(
                NULL,
                ecs_component_data_const(world, entity, component->id),
                field,
                &handle
            );
            yyjson_mut_obj_add_uint(doc, entry, "value", handle.u64);
        } else if (field_value) {
            yyjson_mut_obj_add_val(doc, entry, "value", yyjson_val_mut_copy(doc, field_value));
        } else {
            yyjson_mut_obj_add_null(doc, entry, "value");
        }
    }
    yyjson_mut_obj_add_int(doc, object, "field_count", order_count);
    yyjson_mut_obj_add_bool(doc, object, "fields_truncated", (uint64_t)order_count > field_limit);
    yyjson_doc_free(fields_doc);
    free(fields_json);
    free(order);
}

static void add_snapshot_behavior(yyjson_mut_doc *doc, yyjson_mut_val *array, preview_runtime *runtime, int index)
{
    source_attachment_info info = source_attachment_at(runtime->sources, index);
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "asset", info.name);
    yyjson_mut_obj_add_uint(doc, object, "sequence", info.sequence);
    yyjson_mut_obj_add_str(
        doc,
        object,
        "backend",
        behavior_backend_name(execution_module_backend(info.module))
    );
    const execution_program_failure *failure = execution_program_error(info.program);
    if (failure) {
        yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "last_failure");
        yyjson_mut_obj_add_int(doc, value, "code", failure->status);
        yyjson_mut_obj_add_uint(doc, value, "function", failure->function_id);
        yyjson_mut_obj_add_uint(doc, value, "node_id", failure->node_id);
        yyjson_mut_obj_add_int(doc, value, "start", failure->start);
        yyjson_mut_obj_add_int(doc, value, "end", failure->end);
        yyjson_mut_obj_add_strcpy(doc, value, "message", failure->message);
    } else {
        yyjson_mut_obj_add_null(doc, object, "last_failure");
    }
    yyjson_mut_val *state = yyjson_mut_obj_add_obj(doc, object, "state");
    yyjson_mut_obj_add_bool(doc, state, "active", info.instance != NULL);
    yyjson_mut_obj_add_bool(
        doc,
        state,
        "suspended",
        info.instance && execution_instance_pending(info.instance) > 0
    );
    yyjson_mut_obj_add_int(doc, state, "begin", info.begin);
}

/* What behaviors set on Ui documents, as a presenter would show it: each
 * entity's selectors with the text, value, visibility and classes set under
 * them. */
static void add_snapshot_ui(yyjson_mut_doc *doc, yyjson_mut_val *object, preview_runtime *behaviors)
{
    ui *ui = behaviors->ui;
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, "ui");
    for (int i = 0; i < ui_element_count(ui); i++) {
        const ui_element *e = ui_element_at(ui, i);
        yyjson_mut_val *record = yyjson_mut_arr_add_obj(doc, array);
        yyjson_mut_obj_add_uint(doc, record, "entity", e->entity);
        yyjson_mut_obj_add_strcpy(doc, record, "element", e->selector);
        if (e->text) {
            yyjson_mut_obj_add_strcpy(doc, record, "text", e->text);
        }
        if (e->has_value) {
            yyjson_mut_obj_add_real(doc, record, "value", e->value);
        }
        if (e->visible >= 0) {
            yyjson_mut_obj_add_bool(doc, record, "visible", e->visible == 1);
        }
        if (!acount(e->classes)) {
            continue;
        }
        yyjson_mut_val *classes = yyjson_mut_obj_add_obj(doc, record, "classes");
        for (int c = 0; c < acount(e->classes); c++) {
            yyjson_mut_obj_add(
                classes,
                yyjson_mut_strcpy(doc, e->classes[c].name),
                yyjson_mut_bool(doc, e->classes[c].on)
            );
        }
    }
}

static yyjson_mut_val *add_snapshot_entity(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    ecs_world *world,
    ecs_entity entity,
    preview_runtime *behaviors,
    const prototype_registry *registry,
    yyjson_val *field_selectors,
    uint64_t component_limit,
    uint64_t field_limit,
    uint64_t behavior_limit
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_uint(doc, object, "id", ecs_entity_to_u64(entity));
    yyjson_mut_obj_add_uint(doc, object, "slot", entity.slot);
    yyjson_mut_obj_add_uint(doc, object, "generation", entity.generation);

    int world_component_count = ecs_world_component_count(world);
    const ecs_component_desc **present = xmalloc(
        (size_t)world_component_count * sizeof *present
    );
    int present_components = 0;
    for (int i = 0; i < world_component_count; i++) {
        const ecs_component_desc *component = ecs_world_component_at(world, i);
        if (ecs_component_has(world, entity, component->id)) {
            present[present_components++] = component;
        }
    }
    for (int i = 1; i < present_components; i++) {
        const ecs_component_desc *current = present[i];
        int j = i;
        while (j > 0 && strcmp(
                            present[j - 1]->name ? present[j - 1]->name : "",
                            current->name ? current->name : ""
                        ) > 0) {
            present[j] = present[j - 1];
            j--;
        }
        present[j] = current;
    }
    yyjson_mut_obj_add_int(doc, object, "component_count", present_components);
    yyjson_mut_val *components = yyjson_mut_obj_add_arr(doc, object, "components");
    int emitted_components = 0;
    for (int i = 0; i < present_components; i++) {
        if ((uint64_t)emitted_components < component_limit) {
            add_snapshot_component(doc, components, world, entity, present[i], registry, field_selectors, field_limit);
            emitted_components++;
        }
    }
    yyjson_mut_obj_add_bool(doc, object, "components_truncated", present_components > emitted_components);

    int behavior_count = 0, emitted_behaviors = 0;
    yyjson_mut_val *attachments = yyjson_mut_obj_add_arr(doc, object, "behaviors");
    for (int i = 0; i < source_attachment_count(behaviors->sources); i++) {
        source_attachment_info info = source_attachment_at(behaviors->sources, i);
        if (ecs_entity_to_u64(info.entity) != ecs_entity_to_u64(entity)) {
            continue;
        }
        behavior_count++;
        if ((uint64_t)emitted_behaviors < behavior_limit) {
            add_snapshot_behavior(doc, attachments, behaviors, i);
            emitted_behaviors++;
        }
    }
    yyjson_mut_obj_add_int(doc, object, "behavior_count", behavior_count);
    yyjson_mut_obj_add_bool(doc, object, "behaviors_truncated", behavior_count > emitted_behaviors);
    free(present);
    return object;
}

static int handle_scene_runtime_snapshot(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *name = request_string(request, "name");
    if (!name || !name[0]) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "scene name is required");
        return -1;
    }
    uint64_t frames = SCENARIO_DEFAULT_FRAMES;
    if (yyjson_obj_get(request, "frames") &&
        (!request_uint(request, "frames", &frames) || frames > SCENARIO_RUN_MAX_FRAMES)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "frames must be a non-negative integer no greater than 108000");
        return -1;
    }
    double fixed_dt = SCENARIO_FIXED_DT;
    yyjson_val *fixed_dt_value = yyjson_obj_get(request, "fixed_dt");
    if (fixed_dt_value) {
        if (!yyjson_is_num(fixed_dt_value) ||
            !isfinite(yyjson_get_num(fixed_dt_value)) ||
            fabs(yyjson_get_num(fixed_dt_value) - SCENARIO_FIXED_DT) > 1e-12) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "fixed_dt must equal the engine fixed timestep (1/60)");
            return -1;
        }
        fixed_dt = yyjson_get_num(fixed_dt_value);
    }
    uint64_t entity_limit = 0;
    uint64_t component_limit = 0;
    uint64_t field_limit = 0;
    uint64_t behavior_limit = 0;
    if (!runtime_snapshot_limit(
            request,
            "entity_limit",
            RUNTIME_SNAPSHOT_DEFAULT_ENTITIES,
            RUNTIME_SNAPSHOT_MAX_ENTITIES,
            &entity_limit
        ) ||
        !runtime_snapshot_limit(
            request,
            "component_limit",
            RUNTIME_SNAPSHOT_MAX_COMPONENTS,
            RUNTIME_SNAPSHOT_MAX_COMPONENTS,
            &component_limit
        ) ||
        !runtime_snapshot_limit(request, "field_limit", RUNTIME_SNAPSHOT_MAX_FIELDS, RUNTIME_SNAPSHOT_MAX_FIELDS, &field_limit) ||
        !runtime_snapshot_limit(
            request,
            "behavior_limit",
            RUNTIME_SNAPSHOT_MAX_BEHAVIORS,
            RUNTIME_SNAPSHOT_MAX_BEHAVIORS,
            &behavior_limit
        )) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "snapshot limits must be positive integers within their supported bounds");
        return -1;
    }
    yyjson_val *field_selectors = yyjson_obj_get(request, "fields");
    if (field_selectors) {
        if (!yyjson_is_arr(field_selectors) || yyjson_arr_size(field_selectors) > 32) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "fields must be an array with at most 32 selectors");
            return -1;
        }
        for (size_t i = 0; i < yyjson_arr_size(field_selectors); i++) {
            yyjson_val *selector = yyjson_arr_get(field_selectors, i);
            if (!yyjson_is_str(selector) || strlen(yyjson_get_str(selector)) > 128) {
                response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "field selectors must be strings no longer than 128 characters");
                return -1;
            }
        }
    }
    scenario_context context = { 0 };
    char error[256] = { 0 };
    int status = prepare_scene_scenario(root, name, &context, error, sizeof error);
    if (status == CHIBI_AUTOMATION_OK) {
        status = advance_request_scenario(&context, request, frames, fixed_dt, error, sizeof error);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(&context);
        response_error(result, exit_code, status, error);
        return -1;
    }
    ecs_world *world = prototype_preview_world(context.preview);
    preview_runtime *behaviors = prototype_preview_live(context.preview)->runtime;
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(
        result->doc,
        value,
        &context.prototype_ref,
        context.prototype_bytes,
        context.prototype_hash
    );
    yyjson_mut_obj_add_uint(result->doc, value, "frames", frames);
    yyjson_mut_obj_add_real(result->doc, value, "fixed_dt", fixed_dt);
    add_hash_named(result->doc, value, "world_hash", scenario_state_hash(&context));
    add_saved_state(result->doc, value, &context);
    add_presentation_state(result->doc, value, &context);
    yyjson_mut_obj_add_int(
        result->doc,
        value,
        "particle_count",
        particles_count(prototype_preview_particles(context.preview))
    );
    uint32_t live_entities = ecs_world_live_count(world);
    yyjson_mut_obj_add_uint(result->doc, value, "live_entities", live_entities);
    yyjson_mut_obj_add_uint(result->doc, value, "entity_count", live_entities);
    yyjson_mut_val *entities = yyjson_mut_obj_add_arr(result->doc, value, "entities");
    uint32_t emitted_entities = 0;
    for (uint32_t slot = 1; slot <= ecs_world_high_water_mark(world); slot++) {
        ecs_entity entity = { slot, ecs_world_generation(world, slot) };
        if (!ecs_entity_is_alive(world, entity) ||
            (uint64_t)emitted_entities >= entity_limit) {
            continue;
        }
        yyjson_mut_val *record = add_snapshot_entity(
            result->doc,
            entities,
            world,
            entity,
            behaviors,
            &context.registry,
            field_selectors,
            component_limit,
            field_limit,
            behavior_limit
        );
        for (int i = 0; i < acount(context.scene->entities); i++) {
            ecs_entity authored = prototype_preview_entity_at(context.preview, i);
            if (ecs_entity_to_u64(authored) == ecs_entity_to_u64(entity)) {
                char *path = scene_document_entity_path(context.scene, i);
                yyjson_mut_obj_add_strcpy(result->doc, record, "name", path);
                free(path);
                yyjson_mut_obj_add_strcpy(
                    result->doc,
                    record,
                    "prototype",
                    context.scene->entities[i].prototype
                );
                break;
            }
        }
        emitted_entities++;
    }
    yyjson_mut_obj_add_bool(result->doc, value, "entities_truncated", live_entities > emitted_entities);
    add_snapshot_ui(result->doc, value, behaviors);
    int log_count = preview_runtime_log_count(behaviors);
    yyjson_mut_obj_add_int(result->doc, value, "log_count", log_count);
    yyjson_mut_val *logs = yyjson_mut_obj_add_arr(result->doc, value, "logs");
    int emitted_logs = 0;
    for (int i = 0; i < log_count && emitted_logs < RUNTIME_SNAPSHOT_MAX_LOGS; i++) {
        const preview_log *info = preview_runtime_log_at(behaviors, i);
        yyjson_mut_val *log = yyjson_mut_arr_add_obj(result->doc, logs);
        yyjson_mut_obj_add_uint(result->doc, log, "entity", info->owner);
        yyjson_mut_obj_add_strcpy(result->doc, log, "asset", info->program ? info->program : "");
        yyjson_mut_obj_add_strncpy(result->doc, log, "message", info->text, info->text_length);
        emitted_logs++;
    }
    yyjson_mut_obj_add_bool(result->doc, value, "logs_truncated", log_count > emitted_logs);
    add_scenario_timing(result->doc, value, frames, fixed_dt);
    scenario_context_free(&context);
    return 0;
}

static int handle_runtime_snapshot(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *kind = request_string(request, "kind");
    if (kind && (!strcmp(kind, "scene") || !strcmp(kind, "scenes"))) {
        return handle_scene_runtime_snapshot(root, request, result, exit_code);
    }
    if (kind && strcmp(kind, "prototype") && strcmp(kind, "prototypes")) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "runtime_snapshot supports prototype or scene targets");
        return -1;
    }
    const char *name = request_string(request, "name");
    if (!name || !name[0]) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "prototype name is required");
        return -1;
    }

    uint64_t frames = SCENARIO_DEFAULT_FRAMES;
    yyjson_val *frames_value = yyjson_obj_get(request, "frames");
    if (frames_value && (!request_uint(request, "frames", &frames) ||
                         frames > SCENARIO_RUN_MAX_FRAMES)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "frames must be a non-negative integer no greater than 108000");
        return -1;
    }
    double fixed_dt = SCENARIO_FIXED_DT;
    yyjson_val *fixed_dt_value = yyjson_obj_get(request, "fixed_dt");
    if (fixed_dt_value) {
        if (!yyjson_is_num(fixed_dt_value)) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "fixed_dt must be a positive number");
            return -1;
        }
        fixed_dt = yyjson_get_num(fixed_dt_value);
        if (!isfinite(fixed_dt) || fixed_dt <= 0 || fixed_dt > 0.25 ||
            fabs(fixed_dt - SCENARIO_FIXED_DT) > 1e-12) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "fixed_dt must equal the engine fixed timestep (1/60)");
            return -1;
        }
    }

    uint64_t entity_limit = 0;
    uint64_t component_limit = 0;
    uint64_t field_limit = 0;
    uint64_t behavior_limit = 0;
    if (!runtime_snapshot_limit(
            request,
            "entity_limit",
            RUNTIME_SNAPSHOT_DEFAULT_ENTITIES,
            RUNTIME_SNAPSHOT_MAX_ENTITIES,
            &entity_limit
        ) ||
        !runtime_snapshot_limit(
            request,
            "component_limit",
            RUNTIME_SNAPSHOT_MAX_COMPONENTS,
            RUNTIME_SNAPSHOT_MAX_COMPONENTS,
            &component_limit
        ) ||
        !runtime_snapshot_limit(request, "field_limit", RUNTIME_SNAPSHOT_MAX_FIELDS, RUNTIME_SNAPSHOT_MAX_FIELDS, &field_limit) ||
        !runtime_snapshot_limit(
            request,
            "behavior_limit",
            RUNTIME_SNAPSHOT_MAX_BEHAVIORS,
            RUNTIME_SNAPSHOT_MAX_BEHAVIORS,
            &behavior_limit
        )) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "snapshot limits must be positive integers within their supported bounds");
        return -1;
    }
    yyjson_val *field_selectors = yyjson_obj_get(request, "fields");
    if (field_selectors) {
        if (!yyjson_is_arr(field_selectors) || yyjson_arr_size(field_selectors) > 32) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "fields must be an array with at most 32 selectors");
            return -1;
        }
        size_t selector_count = yyjson_arr_size(field_selectors);
        for (size_t i = 0; i < selector_count; i++) {
            yyjson_val *selector = yyjson_arr_get(field_selectors, i);
            if (!yyjson_is_str(selector) || strlen(yyjson_get_str(selector)) > 128) {
                response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "field selectors must be strings no longer than 128 characters");
                return -1;
            }
        }
    }

    scenario_context context = { 0 };
    char error[256] = { 0 };
    int status = prepare_scenario(root, name, &context, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    status = advance_request_scenario(&context, request, frames, fixed_dt, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(&context);
        response_error(result, exit_code, status, error);
        return -1;
    }

    ecs_world *world = prototype_preview_world(context.preview);
    preview_runtime *behaviors = prototype_preview_live(context.preview)->runtime;
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(
        result->doc,
        value,
        &context.prototype_ref,
        context.prototype_bytes,
        context.prototype_hash
    );
    yyjson_mut_obj_add_uint(result->doc, value, "frames", frames);
    yyjson_mut_obj_add_real(result->doc, value, "fixed_dt", fixed_dt);
    add_hash_named(result->doc, value, "world_hash", scenario_state_hash(&context));
    add_saved_state(result->doc, value, &context);
    add_presentation_state(result->doc, value, &context);
    yyjson_mut_obj_add_int(
        result->doc,
        value,
        "particle_count",
        particles_count(prototype_preview_particles(context.preview))
    );
    uint32_t live_entities = ecs_world_live_count(world);
    yyjson_mut_obj_add_uint(result->doc, value, "live_entities", live_entities);
    yyjson_mut_obj_add_uint(result->doc, value, "entity_count", live_entities);
    yyjson_mut_val *entities = yyjson_mut_obj_add_arr(result->doc, value, "entities");
    uint32_t emitted_entities = 0;
    uint32_t high_water = ecs_world_high_water_mark(world);
    for (uint32_t slot = 1; slot <= high_water; slot++) {
        ecs_entity entity = { slot, ecs_world_generation(world, slot) };
        if (!ecs_entity_is_alive(world, entity)) {
            continue;
        }
        if ((uint64_t)emitted_entities >= entity_limit) {
            break;
        }
        add_snapshot_entity(
            result->doc,
            entities,
            world,
            entity,
            behaviors,
            &context.registry,
            field_selectors,
            component_limit,
            field_limit,
            behavior_limit
        );
        emitted_entities++;
    }
    yyjson_mut_obj_add_bool(result->doc, value, "entities_truncated", live_entities > emitted_entities);
    add_snapshot_ui(result->doc, value, behaviors);
    int log_count = preview_runtime_log_count(behaviors);
    yyjson_mut_obj_add_int(result->doc, value, "log_count", log_count);
    yyjson_mut_val *logs = yyjson_mut_obj_add_arr(result->doc, value, "logs");
    int emitted_logs = 0;
    for (int i = 0; i < log_count && emitted_logs < RUNTIME_SNAPSHOT_MAX_LOGS; i++) {
        const preview_log *info = preview_runtime_log_at(behaviors, i);
        yyjson_mut_val *log = yyjson_mut_arr_add_obj(result->doc, logs);
        yyjson_mut_obj_add_uint(result->doc, log, "entity", info->owner);
        yyjson_mut_obj_add_strcpy(result->doc, log, "asset", info->program ? info->program : "");
        yyjson_mut_obj_add_strncpy(result->doc, log, "message", info->text, info->text_length);
        if (info->node) {
            yyjson_mut_val *origin = yyjson_mut_obj_add_obj(result->doc, log, "origin");
            yyjson_mut_obj_add_uint(result->doc, origin, "id", info->node);
        }
        emitted_logs++;
    }
    yyjson_mut_obj_add_bool(result->doc, value, "logs_truncated", log_count > emitted_logs);
    add_scenario_timing(result->doc, value, frames, fixed_dt);
    scenario_context_free(&context);
    return 0;
}

static bool parse_scenario_timing(
    yyjson_val *request,
    uint64_t *frames,
    double *fixed_dt,
    char *error,
    size_t error_size
)
{
    *frames = SCENARIO_DEFAULT_FRAMES;
    yyjson_val *frames_value = yyjson_obj_get(request, "frames");
    if (frames_value && (!request_uint(request, "frames", frames) ||
                         *frames > SCENARIO_MAX_FRAMES)) {
        snprintf(error, error_size, "frames must be a non-negative integer no greater than 1800");
        return false;
    }
    *fixed_dt = SCENARIO_DEFAULT_FIXED_DT;
    yyjson_val *fixed_dt_value = yyjson_obj_get(request, "fixed_dt");
    if (fixed_dt_value) {
        if (!yyjson_is_num(fixed_dt_value)) {
            snprintf(error, error_size, "fixed_dt must be a positive number");
            return false;
        }
        *fixed_dt = yyjson_get_num(fixed_dt_value);
        if (!isfinite(*fixed_dt) || *fixed_dt <= 0 || *fixed_dt > 0.25 ||
            fabs(*fixed_dt - SCENARIO_DEFAULT_FIXED_DT) > 1e-12) {
            snprintf(error, error_size, "fixed_dt must equal the engine fixed timestep (1/60)");
            return false;
        }
    }
    return true;
}

static int handle_terrain_storage(
    const char *root,
    yyjson_val *request,
    bool load,
    response *result,
    int *exit_code
)
{
    const char *name = request_string(request, "name");
    const char *entity_name = request_string(request, "entity");
    const char *identity = request_string(request, "identity");
    const char *path = request_string(request, "save_path");
    yyjson_val *identity_value = yyjson_obj_get(request, "identity");
    if (!name || !name[0] || !entity_name || !entity_name[0] || !identity ||
        !identity[0] || strlen(identity) > SAVE_BLOB_KEY_MAX ||
        strlen(identity) != yyjson_get_len(identity_value) || !path ||
        path[0] != '/' || yyjson_obj_get(request, "saved_state") ||
        yyjson_obj_get(request, "scenario") || yyjson_obj_get(request, "kind")) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "terrain storage requires scene name, entity, 1-64 byte identity and absolute save_path; omit kind, scenario and saved_state");
        return -1;
    }
    char error[256] = { 0 };
    uint64_t frames;
    double fixed_dt;
    if (!parse_scenario_timing(request, &frames, &fixed_dt, error, sizeof error)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, error);
        return -1;
    }
    frames = yyjson_obj_get(request, "frames") ? frames : 0;
    if (frames > 120) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "terrain storage frames must be no greater than 120");
        return -1;
    }
    scenario_context context = { 0 };
    int status = prepare_scene_scenario(root, name, &context, error, sizeof error);
    if (status == CHIBI_AUTOMATION_OK) {
        status = advance_request_scenario(&context, request, frames, fixed_dt, error, sizeof error);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(&context);
        response_error(result, exit_code, status, error);
        return -1;
    }
    ecs_entity entity = scenario_entity(&context, entity_name);
    const engine_modules *modules = world_modules(
        prototype_preview_live(context.preview)->runtime->owner
    );
    int token = terrain_storage_request(modules->terrain_storage, entity, identity, load, error, sizeof error);
    if (!token) {
        scenario_context_free(&context);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, error);
        return -1;
    }
    terrain_storage_flush(modules->terrain_storage);
    terrain_storage_state completion = terrain_storage_result(modules->terrain_storage, token);
    if (transform_propagate(prototype_preview_world(context.preview)) != ECS_OK) {
        set_error(error, sizeof error, "could not propagate restored terrain transforms");
    }
    bool physics_ready = false;
    if (modules->physics) {
        physics_ready = physics_tick(modules->physics, 0) == 0;
        if (!physics_ready) {
            set_error(error, sizeof error, physics_error(modules->physics));
        }
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_val *finished = yyjson_mut_obj_add_obj(result->doc, value, "completion");
    yyjson_mut_obj_add_int(result->doc, finished, "token", token);
    yyjson_mut_obj_add_bool(result->doc, finished, "pending", completion.pending);
    yyjson_mut_obj_add_bool(result->doc, finished, "succeeded", completion.succeeded);
    yyjson_mut_obj_add_str(
        result->doc,
        finished,
        "status",
        completion.pending     ? "pending"
        : completion.succeeded ? "succeeded"
                               : "failed"
    );
    if (completion.error && completion.error[0]) {
        yyjson_mut_obj_add_strcpy(result->doc, finished, "error", completion.error);
    }
    yyjson_mut_obj_add_bool(result->doc, value, "physics_ready", physics_ready);
    yyjson_mut_obj_add_uint(result->doc, value, "frames", frames);
    yyjson_mut_obj_add_strcpy(result->doc, value, "save_path", path);
    add_presentation_state(result->doc, value, &context);
    add_behavior_failures(result->doc, value, &context);
    yyjson_mut_val *terrain = yyjson_mut_obj_add_obj(result->doc, value, "terrain");
    yyjson_mut_obj_add_strcpy(result->doc, terrain, "entity", entity_name);
    yyjson_mut_obj_add_strcpy(result->doc, terrain, "identity", identity);
    /* The shared codec excludes fragments, actors and transient ECS identities.
     * Its exact bytes are stable evidence of this terrain's canonical samples. */
    unsigned char *bytes = NULL;
    size_t length = 0;
    char snapshot_error[256] = { 0 };
    if (materials_terrain_checkpoint_write(
            modules->materials,
            entity,
            identity,
            &bytes,
            &length,
            snapshot_error,
            sizeof snapshot_error
        )) {
        add_hash_named(
            result->doc,
            terrain,
            "snapshot_hash",
            scenario_hash_bytes(UINT64_C(14695981039346656037), bytes, length)
        );
        yyjson_mut_obj_add_uint(result->doc, terrain, "snapshot_bytes", length);
    } else {
        yyjson_mut_obj_add_strcpy(result->doc, terrain, "snapshot_error", snapshot_error);
    }
    free(bytes);
    if (!completion.succeeded || completion.pending) {
        response_error(
            result,
            exit_code,
            CHIBI_AUTOMATION_EXECUTION_ERROR,
            completion.error && completion.error[0] ? completion.error : "terrain storage did not complete"
        );
    } else if (error[0]) {
        response_error(result, exit_code, CHIBI_AUTOMATION_EXECUTION_ERROR, error);
    }
    scenario_context_free(&context);
    return completion.succeeded && !error[0] ? 0 : -1;
}

static int handle_record_scenario(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *kind = request_string(request, "kind");
    if (kind && strcmp(kind, "prototype") && strcmp(kind, "scene")) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "record_scenario supports prototype or scene targets");
        return -1;
    }
    const char *name = request_string(request, "name");
    if (!name || !name[0]) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "prototype name is required");
        return -1;
    }
    char error[256] = { 0 };
    uint64_t frames = 0;
    double fixed_dt = 0;
    if (!parse_scenario_timing(request, &frames, &fixed_dt, error, sizeof error)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, error);
        return -1;
    }
    /* A recording holds one input per frame; a timeline's samples land on
       their frames. */
    scenario_sequence sequence = { 0 };
    int parsed = prepare_sequence(request, frames, &sequence, error, sizeof error);
    if (parsed != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, parsed, error);
        free_sequence(&sequence);
        return -1;
    }
    scenario_input *inputs = xcalloc(SCENARIO_MAX_FRAMES, sizeof *inputs);
    for (size_t i = 0; i < sequence.count; i++) {
        inputs[sequence.at[i]] = sequence.inputs[i];
    }
    free_sequence(&sequence);

    scenario_context context = { 0 };
    int status = kind && !strcmp(kind, "scene")
                   ? prepare_scene_scenario(root, name, &context, error, sizeof error)
                   : prepare_scenario(root, name, &context, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(inputs);
        return -1;
    }
    scenario_observations observations = { 0 };
    status = scenario_saved_state(&context, request, error, sizeof error);
    if (status == CHIBI_AUTOMATION_OK) {
        status = run_scenario_inputs(&context, inputs, frames, fixed_dt, &observations, error, sizeof error);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(&context);
        response_error(result, exit_code, status, error);
        free(inputs);
        return -1;
    }

    char artifact_path[256] = { 0 };
    status = create_recording_path(artifact_path, sizeof artifact_path, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(&context);
        response_error(result, exit_code, status, error);
        free(inputs);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_val *recording = add_recording(
        result->doc,
        value,
        &context,
        inputs,
        &observations,
        frames,
        fixed_dt,
        artifact_path
    );
    yyjson_mut_obj_add_uint(result->doc, value, "frames", frames);
    yyjson_mut_obj_add_real(result->doc, value, "fixed_dt", fixed_dt);
    yyjson_mut_obj_add_strcpy(result->doc, value, "recording_path", artifact_path);
    yyjson_mut_obj_add_strcpy(result->doc, value, "artifact_path", artifact_path);
    status = write_recording_artifact(artifact_path, recording, error, sizeof error);
    scenario_context_free(&context);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(inputs);
        return -1;
    }
    free(inputs);
    return 0;
}

static int handle_replay_scenario(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *inline_recording = yyjson_obj_get(request, "recording");
    const char *recording_path = request_string(request, "recording_path");
    if ((inline_recording && recording_path) || (!inline_recording && !recording_path)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "replay_scenario requires exactly one recording or recording_path");
        return -1;
    }
    yyjson_doc *recording_doc = NULL;
    yyjson_val *recording = inline_recording;
    char error[256] = { 0 };
    if (recording_path) {
        recording_doc = read_recording_artifact(recording_path, error, sizeof error);
        if (!recording_doc) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, error);
            return -1;
        }
        recording = yyjson_doc_get_root(recording_doc);
    }
    if (!recording || !yyjson_is_obj(recording)) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "recording must be a JSON object");
        return -1;
    }
    size_t recording_bytes = 0;
    char *recording_json = yyjson_val_write(recording, 0, &recording_bytes);
    bool recording_valid = recording_json &&
                           recording_bytes <= SCENARIO_RECORDING_MAX_BYTES;
    free(recording_json);
    if (!recording_valid) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "recording exceeds the supported size");
        return -1;
    }
    if (yyjson_obj_get(recording, "save_path")) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "scenario recordings cannot access save_path");
        return -1;
    }
    const char *format = request_string(recording, "format");
    uint64_t version = 0;
    uint64_t contract = 0;
    if (!format || strcmp(format, SCENARIO_RECORDING_FORMAT) ||
        !request_uint(recording, "version", &version) ||
        version != SCENARIO_RECORDING_VERSION ||
        !request_uint(recording, "contract_version", &contract) ||
        contract != CHIBI_AUTOMATION_CONTRACT) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "recording format, version, or contract is incompatible");
        return -1;
    }
    const char *name = request_string(recording, "name");
    uint64_t expected_prototype_hash = 0, expected_runtime_hash = 0;
    if (!name || !name[0] ||
        !recording_hash(recording, "content_hash", &expected_prototype_hash) ||
        !recording_hash(recording, "runtime_content_hash", &expected_runtime_hash)) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "recording target identity and runtime content hash are required");
        return -1;
    }
    uint64_t frames = 0;
    double fixed_dt = 0;
    if (!parse_scenario_timing(recording, &frames, &fixed_dt, error, sizeof error)) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    yyjson_val *input_values = yyjson_obj_get(recording, "inputs");
    yyjson_val *frame_hashes = yyjson_obj_get(recording, "frame_hashes");
    yyjson_val *live_entities_values = yyjson_obj_get(recording, "live_entities");
    if (!yyjson_is_arr(input_values) || yyjson_arr_size(input_values) != frames ||
        !yyjson_is_arr(frame_hashes) || yyjson_arr_size(frame_hashes) != frames ||
        !yyjson_is_arr(live_entities_values) ||
        yyjson_arr_size(live_entities_values) != frames) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "recording inputs, frame_hashes and live_entities must contain exactly frames entries");
        return -1;
    }
    scenario_input *inputs = xcalloc(SCENARIO_MAX_FRAMES, sizeof *inputs);
    scenario_observations expected = { .has_live_entities = true };
    for (uint64_t frame = 0; frame < frames; frame++) {
        yyjson_val *live = yyjson_arr_get(live_entities_values, frame);
        if (!parse_scenario_input(yyjson_arr_get(input_values, frame), &inputs[frame], error, sizeof error) ||
            !recording_hash_value(yyjson_arr_get(frame_hashes, frame), &expected.world_hash[frame]) ||
            !yyjson_is_uint(live) || yyjson_get_uint(live) > UINT32_MAX) {
            yyjson_doc_free(recording_doc);
            response_error(
                result,
                exit_code,
                CHIBI_AUTOMATION_VALIDATION_ERROR,
                error[0] ? error : "recording observations are invalid"
            );
            free(inputs);
            return -1;
        }
        expected.live_entities[frame] = (uint32_t)yyjson_get_uint(live);
    }

    scenario_context context = { 0 };
    const char *kind = request_string(recording, "kind");
    if (!kind || (strcmp(kind, "scene") && strcmp(kind, "prototype"))) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "recording kind must be scene or prototype");
        free(inputs);
        return -1;
    }
    int status = !strcmp(kind, "scene")
                   ? prepare_scene_scenario(root, name, &context, error, sizeof error)
                   : prepare_scenario(root, name, &context, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, status, error);
        free(inputs);
        return -1;
    }
    if (context.prototype_hash != expected_prototype_hash ||
        context.runtime_hash != expected_runtime_hash) {
        scenario_context_free(&context);
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "recording runtime content changed; create a new recording for the current assets");
        free(inputs);
        return -1;
    }
    scenario_observations actual = { 0 };
    status = scenario_saved_state(&context, recording, error, sizeof error);
    if (status == CHIBI_AUTOMATION_OK) {
        status = run_scenario_inputs(&context, inputs, frames, fixed_dt, &actual, error, sizeof error);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(&context);
        yyjson_doc_free(recording_doc);
        response_error(result, exit_code, status, error);
        free(inputs);
        return -1;
    }
    int divergence = -1;
    for (uint64_t frame = 0; frame < frames; frame++) {
        if (expected.world_hash[frame] != actual.world_hash[frame] ||
            (expected.has_live_entities &&
             expected.live_entities[frame] != actual.live_entities[frame])) {
            divergence = (int)frame;
            break;
        }
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(
        result->doc,
        value,
        &context.prototype_ref,
        context.prototype_bytes,
        context.prototype_hash
    );
    yyjson_mut_obj_add_uint(result->doc, value, "frames", frames);
    yyjson_mut_obj_add_real(result->doc, value, "fixed_dt", fixed_dt);
    yyjson_mut_obj_add_bool(result->doc, value, "matched", divergence < 0);
    yyjson_mut_obj_add_bool(result->doc, value, "deterministic", divergence < 0);
    yyjson_mut_obj_add_bool(result->doc, value, "diverged", divergence >= 0);
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "replayed_frames",
        divergence < 0 ? frames : (uint64_t)divergence + 1
    );
    if (recording_path) {
        yyjson_mut_obj_add_strcpy(result->doc, value, "recording_path", recording_path);
    }
    if (divergence >= 0) {
        yyjson_mut_obj_add_int(result->doc, value, "first_divergence_frame", divergence);
        yyjson_mut_val *difference = yyjson_mut_obj_add_obj(result->doc, value, "divergence");
        yyjson_mut_obj_add_uint(result->doc, difference, "frame", divergence);
        add_hash_named(result->doc, difference, "expected_world_hash", expected.world_hash[divergence]);
        add_hash_named(result->doc, difference, "actual_world_hash", actual.world_hash[divergence]);
        yyjson_mut_obj_add_uint(
            result->doc,
            difference,
            "expected_live_entities",
            expected.live_entities[divergence]
        );
        yyjson_mut_obj_add_uint(
            result->doc,
            difference,
            "actual_live_entities",
            actual.live_entities[divergence]
        );
    }
    scenario_context_free(&context);
    yyjson_doc_free(recording_doc);
    free(inputs);
    return 0;
}

static int handle_read_recording(yyjson_val *request, response *result, int *exit_code)
{
    const char *id = request_string(request, "id");
    if (!id || !id[0] || strlen(id) > 128 ||
        strspn(id, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") !=
            strlen(id)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "id must be a bounded generated recording identifier");
        return -1;
    }
    char path[256] = { 0 };
    if (snprintf(path, sizeof path, "%s%s", SCENARIO_RECORDING_PATH_PREFIX, id) >=
        (int)sizeof path) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "recording identifier is too long");
        return -1;
    }
    char error[256] = { 0 };
    yyjson_doc *document = read_recording_artifact(path, error, sizeof error);
    if (!document) {
        response_error(result, exit_code, CHIBI_AUTOMATION_NOT_FOUND, "recording_not_found");
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_val(
        result->doc,
        value,
        "recording",
        yyjson_val_mut_copy(result->doc, yyjson_doc_get_root(document))
    );
    yyjson_doc_free(document);
    return 0;
}

static void add_component_schema(
    response *result,
    yyjson_mut_val *components,
    const ecs_component_desc *component,
    const prototype_registry *registry
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(result->doc, components);
    yyjson_mut_obj_add_strcpy(
        result->doc,
        object,
        "key",
        component->key ? component->key : component->name
    );
    yyjson_mut_obj_add_strcpy(result->doc, object, "name", component->name);
    yyjson_mut_obj_add_uint(result->doc, object, "id", component->id);
    yyjson_mut_obj_add_uint(result->doc, object, "bytes", component->element_size);
    yyjson_mut_obj_add_uint(result->doc, object, "layout_hash", component->layout_hash);
    if (component->type) {
        yyjson_mut_obj_add_strcpy(result->doc, object, "type", component->type->name);
    }
    yyjson_mut_val *fields = yyjson_mut_obj_add_arr(result->doc, object, "fields");
    for (int i = 0; component->type && i < component->type->field_count; i++) {
        const type_field *field = &component->type->fields[i];
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(result->doc, fields);
        yyjson_mut_obj_add_strcpy(result->doc, entry, "name", field->name);
        yyjson_mut_obj_add_strcpy(
            result->doc,
            entry,
            "type",
            pin_json_name(field->type)
        );
        yyjson_mut_obj_add_uint(result->doc, entry, "bytes", field->size);
        yyjson_mut_obj_add_bool(
            result->doc,
            entry,
            "writable",
            field->type != PIN_HANDLE && field->type != PIN_FLOW
        );
        if (field->type_name) {
            yyjson_mut_obj_add_strcpy(result->doc, entry, "type_name", field->type_name);
        }
        if (field->description) {
            yyjson_mut_obj_add_str(result->doc, entry, "description", field->description);
        }
    }
    av_value *defaults_value = prototype_native_defaults_value(registry, component);
    char *defaults_json = defaults_value ? av_json_write(defaults_value) : NULL;
    av_value_free(defaults_value);
    if (!defaults_json) {
        return;
    }
    yyjson_doc *defaults_doc = yyjson_read(defaults_json, strlen(defaults_json), 0);
    yyjson_val *defaults_root = defaults_doc ? yyjson_doc_get_root(defaults_doc) : NULL;
    yyjson_val *defaults = defaults_root && yyjson_is_obj(defaults_root)
                             ? yyjson_obj_get(
                                   defaults_root,
                                   component->key ? component->key : component->name
                               )
                             : NULL;
    if (!defaults && defaults_root && yyjson_is_obj(defaults_root)) {
        defaults = yyjson_obj_get(defaults_root, component->name);
    }
    if (defaults) {
        yyjson_mut_obj_add_val(
            result->doc,
            object,
            "defaults",
            yyjson_val_mut_copy(result->doc, defaults)
        );
    }
    yyjson_doc_free(defaults_doc);
    free(defaults_json);
}

static int handle_describe_components(yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *names = yyjson_obj_get(request, "names");
    if (names && (!yyjson_is_arr(names) || yyjson_arr_size(names) > 64)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "names must be an array with at most 64 entries");
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_val *components = yyjson_mut_obj_add_arr(result->doc, value, "components");
    prototype_registry registry = project_registry();
    if (names) {
        yyjson_arr_iter iterator = yyjson_arr_iter_with(names);
        yyjson_val *name;
        while ((name = yyjson_arr_iter_next(&iterator))) {
            if (!yyjson_is_str(name) || !yyjson_get_len(name) ||
                yyjson_get_len(name) > 256) {
                response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "component names must be non-empty bounded strings");
                return -1;
            }
            const ecs_component_desc
                *component = automation_query_component(yyjson_get_str(name), NULL);
            if (!component) {
                response_error(result, exit_code, CHIBI_AUTOMATION_NOT_FOUND, "component was not found in the reflected registry");
                return -1;
            }
            add_component_schema(result, components, component, &registry);
        }
    } else {
        for (int i = 0; i < registry.component_count; i++) {
            add_component_schema(result, components, registry.components[i], &registry);
        }
    }
    yyjson_mut_obj_add_uint(result->doc, value, "count", yyjson_mut_arr_size(components));
    return 0;
}

static const char *const REGISTRY_SECTIONS[] = {
    "nodes",
    "types",
    "services",
    "components",
};

/* Nodes dominate the registry; searchNodes serves focused node lookups. */
static bool registry_section(yyjson_val *sections, const char *name)
{
    if (!sections) {
        return strcmp(name, "nodes") != 0;
    }
    size_t i, count;
    yyjson_val *section;
    yyjson_arr_foreach(sections, i, count, section)
    {
        if (!strcmp(yyjson_get_str(section), name)) {
            return true;
        }
    }
    return false;
}

static bool valid_registry_sections(yyjson_val *sections)
{
    if (!sections) {
        return true;
    }
    if (!yyjson_is_arr(sections)) {
        return false;
    }
    size_t i, count;
    yyjson_val *section;
    yyjson_arr_foreach(sections, i, count, section)
    {
        bool known = false;
        for (int s = 0;
             yyjson_is_str(section) &&
             s < (int)(sizeof REGISTRY_SECTIONS / sizeof *REGISTRY_SECTIONS);
             s++) {
            known |= !strcmp(yyjson_get_str(section), REGISTRY_SECTIONS[s]);
        }
        if (!known) {
            return false;
        }
    }
    return true;
}

static int handle_describe_registry(yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *sections = request ? yyjson_obj_get(request, "sections") : NULL;
    if (!valid_registry_sections(sections)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "sections must list nodes, types, services, or components");
        return -1;
    }
    yyjson_mut_doc *doc = result->doc;
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, result->root, "result");
    if (registry_section(sections, "nodes")) {
        yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(doc, value, "nodes");
        for (int i = 0; i < registry_node_count(); i++) {
            const node_desc *node = registry_node(i);
            yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, nodes);
            yyjson_mut_obj_add_strcpy(doc, object, "name", node->name);
            yyjson_mut_obj_add_bool(doc, object, "pure", node_desc_is_pure(node));
            yyjson_mut_obj_add_int(doc, object, "display_class", node->display_class);
            if (node->category) {
                yyjson_mut_obj_add_strcpy(doc, object, "category", node->category);
            }
            if (node->keywords) {
                yyjson_mut_obj_add_strcpy(doc, object, "keywords", node->keywords);
            }
            add_pins(doc, object, "inputs", node->inputs, node->input_count);
            add_pins(doc, object, "outputs", node->outputs, node->output_count);
        }
    }
    if (registry_section(sections, "types")) {
        yyjson_mut_val *types = yyjson_mut_obj_add_arr(doc, value, "types");
        for (int i = 0; i < type_catalog_count(); i++) {
            add_type(doc, types, type_catalog_at(i));
        }
    }
    if (registry_section(sections, "services")) {
        yyjson_mut_val *services = yyjson_mut_obj_add_arr(doc, value, "services");
        for (int i = 0; i < registry_service_count(); i++) {
            const service_desc *service = registry_service(i);
            yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, services);
            yyjson_mut_obj_add_strcpy(doc, object, "name", service->name);
            yyjson_mut_obj_add_uint(doc, object, "stable_id", service->stable_id);
        }
    }
    if (registry_section(sections, "components")) {
        yyjson_mut_val *components = yyjson_mut_obj_add_arr(doc, value, "components");
        ecs_component_catalog catalog = project_components();
        for (int i = 0; i < catalog.component_count; i++) {
            const ecs_component_desc *component = catalog.components[i];
            yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, components);
            yyjson_mut_obj_add_strcpy(doc, object, "key", component->key);
            yyjson_mut_obj_add_strcpy(doc, object, "name", component->name);
            yyjson_mut_obj_add_uint(doc, object, "id", component->id);
            yyjson_mut_obj_add_uint(doc, object, "bytes", component->element_size);
            yyjson_mut_obj_add_uint(doc, object, "layout_hash", component->layout_hash);
            if (component->type) {
                yyjson_mut_obj_add_strcpy(doc, object, "type", component->type->name);
            }
        }
    }
    return 0;
}

/* Behaviors and systems share the canonical capability vocabulary. Declarative
 * shader, field and model authoring retains its own node metadata. Studio
 * capabilities are offered only to the Studio project. */
static bool domain_offers(const char *domain, const node_desc *node, bool studio)
{
    if (!studio && studio_node_is(node)) {
        return false;
    }
    if (domain && !strcmp(domain, "system")) {
        domain = "behavior";
    }
    bool shader = node->category && !strncmp(node->category, "Shader", 6);
    bool field = node->category && !strcmp(node->category, "Procedural Field");
    bool model = node->category && !strncmp(node->category, "Model", 5);
    const char *own = model  ? "model"
                    : shader ? "shader"
                    : field  ? "field"
                             : "behavior";
    return (!domain || !strcmp(domain, own)) && !component_generated_kind(node);
}

/* Component node presets and events belong to behavior graphs. */
static bool behavior_vocabulary(const char *domain)
{
    return !domain || !strcmp(domain, "behavior");
}

/* The events a project names: its event definitions, and those its ui
 * documents send behaviors (assets/ui_events.h); and the values those
 * documents bind under self. */
typedef struct
{
    behavior_asset **definitions;
    ui_event *ui;
    ui_data_binding *bindings;
    bool studio; /* the Studio project, which runs Studio's editor nodes */
} project_events;

static bool defined_event(const project_events *events, const char *name)
{
    for (int i = 0; i < acount(events->definitions); i++) {
        if (!strcmp(events->definitions[i]->name, name)) {
            return true;
        }
    }
    return host_event_reserved(name);
}

/* The names of the ui documents whose events reach `behavior`: the Ui
 * documents on what attaches it (reference_index_attached_documents). They
 * live as long as the catalog snapshot. */
static int attached_ui_documents(
    const char *root,
    const char *behavior,
    const char ***names,
    char *error,
    size_t error_size
)
{
    catalog_asset *assets;
    uint64_t revision, content_hash;
    int cataloged = catalog_snapshot_get(root, &assets, &revision, &content_hash, error, error_size);
    if (cataloged != CHIBI_AUTOMATION_OK) {
        return cataloged;
    }
    const reference_index *index = CATALOG_SNAPSHOT.references;
    int at = reference_index_find_name(index, REFERENCE_ASSET_BEHAVIOR, behavior);
    if (at < 0 || reference_index_asset_at(index, at)->unresolved) {
        snprintf(error, error_size, "no behavior named %s", behavior);
        return CHIBI_AUTOMATION_NOT_FOUND;
    }
    int *documents = NULL;
    reference_index_attached_documents(index, at, &documents);
    for (int i = 0; i < acount(documents); i++) {
        apush(*names, reference_index_asset_at(index, documents[i])->name);
    }
    afree(documents);
    return CHIBI_AUTOMATION_OK;
}

static bool among_names(const char **names, const char *name)
{
    for (int i = 0; i < acount(names); i++) {
        if (!strcmp(names[i], name)) {
            return true;
        }
    }
    return false;
}

/* Adds the events the project's ui documents send, or with a `behavior`
 * those of the documents that reach it; the pause menu's go to the player
 * shell. An invalid document offers none until it validates. */
static int load_ui_events(
    const char *root,
    const char *behavior,
    project_events *events,
    char *error,
    size_t error_size
)
{
    const char **reached = NULL;
    int status = behavior
                   ? attached_ui_documents(root, behavior, &reached, error, error_size)
                   : CHIBI_AUTOMATION_OK;
    asset_entry *entries = NULL;
    if (status == CHIBI_AUTOMATION_OK &&
        enumerate_assets(root, &entries, error, error_size) != 0) {
        status = CHIBI_AUTOMATION_IO_ERROR;
    }
    const asset_family *family = family_by_kind("ui");
    for (int i = 0; i < acount(entries) && status == CHIBI_AUTOMATION_OK; i++) {
        if (entries[i].family != family) {
            continue;
        }
        char *document = asset_name_from_filename(entries[i].filename);
        if (!strcmp(document, UI_DOCUMENT_PAUSE_MENU) ||
            (behavior && !among_names(reached, document))) {
            free(document);
            continue;
        }
        char *path = content_path(root, entries[i].relative);
        char *text = read_file(path, NULL, NULL, error, error_size);
        free(path);
        ui_document_event *found = NULL;
        ui_document_binding *bound = NULL;
        if (text) {
            ui_document_events(text, &found);
            ui_document_bindings(text, &bound);
        }
        for (int b = 0; b < acount(bound); b++) {
            ui_data_binding added = { .binding = bound[b] };
            snprintf(added.document, sizeof added.document, "%s", document);
            apush(events->bindings, added);
        }
        afree(bound);
        /* Host events and definitions already offer their own names. */
        for (int e = 0; e < acount(found); e++) {
            if (!defined_event(events, found[e].name)) {
                ui_events_add(&events->ui, &found[e], document);
            }
        }
        status = text ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_IO_ERROR;
        ui_document_events_free(found);
        free(document);
        free(text);
    }
    free_asset_entries(entries);
    afree(reached);
    return status;
}

static void free_project_events(project_events *events)
{
    free_behavior_assets(events->definitions);
    ui_events_free(events->ui);
    afree(events->bindings);
}

static int load_project_events(
    const char *root,
    const char *behavior,
    project_events *events,
    char *error,
    size_t error_size
)
{
    int status = load_behavior_family_assets(
        root,
        family_by_kind("event"),
        &events->definitions,
        error,
        error_size
    );
    return status == CHIBI_AUTOMATION_OK
             ? load_ui_events(root, behavior, events, error, error_size)
             : status;
}

typedef struct
{
    const char *pin_type;
    const char *pin_container;
    const char *pin_kind;
    yyjson_val *pure;
} pin_filter;

static bool pin_matches(const pin_spec *pin, const pin_filter *filter)
{
    return (!filter->pin_type || !strcmp(pin_json_name(pin->type), filter->pin_type) ||
            (pin->type_name && !strcmp(pin->type_name, filter->pin_type))) &&
           (!filter->pin_container ||
            !strcmp(container_json_name(pin->container), filter->pin_container)) &&
           (!filter->pin_kind ||
            !strcmp(pin->kind == PIN_EXEC ? "exec" : "data", filter->pin_kind));
}

/* A candidate's pins: a registry node's declared ones, or those a configured
 * node has in a scratch graph. */
typedef struct
{
    const pin_spec *inputs, *outputs;
    int input_count, output_count;
} pin_sides;

static bool pins_match(const pin_sides *n, const pin_filter *filter)
{
    bool pure = true;
    bool pin_filtered = filter->pin_type || filter->pin_container || filter->pin_kind;
    bool matched = !pin_filtered;
    for (int output = 0; output < 2; output++) {
        const pin_spec *pins = output ? n->outputs : n->inputs;
        int count = output ? n->output_count : n->input_count;
        for (int i = 0; i < count; i++) {
            pure &= pins[i].kind != PIN_EXEC;
            matched |= pin_filtered && pin_matches(&pins[i], filter);
        }
    }
    return matched && (!filter->pure || pure == yyjson_get_bool(filter->pure));
}

/* A search result: a registry node, a component node preset, or an event. An
 * event is an Event node for a host event, an event definition or a ui
 * event, or a Send Event for a definition. */
typedef struct
{
    const node_desc *desc;
    const component_node_preset *preset;
    const host_event_desc *host;
    const behavior_asset *definition;
    const ui_event *ui;
    const ui_data_binding *binding; /* a Set UI Data that sets it */
    int score;
    int order;
    /* With a wire to fit: how well (wire_fit) and through which pins. */
    int fit;
    int wire_input, wire_output;
} node_match;

static int node_match_compare(const void *a, const void *b)
{
    const node_match *left = a, *right = b;
    int fit = right->fit - left->fit;
    int score = right->score - left->score;
    return fit ? fit : score ? score : left->order - right->order;
}

static const char *match_event(const node_match *match)
{
    return match->host       ? match->host->name
         : match->definition ? match->definition->name
         : match->ui         ? match->ui->name
                             : NULL;
}

/* The node add_node creates for a match, in a scratch graph. */
/* Set UI Data's Name, the pin a binding's result sets. */
#define SET_UI_DATA_NAME 2

static node *match_node(graph *g, const node_match *match)
{
    node *n = graph_add_node(g, match->desc);
    assert(n);
    const char *event = match_event(match);
    int result = 0;
    if (match->binding) {
        pin_value name = { .type = PIN_NAME,
                           .type_name = "name",
                           .str = match->binding->binding.name };
        result = graph_set_pin_default(g, n, 0, SET_UI_DATA_NAME, &name);
    } else if (match->preset) {
        result = component_node_configure(
            g,
            n,
            match->preset->setting,
            match->preset->field
        );
    } else if (event && n->desc == &NODE_Event) {
        result = graph_set_event_name(g, n, event) ||
                 (match->host ? graph_host_event_apply(g, n)
                  : match->definition ? graph_event_apply(g, n, match->definition)
                                      : 0);
    } else if (event) {
        result = graph_set_node_reference(g, n, 0, event) ||
                 graph_event_apply(g, n, match->definition);
    }
    assert(!result);
    (void)result;
    return n;
}

/* Appends " word" while it fits. */
static void append_word(char *text, size_t size, int *used, const char *word)
{
    if (word && *used < (int)size) {
        *used += snprintf(text + *used, size - (size_t)*used, " %s", word);
    }
}

/* A preset reads as its access, its type and its field: "Set Health
 * current". Get, a whole-value Set, Make and Break also carry every field. */
static int preset_score(const component_node_preset *preset, const char *query)
{
    char name[256], secondary[1024] = "";
    snprintf(
        name,
        sizeof name,
        "%.*s %s %s",
        (int)strcspn(preset->desc->name, " "),
        preset->desc->name,
        preset->type->name,
        preset->field ? preset->field : ""
    );
    int used = 0;
    append_word(secondary, sizeof secondary, &used, preset->desc->category);
    append_word(secondary, sizeof secondary, &used, preset->desc->keywords);
    component_node_kind kind = component_node_kind_of(preset->desc);
    bool fields = !preset->field && kind != COMPONENT_NODE_HAS &&
                  kind != COMPONENT_NODE_ADD && kind != COMPONENT_NODE_REMOVE;
    for (int i = 0; fields && i < preset->type->field_count; i++) {
        append_word(secondary, sizeof secondary, &used, preset->type->fields[i].name);
    }
    const node_desc *shape = component_node_shape(preset->desc, preset->setting);
    int binding = 0;
    const char *spelling = shape ? behavior_expression_spelling(shape, &binding) : NULL;
    append_word(secondary, sizeof secondary, &used, binding ? NULL : spelling);
    return text_search_word_score(query, name, secondary);
}

/* An event's words beside its name: its keywords or description and payload
 * names, and for a ui event what the player does. */
static void event_words(const node_match *match, char *text, size_t size)
{
    int used = 0;
    bool send = match->desc != &NODE_Event;
    append_word(text, size, &used, send ? "Events send dispatch" : "Events when");
    if (match->host) {
        append_word(text, size, &used, match->host->keywords);
        for (int i = 0; i < match->host->payload_count; i++) {
            append_word(text, size, &used, match->host->payload[i].name);
        }
        return;
    }
    if (match->ui) {
        append_word(text, size, &used, match->ui->keywords);
        append_word(text, size, &used, match->ui->description);
        return;
    }
    append_word(text, size, &used, match->definition->description);
    for (int i = 0; i < acount(match->definition->variables); i++) {
        append_word(text, size, &used, match->definition->variables[i].name);
    }
}

/* An event reads as "On Contact Begin" or "Send Damage"; an Event also as
 * its own name, so naming it exactly ranks it first. */
static int event_score(const node_match *match, const char *query)
{
    char name[256], secondary[1024] = "";
    bool send = match->desc != &NODE_Event;
    snprintf(name, sizeof name, "%s %s", send ? "Send" : "On", match_event(match));
    event_words(match, secondary, sizeof secondary);
    int phrased = text_search_word_score(query, name, secondary);
    int named = send ? -1 : text_search_word_score(query, match_event(match), secondary);
    return phrased > named ? phrased : named;
}

/* Pin names and a node's expression function rank like keywords, so a query
 * can name what a node takes or how an expression writes it. */
static int node_score(const node_desc *node, const char *query)
{
    char secondary[1024] = "";
    int used = 0;
    append_word(secondary, sizeof secondary, &used, node->category);
    append_word(secondary, sizeof secondary, &used, node->keywords);
    const char *alias = NULL;
    for (int i = 0; (alias = behavior_expression_alias(node, i)); i++) {
        append_word(secondary, sizeof secondary, &used, alias);
    }
    for (int i = 0; i < node->input_count; i++) {
        append_word(secondary, sizeof secondary, &used, node->inputs[i].name);
    }
    for (int i = 0; i < node->output_count; i++) {
        append_word(secondary, sizeof secondary, &used, node->outputs[i].name);
    }
    return text_search_word_score(query, node->name, secondary);
}

/* A binding reads as "Set UI Data items", and also as its document and its
   rows' fields. */
static int binding_score(const node_match *match, const char *query)
{
    const ui_data_binding *b = match->binding;
    char name[128], secondary[512];
    snprintf(name, sizeof name, "%s %s", match->desc->name, b->binding.name);
    snprintf(
        secondary,
        sizeof secondary,
        "%s self %s %s %s",
        match->desc->keywords,
        b->document,
        b->binding.list ? "list rows data-for" : "value",
        b->binding.fields
    );
    return text_search_word_score(query, name, secondary);
}

static int match_score(const node_match *match, const char *query)
{
    return match->binding     ? binding_score(match, query)
         : match->preset      ? preset_score(match->preset, query)
         : match_event(match) ? event_score(match, query)
                              : node_score(match->desc, query);
}

/* How Behavior Expression writes a node over its data inputs: "clamp(Value,
 * Min, Max)", "A + B" or "-Value". A component node reads as the node it
 * lowers to, so a Make vec2 is "vec2(x, y)". */
static bool expression_form(const graph *g, const node *n, char *text, size_t size)
{
    const node_desc *desc = component_node_target(g, n);
    int binding = 0;
    const char *spelling = desc ? behavior_expression_spelling(desc, &binding) : NULL;
    if (!spelling) {
        return false;
    }
    const char *arguments[8];
    int count = 0;
    for (int i = 0; i < n->input_count && count < 8; i++) {
        const pin_spec *pin = &n->input_pins[i];
        if (pin->kind == PIN_DATA && !(pin->flags & PIN_FLAG_SETTING) && pin->name) {
            arguments[count++] = pin->name;
        }
    }
    if (binding == BEHAVIOR_EXPRESSION_PREFIX_BINDING && count == 1) {
        snprintf(text, size, "%s%s", spelling, arguments[0]);
        return true;
    }
    if (binding && count == 2) {
        snprintf(text, size, "%s %s %s", arguments[0], spelling, arguments[1]);
        return true;
    }
    if (binding) {
        return false;
    }
    /* A function's arguments can take the inputs in another order, as in
     * smoothstep(Min, Max, Value). */
    int used = snprintf(text, size, "%s(", spelling);
    for (int i = 0; i < behavior_expression_arity(desc) && used < (int)size; i++) {
        int input = behavior_expression_argument_input(desc, i);
        assert(input < n->input_count);
        used += snprintf(
            text + used,
            size - (size_t)used,
            "%s%s",
            i ? ", " : "",
            n->input_pins[input].name
        );
    }
    if (used < (int)size) {
        snprintf(text + used, size - (size_t)used, ")");
    }
    return true;
}

/* "name: type", marking containers, wildcards, settings and Self defaults. */
static void add_pin_texts(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    const char *key,
    const pin_spec *pins,
    int count,
    bool settings
)
{
    yyjson_mut_val *list = yyjson_mut_arr(doc);
    for (int i = 0; i < count; i++) {
        const pin_spec *pin = &pins[i];
        bool setting = pin->flags & PIN_FLAG_SETTING;
        if (setting && !settings) {
            continue;
        }
        /* A wildcard keeps its flag once a connection types it. */
        bool unresolved = (pin->flags & PIN_FLAG_WILDCARD) && pin->type == PIN_FLOW;
        const char *type = pin->kind == PIN_EXEC ? "exec"
                         : unresolved            ? "any"
                         : pin->type_name        ? pin->type_name
                                                 : pin_json_name(pin->type);
        const char *name = pin->name ? pin->name : "";
        const char *mark = setting                              ? " (setting)"
                         : (pin->flags & PIN_FLAG_DEFAULT_SELF) ? " = Self"
                                                                : "";
        char text[256];
        if (pin->container == PIN_CONTAINER_SINGLE) {
            snprintf(text, sizeof text, "%s: %s%s", name, type, mark);
        } else {
            const char *container = container_json_name(pin->container);
            snprintf(text, sizeof text, "%s: %s<%s>%s", name, container, type, mark);
        }
        yyjson_mut_arr_add_strcpy(doc, list, text);
    }
    if (yyjson_mut_arr_size(list)) {
        yyjson_mut_obj_add_val(doc, item, key, list);
    }
}

/* A match's sentence: its preset's, its event's or its node's. */
static bool match_description(const node_match *match, char *text, size_t size)
{
    node_subject subject = { .desc = match->desc,
                             .host = match->host,
                             .definition = match->definition };
    if (match->preset) {
        subject.setting = match->preset->setting;
        subject.field = match->preset->field;
    }
    if (match->ui) {
        snprintf(text, size, "%s.", match->ui->description);
        return true;
    }
    const ui_data_binding *b = match->binding;
    if (b && b->binding.list && b->binding.fields[0]) {
        int first = (int)strcspn(b->binding.fields, ",");
        snprintf(
            text,
            size,
            "Gives %s self.%s, a list data-for repeats whose rows bind %s; "
            "Name %s.0.%.*s sets one row's %.*s.",
            b->document,
            b->binding.name,
            b->binding.fields,
            b->binding.name,
            first,
            b->binding.fields,
            first,
            b->binding.fields
        );
        return true;
    }
    if (b) {
        snprintf(
            text,
            size,
            "Gives %s self.%s, %s.",
            b->document,
            b->binding.name,
            b->binding.list ? "a list data-for repeats" : "the value it binds"
        );
        return true;
    }
    return node_subject_describe(&subject, text, size);
}

/* What add_node needs: the type; a preset's setting, by component or type
 * name; an event's name; the expression that writes the node; and its pins. */
static void add_node_match(
    yyjson_mut_doc *doc,
    yyjson_mut_val *nodes,
    graph *g,
    const node_match *match,
    bool pins
)
{
    yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, nodes);
    const node_desc *desc = match->desc;
    yyjson_mut_obj_add_str(doc, item, "type", desc->name);
    node *n = match_node(g, match);
    const char *event = match_event(match);
    if (event) {
        const char *key = desc == &NODE_Event ? "event_name" : "reference_name";
        yyjson_mut_obj_add_strcpy(doc, item, key, event);
    } else if (desc->category && desc->category[0]) {
        yyjson_mut_obj_add_str(doc, item, "category", desc->category);
    }
    if (match->binding) {
        yyjson_mut_val *inputs = yyjson_mut_obj_add_obj(doc, item, "inputs");
        yyjson_mut_obj_add_strcpy(doc, inputs, "Name", match->binding->binding.name);
    }
    if (match->preset) {
        const ecs_component_desc *component = component_by_key(match->preset->setting);
        yyjson_mut_val *inputs = yyjson_mut_obj_add_obj(doc, item, "inputs");
        yyjson_mut_obj_add(
            inputs,
            yyjson_mut_strcpy(doc, n->input_pins[component_node_setting(n)].name),
            yyjson_mut_str(doc, component ? component->name : match->preset->setting)
        );
        if (match->preset->field) {
            yyjson_mut_obj_add_str(doc, item, "field", match->preset->field);
        }
    }
    char description[256];
    if (match_description(match, description, sizeof description)) {
        yyjson_mut_obj_add_strcpy(doc, item, "description", description);
    }
    if (match->wire_input >= 0) {
        yyjson_mut_obj_add_strcpy(
            doc,
            item,
            "wire_input",
            n->input_pins[match->wire_input].name
        );
    }
    if (match->wire_output >= 0) {
        yyjson_mut_obj_add_strcpy(
            doc,
            item,
            "wire_output",
            n->output_pins[match->wire_output].name
        );
    }
    char expression[256];
    if (expression_form(g, n, expression, sizeof expression)) {
        yyjson_mut_obj_add_strcpy(doc, item, "expression", expression);
    }
    if (pins) {
        add_pin_texts(doc, item, "in", n->input_pins, n->input_count, !match->preset);
        add_pin_texts(doc, item, "out", n->output_pins, n->output_count, true);
    }
    int removed = graph_remove_node(g, n);
    assert(!removed);
    (void)removed;
}

/* Without a query or filter, the domain's vocabulary: node types by category;
 * for behaviors the events and the components and struct types that settings
 * take. System graphs name generated nodes directly, so they get patterns. */
static void add_node_outline(
    yyjson_mut_doc *doc,
    yyjson_mut_val *value,
    const char *domain,
    const project_events *project
)
{
    static const char *const generated[] = {
        "Get_<Component>",
        "Set_<Component>",
        "Make_<Type>",
        "Break_<Type>",
    };
    yyjson_mut_val *categories = yyjson_mut_obj_add_obj(doc, value, "categories");
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *node = registry_node(i);
        if (!domain_offers(domain, node, project->studio) ||
            component_generated_kind(node)) {
            continue;
        }
        const char *category = node->category && node->category[0] ? node->category
                                                                   : "Basic";
        yyjson_mut_val *names = yyjson_mut_obj_get(categories, category);
        if (!names) {
            names = yyjson_mut_obj_add_arr(doc, categories, category);
        }
        yyjson_mut_arr_add_str(doc, names, node->name);
    }
    bool system = domain && !strcmp(domain, "system");
    if (!system && !behavior_vocabulary(domain)) {
        return;
    }
    if (system) {
        yyjson_mut_val *patterns = yyjson_mut_obj_add_arr(doc, value, "generated");
        for (size_t i = 0; i < sizeof generated / sizeof *generated; i++) {
            yyjson_mut_arr_add_str(doc, patterns, generated[i]);
        }
    } else {
        yyjson_mut_val *events = yyjson_mut_obj_add_arr(doc, value, "events");
        for (int i = 0; i < HOST_EVENT_COUNT; i++) {
            yyjson_mut_arr_add_str(doc, events, HOST_EVENTS[i].name);
        }
        for (int i = 0; i < acount(project->definitions); i++) {
            yyjson_mut_arr_add_strcpy(doc, events, project->definitions[i]->name);
        }
        for (int i = 0; i < acount(project->ui); i++) {
            yyjson_mut_arr_add_strcpy(doc, events, project->ui[i].name);
        }
    }
    yyjson_mut_val *names = yyjson_mut_obj_add_obj(doc, value, system ? "names" : "settings");
    yyjson_mut_val *components = yyjson_mut_obj_add_arr(doc, names, "Component");
    yyjson_mut_val *types = yyjson_mut_obj_add_arr(doc, names, "Type");
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *node = registry_node(i);
        if (node->component_kind == NODE_COMPONENT_GET_SELF) {
            yyjson_mut_arr_add_str(doc, components, node->component->name);
        }
        if (node->type && node->pure_kind == NODE_PURE_MAKE) {
            yyjson_mut_arr_add_str(doc, types, node->type->name);
        }
    }
}

/* Registry nodes, then component node presets, host events, the project's
 * event definitions as Event and Send Event, and its ui events as Event. */
static node_match *node_candidates(
    const char *domain,
    const component_node_preset *presets,
    const project_events *project
)
{
    node_match *matches = NULL;
    for (int i = 0; i < registry_node_count(); i++) {
        if (domain_offers(domain, registry_node(i), project->studio)) {
            apush(matches, ((node_match){ .desc = registry_node(i) }));
        }
    }
    if (!behavior_vocabulary(domain)) {
        return matches;
    }
    for (int i = 0; i < acount(presets); i++) {
        apush(matches, ((node_match){ .desc = presets[i].desc, .preset = &presets[i] }));
    }
    for (int i = 0; i < HOST_EVENT_COUNT; i++) {
        apush(matches, ((node_match){ .desc = &NODE_Event, .host = &HOST_EVENTS[i] }));
    }
    const node_desc *send = registry_node_by_name("Send Event");
    for (int i = 0; i < acount(project->definitions); i++) {
        behavior_asset *definition = project->definitions[i];
        apush(matches, ((node_match){ .desc = &NODE_Event, .definition = definition }));
        apush(matches, ((node_match){ .desc = send, .definition = definition }));
    }
    for (int i = 0; i < acount(project->ui); i++) {
        apush(matches, ((node_match){ .desc = &NODE_Event, .ui = &project->ui[i] }));
    }
    const node_desc *set_data = registry_node_by_name("Set UI Data");
    for (int i = 0; i < acount(project->bindings); i++) {
        apush(matches, ((node_match){ .desc = set_data, .binding = &project->bindings[i] }));
    }
    return matches;
}

/* A pin type as searchNodes writes it: "exec", "any", "number", "Health",
 * "ecs.entity" or "array<int>". */
static bool wire_pin(const char *text, pin_spec *out)
{
    *out = (pin_spec){ .type = PIN_FLOW };
    char base[128];
    const char *open = strchr(text, '<');
    size_t length = strlen(text);
    if (open) {
        const char *containers[] = { "array", "set", "map" };
        const pin_container kinds[] = { PIN_CONTAINER_ARRAY,
                                        PIN_CONTAINER_SET,
                                        PIN_CONTAINER_MAP };
        size_t prefix = (size_t)(open - text);
        bool known = false;
        for (int i = 0; i < 3; i++) {
            if (strlen(containers[i]) == prefix &&
                !strncmp(text, containers[i], prefix)) {
                out->container = kinds[i];
                known = true;
            }
        }
        if (!known || text[length - 1] != '>' || length - prefix - 2 >= sizeof base) {
            return false;
        }
        snprintf(base, sizeof base, "%.*s", (int)(length - prefix - 2), open + 1);
    } else if (length >= sizeof base) {
        return false;
    } else {
        snprintf(base, sizeof base, "%s", text);
    }
    if (!strcmp(base, "exec")) {
        out->kind = PIN_EXEC;
        return !open;
    }
    if (!strcmp(base, "any")) {
        out->flags = PIN_FLAG_WILDCARD;
        return true;
    }
    for (int i = PIN_BOOL; i < PIN_HANDLE; i++) {
        if (!strcmp(base, pin_type_name((pin_type)i))) {
            out->type = (pin_type)i;
            return true;
        }
    }
    /* A handle is named by the pins that carry it, such as ecs.entity. */
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *node = registry_node(i);
        for (int p = 0; p < node->input_count; p++) {
            if (node->inputs[p].type == PIN_HANDLE && node->inputs[p].type_name &&
                !strcmp(node->inputs[p].type_name, base)) {
                out->type = PIN_HANDLE;
                out->type_name = node->inputs[p].type_name;
                return true;
            }
        }
    }
    const type_desc *type = type_catalog_find(base);
    const enum_desc *enumeration = type_catalog_enum_find(base);
    out->type = type ? PIN_STRUCT : PIN_ENUM;
    out->type_name = type ? type->name : enumeration ? enumeration->name : NULL;
    return out->type_name != NULL;
}

/* The best pin on one side of `n` for a wire: into an input from `from`, or
 * out of an output to `to`. Returns its fit and sets `pin`, or 0 and -1. */
static int best_wire(const pin_sides *n, const pin_spec *from, const pin_spec *to, int *pin)
{
    const pin_spec *pins = from ? n->inputs : n->outputs;
    int count = from ? n->input_count : n->output_count, best = 0;
    *pin = -1;
    for (int i = 0; i < count; i++) {
        int fit = from ? wire_fit(from, &pins[i]) : wire_fit(&pins[i], to);
        if (fit > best) {
            best = fit;
            *pin = i;
        }
    }
    return best;
}

static int handle_search_nodes(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *query = request_string(request, "query");
    const char *domain = request_string(request, "domain");
    pin_filter filter = { .pin_type = request_string(request, "pin_type"),
                          .pin_container = request_string(request, "pin_container"),
                          .pin_kind = request_string(request, "pin_kind"),
                          .pure = yyjson_obj_get(request, "pure") };
    yyjson_val *limit_value = yyjson_obj_get(request, "limit");
    yyjson_val *offset_value = yyjson_obj_get(request, "offset");
    yyjson_val *pins_value = yyjson_obj_get(request, "include_pins");
    const char *wire_from_text = request_string(request, "wire_from");
    const char *wire_to_text = request_string(request, "wire_to");
    const char *behavior = request_string(request, "behavior");
    pin_spec wire_from, wire_to;
    if ((wire_from_text && !wire_pin(wire_from_text, &wire_from)) ||
        (wire_to_text && !wire_pin(wire_to_text, &wire_to)) ||
        (yyjson_obj_get(request, "wire_from") && !wire_from_text) ||
        (yyjson_obj_get(request, "wire_to") && !wire_to_text)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "wire_from and wire_to take a pin type as searchNodes writes it, such as exec, float, Health or array<int>");
        return -1;
    }
    if ((yyjson_obj_get(request, "query") && !query) ||
        (yyjson_obj_get(request, "domain") &&
         (!domain || (strcmp(domain, "shader") && strcmp(domain, "behavior") &&
                      strcmp(domain, "field") && strcmp(domain, "system") &&
                      strcmp(domain, "model")))) ||
        (yyjson_obj_get(request, "pin_type") &&
         (!filter.pin_type || strlen(filter.pin_type) > 128)) ||
        (yyjson_obj_get(request, "pin_container") &&
         (!filter.pin_container || (strcmp(filter.pin_container, "single") &&
                                    strcmp(filter.pin_container, "array") &&
                                    strcmp(filter.pin_container, "set") &&
                                    strcmp(filter.pin_container, "map")))) ||
        (yyjson_obj_get(request, "pin_kind") &&
         (!filter.pin_kind ||
          (strcmp(filter.pin_kind, "data") && strcmp(filter.pin_kind, "exec")))) ||
        (limit_value && (!yyjson_is_int(limit_value) || yyjson_get_sint(limit_value) < 1 ||
                         yyjson_get_sint(limit_value) > 50)) ||
        (offset_value &&
         (!yyjson_is_int(offset_value) || yyjson_get_sint(offset_value) < 0 ||
          yyjson_get_sint(offset_value) > 100000)) ||
        (pins_value && !yyjson_is_bool(pins_value)) ||
        (filter.pure && !yyjson_is_bool(filter.pure)) ||
        (query && strlen(query) > 256) ||
        (yyjson_obj_get(request, "behavior") && (!behavior || !behavior[0]))) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "invalid node search query, pin filter, page, include_pins or behavior");
        return -1;
    }
    project_events events = { .studio = studio_project_is(root) };
    char error[256] = "";
    int status = behavior_vocabulary(domain)
                   ? load_project_events(root, behavior, &events, error, sizeof error)
                   : CHIBI_AUTOMATION_OK;
    if (status != CHIBI_AUTOMATION_OK) {
        free_project_events(&events);
        response_error(result, exit_code, status, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    bool searched = query && query[0];
    bool pin_filtered = filter.pin_type || filter.pin_container ||
                        filter.pin_kind || filter.pure;
    bool wired = wire_from_text || wire_to_text;
    if (!searched && !pin_filtered && !wired) {
        add_node_outline(result->doc, value, domain, &events);
        free_project_events(&events);
        return 0;
    }
    int limit = limit_value ? (int)yyjson_get_sint(limit_value) : 10;
    int offset = offset_value ? (int)yyjson_get_sint(offset_value) : 0;
    bool pins = !pins_value || yyjson_get_bool(pins_value);
    /* Presets and events need words or a wire to rank; a pin filter alone
     * lists nodes. */
    bool listed = searched || wired;
    component_node_preset *presets = listed ? component_node_presets() : NULL;
    node_match *candidates = node_candidates(domain, presets, &events);
    behavior_asset *scratch = behavior_asset_new("Node search");
    node_match *matches = NULL;
    for (int i = 0; i < acount(candidates); i++) {
        node_match match = candidates[i];
        bool offered = listed || (!match_event(&match) && !match.binding);
        match.order = i;
        match.score = offered ? match_score(&match, query) : -1;
        match.wire_input = match.wire_output = -1;
        if (match.score < 0) {
            continue;
        }
        if (pin_filtered || wired) {
            bool configured = match.preset || match_event(&match) || match.binding;
            node *n = configured ? match_node(scratch->graph, &match) : NULL;
            pin_sides sides = n ? (pin_sides){ n->input_pins,
                                               n->output_pins,
                                               n->input_count,
                                               n->output_count }
                                : (pin_sides){ match.desc->inputs,
                                               match.desc->outputs,
                                               match.desc->input_count,
                                               match.desc->output_count };
            bool kept = pins_match(&sides, &filter);
            int into = wire_from_text
                         ? best_wire(&sides, &wire_from, NULL, &match.wire_input)
                         : 3;
            int out = wire_to_text
                        ? best_wire(&sides, NULL, &wire_to, &match.wire_output)
                        : 3;
            match.fit = wired ? (into < out ? into : out) : 0;
            int removed = n ? graph_remove_node(scratch->graph, n) : 0;
            assert(!removed);
            (void)removed;
            if (!kept || (wired && !match.fit)) {
                continue;
            }
        }
        apush(matches, match);
    }
    if (acount(matches) > 1) {
        asort(matches, node_match_compare);
    }
    yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(result->doc, value, "nodes");
    int total = acount(matches);
    for (int i = offset; i < total && i < offset + limit; i++) {
        add_node_match(result->doc, nodes, scratch->graph, &matches[i], pins);
    }
    int count = total > offset ? (total - offset < limit ? total - offset : limit) : 0;
    yyjson_mut_obj_add_int(result->doc, value, "total", total);
    yyjson_mut_obj_add_int(result->doc, value, "offset", offset);
    yyjson_mut_obj_add_int(result->doc, value, "count", count);
    yyjson_mut_obj_add_bool(result->doc, value, "has_more", offset + count < total);
    afree(matches);
    afree(candidates);
    afree(presets);
    behavior_asset_free(scratch);
    free_project_events(&events);
    return 0;
}

static int handle_preview_field(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char *json = NULL;
    size_t bytes;
    uint64_t hash;
    char error[256] = { 0 };
    int status = load_asset(root, request, &ref, &json, &bytes, &hash, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    behavior_asset *authored = load_project_behavior(root, json);
    free(json);
    if (!authored || authored->domain != BEHAVIOR_DOMAIN_FIELD) {
        behavior_asset_free(authored);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "Invalid field asset");
        return -1;
    }
    behavior_asset *asset = link_source_libraries(root, authored, error, sizeof error);
    behavior_asset_free(authored);
    if (!asset) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    bool valid = automation_field_preview(asset, request, result->doc, value, error, sizeof error);
    behavior_asset_free(asset);
    if (!valid) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }

    return 0;
}

static int handle_bake_material(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char *text = NULL;
    size_t bytes;
    uint64_t hash;
    char error[256] = { 0 };
    int status = load_asset(root, request, &ref, &text, &bytes, &hash, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    behavior_asset *authored = load_project_behavior(root, text);
    free(text);
    if (!authored || authored->domain != BEHAVIOR_DOMAIN_MATERIAL) {
        const char *message = authored
                                ? "bake_material requires a material asset"
                                : graph_last_error();
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, message);
        behavior_asset_free(authored);
        return -1;
    }
    behavior_asset *asset = link_project_libraries(root, authored, error, sizeof error);
    behavior_asset_free(authored);
    if (!asset) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    bool valid = automation_material_bake(asset, request, result->doc, value, error, sizeof error);
    behavior_asset_free(asset);
    if (!valid) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }
    return 0;
}

static int validate_shader_binding(const char *root, const Shader *binding, char *error, size_t error_size)
{
    if (!binding) {
        return CHIBI_AUTOMATION_OK;
    }
    behavior_asset **sources = NULL;
    int status = load_model_catalog(root, &sources, error, error_size);
    if (status == CHIBI_AUTOMATION_OK) {
        behavior_asset **libraries = NULL;
        status = load_behavior_family_assets(root, family_by_kind("shader-library"), &libraries, error, error_size);
        for (int i = 0; i < acount(libraries); i++) {
            apush(sources, libraries[i]);
        }
        afree(libraries);
    }
    const char *names[] = { binding->material, binding->shape };
    const char *kinds[] = { "material", "shape" };
    for (int i = 0; i < 2 && status == CHIBI_AUTOMATION_OK; i++) {
        if (!names[i] || !*names[i]) {
            continue;
        }
        bool model = false;
        if (i == 0 || i == 1) {
            for (int m = 0; m < acount(sources); m++) {
                model |= sources[m]->domain == (i == 0 ? BEHAVIOR_DOMAIN_MATERIAL
                                                       : BEHAVIOR_DOMAIN_MODEL) &&
                         !strcmp(sources[m]->name, names[i]);
                if (i == 0 && sources[m]->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
                    size_t length = strlen(sources[m]->name);
                    model |= !strncmp(names[i], sources[m]->name, length) &&
                             names[i][length] == '.' &&
                             behavior_asset_find_graph_by_name(sources[m], names[i] + length + 1);
                }
            }
        }
        if (model) {
            continue;
        }
        asset_ref ref = { 0 };
        char *text = NULL;
        status = load_named_asset(root, family_by_kind(kinds[i]), names[i], &ref, &text, NULL, NULL, error, error_size);
        if (status == CHIBI_AUTOMATION_OK) {
            behavior_asset *asset = load_project_behavior(root, text);
            if (!asset) {
                snprintf(error, error_size, "%s", graph_last_error());
                status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            } else {
                behavior_asset *linked = link_project_libraries(root, asset, error, error_size);
                behavior_asset_free(asset);
                if (!linked) {
                    status = CHIBI_AUTOMATION_VALIDATION_ERROR;
                } else {
                    apush(sources, linked);
                }
            }
        }
        free(text);
    }
    if (status == CHIBI_AUTOMATION_OK) {
        shader_error diagnostic = { 0 };
        if (!render_shader_binding_sources_validate(
                binding,
                binding->parameters,
                (const behavior_asset *const *)sources,
                acount(sources),
                &diagnostic
            )) {
            snprintf(error, error_size, "Shader.parameters: %s", diagnostic.message);
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
    }
    free_behavior_assets(sources);
    return status;
}

/* A Ui component names a UI document the project has. */
static int validate_ui_binding(const char *root, const Ui *ui, char *error, size_t error_size)
{
    if (!ui || !ui->document || !ui->document[0] || ui_exists(root, ui->document)) {
        return CHIBI_AUTOMATION_OK;
    }
    set_errorf(error, error_size, "Unknown UI document '%s'", ui->document);
    return CHIBI_AUTOMATION_VALIDATION_ERROR;
}

/* The shader bindings and UI documents of a resolved prototype and its
 * children resolve. */
static int spawned_bindings(const char *root, const prototype *prototype, char *error, size_t error_size)
{
    int status = CHIBI_AUTOMATION_OK;
    for (int i = 0; i < acount(prototype->components) && status == CHIBI_AUTOMATION_OK;
         i++) {
        const prototype_component *component = &prototype->components[i];
        if (!strcmp(component->desc->name, "Shader")) {
            status = validate_shader_binding(root, component->data, error, error_size);
        } else if (!strcmp(component->desc->name, "Ui")) {
            status = validate_ui_binding(root, component->data, error, error_size);
        }
    }
    for (int i = 0;
         i < acount(prototype->child_prototypes) && status == CHIBI_AUTOMATION_OK;
         i++) {
        status = spawned_bindings(root, prototype->child_prototypes[i], error, error_size);
    }
    return status;
}

/* A prototype's shader bindings and UI documents resolve, its children's
 * included. */
static int validate_prototype_bindings(const char *root, const char *text, char *error, size_t error_size)
{
    prototype_includes includes = { .root = root };
    prototype_registry registry = registry_with_includes(&includes);
    prototype *prototype = prototype_load(text, &registry, error, error_size);
    if (!prototype) {
        prototype_includes_free(&includes);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    int status = spawned_bindings(root, prototype, error, error_size);
    prototype_free(prototype);
    prototype_includes_free(&includes);
    return status;
}

/* Adds each behavior the prototype's spawn attaches, its own and its
 * children's, to `names` once. */
static void spawned_behaviors(const prototype *prototype, const char ***names)
{
    for (int i = 0; i < acount(prototype->graphs); i++) {
        bool seen = false;
        for (int j = 0; j < acount(*names); j++) {
            seen |= !strcmp((*names)[j], prototype->graphs[i]);
        }
        if (!seen) {
            apush(*names, prototype->graphs[i]);
        }
    }
    for (int i = 0; i < acount(prototype->child_prototypes); i++) {
        spawned_behaviors(prototype->child_prototypes[i], names);
    }
}

static int validate_prototype_behaviors(const char *root, const prototype *prototype, char *error, size_t error_size)
{
    const char **names = NULL;
    spawned_behaviors(prototype, &names);
    bool fallout = false;
    int status = CHIBI_AUTOMATION_OK;
    for (int i = 0; status == CHIBI_AUTOMATION_OK && i < acount(names); i++) {
        if (!names[i][0]) {
            snprintf(error, error_size, "prototype behavior names must be non-empty strings");
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            continue;
        }
        asset_ref ref = { 0 };
        char *behavior_json = NULL;
        status = load_named_asset(
            root,
            family_by_kind("behavior"),
            names[i],
            &ref,
            &behavior_json,
            NULL,
            NULL,
            error,
            error_size
        );
        free(behavior_json);
        if (status == CHIBI_AUTOMATION_OK) {
            status = past_fallout(
                validate_scene_behavior(root, names[i], error, error_size),
                error,
                &fallout
            );
        }
    }
    afree(names);
    return status == CHIBI_AUTOMATION_OK ? fallout_status(fallout, error, error_size)
                                         : status;
}

static int validate_scene_behavior(const char *root, const char *name, char *error, size_t error_size)
{
    authoring_language_catalog *native = source_native_catalog(root, error, error_size);
    if (!native) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    script_lower_bindings bindings = authoring_language_bindings(native);
    authoring_sources *sources = authoring_sources_prepare(
        chibi_automation_content(root),
        authoring_language_catalog_view(native),
        &bindings,
        behavior_default_backend()
    );
    bool found = false;
    bool valid = authoring_sources_valid(sources);
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (source->analysis && source->analysis->name &&
            !strcmp(source->analysis->name, name)) {
            found = source->analysis->kind == SCRIPT_PROGRAM_BEHAVIOR;
            if (!source->valid && source->diagnostic_count) {
                set_error(error, error_size, source->diagnostics[0].message);
            }
        }
    }
    if (!found) {
        /* The catalog's words, so validate_project counts it once. */
        snprintf(error, error_size, "Unknown behavior or system '%s'", name);
    } else if (!valid && !error[0]) {
        set_error(error, error_size, "Project source preparation failed");
    }
    authoring_sources_free(sources);
    authoring_language_catalog_free(native);
    return found && valid ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_VALIDATION_ERROR;
}

/* Every entity path a scene's component values name is one of its entities'. */
static int validate_scene_paths(
    const scene_document *scene,
    const prototype_registry *registry,
    char *error,
    size_t error_size
)
{
    int status = CHIBI_AUTOMATION_OK;
    for (int i = 0; i < acount(scene->entities) && status == CHIBI_AUTOMATION_OK; i++) {
        const scene_entity *entity = &scene->entities[i];
        prototype_link *links = entity->components
                                  ? prototype_components_links(registry, entity->components)
                                  : NULL;
        for (int j = 0; j < acount(links) && status == CHIBI_AUTOMATION_OK; j++) {
            if (scene_document_entity_index(scene, links[j].path) < 0) {
                snprintf(
                    error,
                    error_size,
                    "Scene entity '%s': %s %s names '%s', but no entity has "
                    "that path",
                    entity->name,
                    links[j].component->name,
                    links[j].component->type->fields[links[j].field].name,
                    links[j].path
                );
                status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            }
        }
        prototype_links_free(links);
    }
    return status;
}

static int validate_scene_dependencies(const char *root, const char *json, char *error, size_t error_size)
{
    scene_document *scene = scene_document_load(json, error, error_size);
    if (!scene) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    if (scene->ui && !ui_exists(root, scene->ui)) {
        set_errorf(error, error_size, "Unknown UI document '%s'", scene->ui);
        scene_document_free(scene);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    prototype_includes includes = { .root = root };
    prototype_registry registry = registry_with_includes(&includes);
    int status = validate_scene_paths(scene, &registry, error, error_size);
    bool fallout = false;
    for (int i = 0; i < acount(scene->entities) && status == CHIBI_AUTOMATION_OK; i++) {
        scene_entity *entity = &scene->entities[i];
        asset_ref prototype_ref = { 0 };
        char *prototype_json = NULL;
        status = load_named_asset(
            root,
            family_by_kind("prototype"),
            entity->prototype,
            &prototype_ref,
            &prototype_json,
            NULL,
            NULL,
            error,
            error_size
        );
        if (status == CHIBI_AUTOMATION_NOT_FOUND) {
            set_errorf(error, error_size, "Unknown prototype '%s'", entity->prototype);
        }
        if (status != CHIBI_AUTOMATION_OK) {
            free(prototype_json);
            break;
        }
        prototype *prototype = prototype_load(prototype_json, &registry, error, error_size);
        if (!prototype) {
            free(prototype_json);
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            break;
        }
        status = past_fallout(
            validate_prototype_behaviors(root, prototype, error, error_size),
            error,
            &fallout
        );
        free(prototype_json);
        if (status == CHIBI_AUTOMATION_OK) {
            ecs_world *world = ecs_world_new();
            for (int component = 0; component < registry.component_count &&
                                    status == CHIBI_AUTOMATION_OK;
                 component++) {
                if (ecs_world_register_component(world, registry.components[component], NULL) !=
                    ECS_OK) {
                    snprintf(error, error_size, "could not register scene components");
                    status = CHIBI_AUTOMATION_EXECUTION_ERROR;
                }
            }
            ecs_entity actor = { 0 };
            if (status == CHIBI_AUTOMATION_OK &&
                prototype_spawn_components(prototype, world, &actor, error, error_size) !=
                    ECS_OK) {
                status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            }
            char **component_strings = NULL;
            if (status == CHIBI_AUTOMATION_OK && entity->components &&
                prototype_apply_components_value(
                    world,
                    actor,
                    entity->components,
                    &registry,
                    &component_strings,
                    error,
                    error_size
                ) != 0) {
                status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            }
            if (status == CHIBI_AUTOMATION_OK) {
                status = validate_shader_binding(root, Shader_get_const(world, actor), error, error_size);
            }
            if (status == CHIBI_AUTOMATION_OK) {
                status = validate_ui_binding(root, Ui_get_const(world, actor), error, error_size);
            }
            ecs_world_free(world);
            prototype_owned_strings_free(component_strings);
        }
        prototype_free(prototype);
        for (int behavior = 0;
             behavior < acount(entity->behaviors) && status == CHIBI_AUTOMATION_OK;
             behavior++) {
            status = past_fallout(
                validate_scene_behavior(root, entity->behaviors[behavior], error, error_size),
                error,
                &fallout
            );
        }
    }
    scene_document_free(scene);
    prototype_includes_free(&includes);
    return status != CHIBI_AUTOMATION_OK
             ? status
             : fallout_status(fallout, error, error_size);
}

static int load_ui_source(
    const char *root,
    yyjson_val *request,
    asset_ref *ref,
    char **source,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
)
{
    if (resolve_asset_ref(request, ref, error, error_size) ||
        ref->family->loader != ASSET_UI) {
        if (!error[0]) {
            snprintf(error, error_size, "this operation takes a ui asset");
        }
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    return load_asset(root, request, ref, source, bytes, hash, error, error_size);
}

static int handle_inspect_ui(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char error[256] = { 0 }, *text = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    int status = load_ui_source(root, request, &ref, &text, &bytes, &hash, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    char *name = asset_name_from_filename(ref.filename);
    char *rcss = ui_styles(root, name);
    automation_ui_inspect(name, text, rcss, result->doc, value);
    yyjson_val *complete = yyjson_obj_get(request, "complete");
    if (complete) {
        status = automation_ui_complete(
            chibi_automation_content(root),
            text,
            rcss,
            complete,
            result->doc,
            value,
            error,
            sizeof error
        );
    }
    free(rcss);
    free(name);
    free(text);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    return 0;
}

static int operation_requires_root(const char *operation)
{
    return !strcmp(operation, "execution_status") ||
           !strcmp(operation, "describe_components") ||
           !strcmp(operation, "describe_registry") ||
           !strcmp(operation, "profile_scenario") || !strcmp(operation, "status") ||
           !strcmp(operation, "list_assets") || !strcmp(operation, "read_asset") ||
           !strcmp(operation, "validate_asset") ||
           !strcmp(operation, "validate_project") ||
           !strcmp(operation, "new_asset") || !strcmp(operation, "bake_model") ||
           !strcmp(operation, "bake_material") ||
           !strcmp(operation, "preview_field") ||
           !strcmp(operation, "import_sky") || !strcmp(operation, "import_model") ||
           !strcmp(operation, "search_nodes") || !strcmp(operation, "catalog") ||
           !strcmp(operation, "search_assets") ||
           !strcmp(operation, "asset_dependencies") ||
           !strcmp(operation, "find_references") ||
           !strcmp(operation, "call_hierarchy") ||
           !strcmp(operation, "inspect_graph") ||
           !strcmp(operation, "inspect_system") ||
           !strcmp(operation, "run_scenario") ||
           !strcmp(operation, "save_terrain") || !strcmp(operation, "load_terrain") ||
           !strcmp(operation, "runtime_snapshot") ||
           !strcmp(operation, "record_scenario") ||
           !strcmp(operation, "replay_scenario") || !strcmp(operation, "inspect_ui");
}

char *chibi_automation_handle(const char *request, const char *default_root, int *exit_code)
{
    if (exit_code) {
        *exit_code = CHIBI_AUTOMATION_OK;
    }
    yyjson_doc *document = request ? yyjson_read(request, strlen(request), 0) : NULL;
    if (!document || !yyjson_is_obj(yyjson_doc_get_root(document))) {
        response result = response_new("");
        response_error(&result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "request must be a JSON object");
        if (document) {
            yyjson_doc_free(document);
        }
        return response_finish(&result);
    }
    yyjson_val *root_value = yyjson_doc_get_root(document);
    uint64_t contract = 0;
    const char *operation = request_string(root_value, "operation");
    response result = response_new(operation);
    if (!request_uint(root_value, "contract", &contract) ||
        contract != CHIBI_AUTOMATION_CONTRACT) {
        response_error(&result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "unsupported or missing contract");
        yyjson_doc_free(document);
        return response_finish(&result);
    }
    if (!operation || !operation[0]) {
        response_error(&result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "operation is required");
        yyjson_doc_free(document);
        return response_finish(&result);
    }
    if (!operation_by_name(operation)) {
        response_error(&result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "unknown operation");
        yyjson_doc_free(document);
        return response_finish(&result);
    }
    if (yyjson_obj_get(root_value, "save_path") &&
        strcmp(operation, "save_terrain") && strcmp(operation, "load_terrain") &&
        strcmp(operation, "run_scenario") && strcmp(operation, "runtime_snapshot")) {
        response_error(&result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "save_path is supported only by terrain storage and runtime scenario operations");
        yyjson_doc_free(document);
        return response_finish(&result);
    }
    char *root = NULL;
    /* Unsaved source analysis needs no project, but sees its components. */
    bool optional_root = !strcmp(operation, "analyze_code");
    if (operation_requires_root(operation) || optional_root) {
        char error[256] = { 0 };
        root = root_path(request_string(root_value, "project_root"), default_root, error, sizeof error);
        if (!root && !optional_root) {
            response_error(&result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
            yyjson_doc_free(document);
            return response_finish(&result);
        }
    }
    declared_components *declared = NULL;
    if (root) {
        char error[512] = { 0 };
        bool metadata = !strcmp(operation, "validate_project") ||
                        !strcmp(operation, "status");
        if (!open_content_mode(root, metadata, error, sizeof error) ||
            !load_declared(root, &declared, error, sizeof error)) {
            response_error(&result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
            free(root);
            yyjson_doc_free(document);
            return response_finish(&result);
        }
    }
    scenario_context image_context = { 0 };
    bool needs_images = root && (!strcmp(operation, "validate_asset") ||
                                 !strcmp(operation, "validate_project") ||
                                 !strcmp(operation, "inspect_graph") ||
                                 !strcmp(operation, "bake_model") ||
                                 !strcmp(operation, "bake_material"));
    if (needs_images) {
        char error[256] = { 0 };
        int status = scenario_mount_content(root, &image_context, error, sizeof error);
        if (status != CHIBI_AUTOMATION_OK) {
            response_error(&result, exit_code, status, error);
            scenario_context_free(&image_context);
            declared_components_free(declared);
            free(root);
            yyjson_doc_free(document);
            return response_finish(&result);
        }
    }
    if (!strcmp(operation, "execution_status")) {
        handle_execution_status(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "profile_scenario")) {
        handle_profile_scenario(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "search_nodes")) {
        handle_search_nodes(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "describe_components")) {
        handle_describe_components(root_value, &result, exit_code);
    } else if (!strcmp(operation, "preview_field")) {
        handle_preview_field(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "bake_material")) {
        handle_bake_material(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "preview_shader")) {
        response_error(&result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "preview_shader requires chibi_ctl --render");
    } else if (!strcmp(operation, "describe_contract")) {
        handle_describe_contract(&result);
    } else if (!strcmp(operation, "status")) {
        handle_status(root, &result, exit_code);
    } else if (!strcmp(operation, "list_assets")) {
        handle_list_assets(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "analyze_code")) {
        char error[256] = { 0 };
        yyjson_mut_val *value = yyjson_mut_obj_add_obj(result.doc, result.root, "result");
        /* A root that does not exist holds no source but the one analyzed. */
        const char *analysis_root = root ? root
                                         : request_string(root_value, "project_root");
        if (!analysis_root) {
            analysis_root = default_root;
        }
        int status = root || open_content(analysis_root, error, sizeof error)
                       ? CHIBI_AUTOMATION_OK
                       : CHIBI_AUTOMATION_VALIDATION_ERROR;
        if (status == CHIBI_AUTOMATION_OK) {
            status = analyze_project_source(
                analysis_root,
                root_value,
                true,
                result.doc,
                value,
                error,
                sizeof error
            );
        }
        if (status != CHIBI_AUTOMATION_OK) {
            response_error(&result, exit_code, status, error);
        }
    } else if (!strcmp(operation, "read_asset")) {
        handle_read_asset(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "validate_asset")) {
        handle_validate_asset(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "validate_project")) {
        handle_validate_project(root, &result, exit_code);
    } else if (!strcmp(operation, "new_project")) {
        handle_new_project(root_value, &result, exit_code);
    } else if (!strcmp(operation, "new_asset")) {
        handle_new_asset(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "catalog")) {
        handle_catalog(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "search_assets")) {
        handle_search_assets(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "asset_dependencies")) {
        handle_asset_dependencies(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "find_references") || !strcmp(operation, "call_hierarchy")) {
        handle_navigation(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "bake_model")) {
        handle_bake_model(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "import_sky")) {
        handle_import_sky(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "import_model")) {
        handle_import_model(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "inspect_graph")) {
        handle_inspect_graph(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "inspect_system")) {
        handle_inspect_system(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "run_scenario")) {
        handle_run_scenario(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "save_terrain") || !strcmp(operation, "load_terrain")) {
        handle_terrain_storage(root, root_value, !strcmp(operation, "load_terrain"), &result, exit_code);
    } else if (!strcmp(operation, "runtime_snapshot")) {
        handle_runtime_snapshot(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "record_scenario")) {
        handle_record_scenario(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "replay_scenario")) {
        handle_replay_scenario(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "read_recording")) {
        handle_read_recording(root_value, &result, exit_code);
    } else if (!strcmp(operation, "inspect_ui")) {
        handle_inspect_ui(root, root_value, &result, exit_code);
    } else {
        handle_describe_registry(root_value, &result, exit_code);
    }
    scenario_context_free(&image_context);
    free(root);
    yyjson_doc_free(document);
    /* The response borrows declared component names. */
    char *text = response_finish(&result);
    declared_components_free(declared);
    return text;
}
