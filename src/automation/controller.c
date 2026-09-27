#include "automation/controller.h"

#include "assets/project.h"
#include "assets/prototype.h"
#include "assets/refactor.h"
#include "assets/reference_index.h"
#include "assets/scene_document.h"
#include "assets/system_document.h"
#include "assets/ui_document.h"
#include "assets/ui_events.h"
#include "automation/asset_write.h"
#include "automation/field_preview.h"
#include "automation/graph_edit.h"
#include "automation/graph_optimize.h"
#include "automation/navigation.h"
#include "automation/prototype_edit.h"
#include "automation/scene_edit.h"
#include "automation/ui_edit.h"
#include "ecs/ecs.h"
#include "editor/documents/component_presentation.h"
#include "graph/authoring/describe.h"
#include "graph/authoring/layout.h"
#include "graph/compiler/behavior_program.h"
#include "graph/compiler/compile.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/document/events.h"
#include "graph/document/graph_signature.h"
#include "graph/document/library.h"
#include "graph/document/problems.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/runtime/instance.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "runtime/systems/system.h"
#include "runtime/ui.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/text_search.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef enum
{
    ASSET_GRAPH,
    ASSET_SYSTEM,
    ASSET_PROTOTYPE,
    ASSET_SCENE,
    ASSET_EDITOR,
    ASSET_UI,
} asset_loader;

typedef struct
{
    const char *kind;
    const char *folder;
    const char *extension; /* of its files: YAML, or a ui asset's markup */
    asset_loader loader;
    reference_asset_kind reference; /* KIND_COUNT for assets nothing references */
} asset_family;

static const asset_family ASSET_FAMILIES[] = {
    { "behavior", "behaviors", ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_BEHAVIOR },
    { "event", "events", ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_EVENT },
    { "library", "libraries", ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_LIBRARY },
    { "material", "materials", ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_MATERIAL },
    { "shape", "shapes", ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_SHAPE },
    { "field", "fields", ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_FIELD },
    { "shader-library", "shader-libraries", ASSET_EXTENSION, ASSET_GRAPH, REFERENCE_ASSET_LIBRARY },
    { "system", "systems", ASSET_EXTENSION, ASSET_SYSTEM, REFERENCE_ASSET_SYSTEM },
    { "prototype", "prototypes", ASSET_EXTENSION, ASSET_PROTOTYPE, REFERENCE_ASSET_PROTOTYPE },
    { "scene", "scenes", ASSET_EXTENSION, ASSET_SCENE, REFERENCE_ASSET_SCENE },
    { "editor", "editor", ASSET_EXTENSION, ASSET_EDITOR, REFERENCE_ASSET_KIND_COUNT },
    { "ui", "ui", UI_MARKUP_EXTENSION, ASSET_UI, REFERENCE_ASSET_UI },
};

typedef struct
{
    const asset_family *family;
    char filename[256];
    char relative[512];
} asset_ref;

typedef struct
{
    const asset_family *family;
    char *filename;
    char *relative;
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
    asset_entry *entries;
    catalog_asset *assets;
    reference_index *references;
    uint64_t revision;
    uint64_t content_hash;
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
    { "inputs", "array", false, "Per-frame movement, aim, controls, actions, UI interactions and typed entity-targeted events." },
    { "input_timeline", "array", false, "At most 1800 sparse {frame,input} samples in increasing frame order; alternative to inputs." },
    { "saved_state", "object", false, "Isolated initial committed text values; never accesses player save files." },
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
    { "fields", "string[]", false, "Optional reflected field selectors such as GameTransform.x." },
    { "inputs", "array", false, "Per-frame movement, aim, controls, actions, UI interactions and typed entity-targeted events." },
    { "input_timeline", "array", false, "At most 1800 sparse {frame,input} samples in increasing frame order; alternative to inputs." },
    { "saved_state", "object", false, "Isolated initial committed text values; never accesses player save files." },
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
    { "wire_from", "string", false, "Type of an output to wire from (exec, float, GameHealth, array<int>); ranks nodes by the input that takes it." },
    { "wire_to", "string", false, "Type of an input to wire into; ranks nodes by the output that feeds it." },
    { "pin_container", "string", false, "Require a matching pin container: single, array, set, or map." },
    { "pin_kind", "string", false, "Require a matching pin kind: data or exec." },
    { "pure", "boolean", false, "Require pure or impure node descriptors." },
    { "limit", "integer", false, "Page size, default 10, maximum 50." },
    { "offset", "integer", false, "Zero-based page offset." },
    { "include_pins", "boolean", false, "Include typed pins, default true." },
    { "behavior", "string", false, "A behavior asset's name: offer only the UI events of the GameUi documents on the prototypes and scene objects that attach it." },
};
static const contract_field CONTRACT_DESCRIBE_COMPONENTS[] = {
    { "names", "string[]", false, "Optional component names or stable keys; omit to describe every reflected component." },
};
static const contract_field CONTRACT_EDIT_GRAPH[] = {
    { "project_root", "string", false, "Project directory." },
    { "path", "string", false, "Graph asset path below content, or use kind/name." },
    { "kind", "string", false, "Graph asset family." },
    { "name", "string", false, "Graph asset name." },
    { "create", "boolean", false, "Create a new asset without replacing existing content." },
    { "expected_hash", "string", false, "Required current asset hash when editing." },
    { "dry_run", "boolean", false, "Validate and return results without writing." },
    { "graph_id", "uint64", false, "Stable graph ID." },
    { "graph_name", "string", false, "Unique graph name." },
    { "description", "string", false, "Top-level asset description; empty clears it." },
    { "owner_type", "string", false, "Optional owner handle type for behavior and library assets." },
    { "query", "object|null", false, "System query; null clears the filter." },
    { "graph_description", "string", false, "Description for the selected graph; empty clears it." },
    { "operations", "array", true, "Atomic semantic edit batch, at most 128 operations." },
};
static const contract_field CONTRACT_EDIT_PROTOTYPE[] = {
    { "project_root", "string", false, "Project directory." },
    { "path", "string", false, "Prototype asset path below content, or use kind/name." },
    { "kind", "string", false, "Prototype asset family." },
    { "name", "string", false, "Prototype asset name." },
    { "create", "boolean", false, "Create a new asset without replacing existing content." },
    { "expected_hash", "string", false, "Required current asset hash when editing." },
    { "dry_run", "boolean", false, "Validate and return results without writing." },
    { "operations", "array", true, "Atomic semantic component, field, and behavior edit batch, at most 128 operations." },
};
static const contract_field CONTRACT_EDIT_SCENE[] = {
    { "project_root", "string", false, "Project directory." },
    { "path", "string", false, "Scene asset path below content, or use kind/name." },
    { "kind", "string", false, "Scene asset family." },
    { "name", "string", false, "Scene asset name." },
    { "create", "boolean", false, "Create a new scene without replacing existing content." },
    { "expected_hash", "string", false, "Required current asset hash when editing." },
    { "dry_run", "boolean", false, "Validate and serialize the proposed edit without publishing it." },
    { "operations", "array", true, "Atomic semantic scene edit batch, at most 128 operations." },
};
static const contract_field CONTRACT_PROFILE_SCENARIO[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "kind", "string", false, "Target family; defaults to prototype." },
    { "name", "string", true, "Prototype or scene asset name." },
    { "frames", "uint64", false, "Number of deterministic fixed steps; maximum 1800." },
    { "fixed_dt", "number", false, "Fixed timestep in seconds; currently limited to 1/60." },
    { "warmup_frames", "uint64", false, "Frames to run before collecting samples; maximum 1800." },
};
static const contract_field CONTRACT_EXECUTION_STATUS[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "kind", "string", true, "Behavior or system." },
    { "name", "string", true, "Asset name." },
};
static const contract_field CONTRACT_OPTIMIZE_GRAPH[] = {
    { "project_root", "string", false, "Project directory." },
    { "path", "string", false, "Graph asset path below content, or use kind/name." },
    { "kind", "string", false, "Graph asset family." },
    { "name", "string", false, "Graph asset name." },
    { "expected_hash", "string", false, "Required current asset hash when applying an optimization." },
    { "dry_run", "boolean", false, "Report safe optimizations without publishing them; defaults to true." },
    { "graph_id", "uint64", false, "Stable graph ID." },
    { "graph_name", "string", false, "Unique graph name." },
    { "fanout_threshold", "integer", false, "Minimum compatible fanout for local alias creation; 2 through 64." },
    { "preserve_debug", "boolean", false, "Keep watched, disabled, and breakpoint graph state intact; defaults to true." },
    { "local_aliases", "boolean", false, "Create local aliases for safe high-fanout values; defaults to true." },
    { "math_expressions",
      "boolean",
      false,
      "First replace math node trees with expressions; defaults to false." },
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
static const contract_field CONTRACT_PREVIEW_SHADER[] = {
    { "project_root", "string", false, "Project directory." },
    { "path", "string", false, "Shader asset path, or use kind/name." },
    { "kind", "string", false, "Material or shape; preview shader libraries through a consumer." },
    { "name", "string", false, "Shader asset name." },
    { "width", "integer", false, "Preview width, default 256, maximum 512." },
    { "height", "integer", false, "Preview height, default 256, maximum 512." },
    { "time", "number", false, "Deterministic preview simulation time." },
    { "beat_phase", "number", false, "Material Beat Phase, 0 to 1; default 0." },
    { "beat_pulse", "number", false, "Material Beat Pulse, 0 to 1; default 0." },
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
static const contract_field CONTRACT_RENAME_SYMBOL[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "asset", "object", false, "Asset {kind, name}; with graph or graph_id, its function or macro; with variable, parameter or local, that member." },
    { "graph", "string", false, "Function or macro name in asset: the selected graph, a parameter's function, or where a local is." },
    { "graph_id", "uint64", false, "Function or macro ID in asset, as inspectGraph lists it." },
    { "variable", "string", false, "Variable name in asset." },
    { "parameter", "string", false, "Input or output name of the function graph names." },
    { "local", "string", false, "Named value (local) in asset." },
    { "event", "string", false, "Event name; its definition, when it has one, goes with it." },
    { "component", "string", false, "Component name or key; refused, since C reflection names it." },
    { "field", "string", false, "Field path of component; refused." },
    { "dry_run", "boolean", false, "List every asset and change without publishing; defaults to true." },
    { "expected_hashes", "object", false, "The dry run's path-to-hash map; required to publish." },
    { "new_name", "string", true, "The symbol's new name." },
};
static const contract_field CONTRACT_DELETE_SYMBOL[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "asset", "object", false, "Asset {kind, name}; with graph or graph_id, its function or macro; with variable, parameter or local, that member." },
    { "graph", "string", false, "Function or macro name in asset: the selected graph, a parameter's function, or where a local is." },
    { "graph_id", "uint64", false, "Function or macro ID in asset, as inspectGraph lists it." },
    { "variable", "string", false, "Variable name in asset." },
    { "parameter", "string", false, "Input or output name of the function graph names." },
    { "local", "string", false, "Named value (local) in asset." },
    { "event", "string", false, "Event name; its definition, when it has one, goes with it." },
    { "component", "string", false, "Component name or key; refused, since C reflection names it." },
    { "field", "string", false, "Field path of component; refused." },
    { "dry_run", "boolean", false, "List every asset and change without publishing; defaults to true." },
    { "expected_hashes", "object", false, "The dry run's path-to-hash map; required to publish." },
    { "force", "boolean", false, "Delete while references remain, and list them as dangling." },
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
    { "edit_graph",
      "Create or edit a graph asset with one atomic semantic batch; validate "
      "before saving.",
      CONTRACT_EDIT_GRAPH,
      CONTRACT_FIELD_COUNT(CONTRACT_EDIT_GRAPH),
      1,
      false,
      false,
      true },
    { "edit_prototype",
      "Create or edit a prototype with semantic component, field, and behavior "
      "operations; validate "
      "before saving.",
      CONTRACT_EDIT_PROTOTYPE,
      CONTRACT_FIELD_COUNT(CONTRACT_EDIT_PROTOTYPE),
      1,
      false,
      false,
      true },
    { "edit_scene",
      "Create or edit a scene with semantic entity composition, overrides, and "
      "behavior operations; validate before saving.",
      CONTRACT_EDIT_SCENE,
      CONTRACT_FIELD_COUNT(CONTRACT_EDIT_SCENE),
      1,
      false,
      false,
      true },
    { "optimize_graph",
      "Apply safe graph folds, local aliases, and cleanup passes and report "
      "skipped opportunities.",
      CONTRACT_OPTIMIZE_GRAPH,
      CONTRACT_FIELD_COUNT(CONTRACT_OPTIMIZE_GRAPH),
      1,
      false,
      false,
      true },
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
        "List authored JSON assets and their stable content identities.",
        CONTRACT_LIST_ASSETS,
        CONTRACT_FIELD_COUNT(CONTRACT_LIST_ASSETS),
        1,
        true,
        true,
        false,
    },
    {
        "read_asset",
        "Read one authored asset as normalized JSON data.",
        CONTRACT_ASSET_REFERENCE,
        CONTRACT_FIELD_COUNT(CONTRACT_ASSET_REFERENCE),
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
        true,
        true,
        false,
    },
    {
        "runtime_snapshot",
        "Launch one prototype or scene and return a bounded reflected runtime "
        "snapshot.",
        CONTRACT_RUNTIME_SNAPSHOT,
        CONTRACT_FIELD_COUNT(CONTRACT_RUNTIME_SNAPSHOT),
        1,
        true,
        true,
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
        "rename_symbol",
        "Rename a symbol and every reference to it across assets, as one "
        "hash-checked change.",
        CONTRACT_RENAME_SYMBOL,
        CONTRACT_FIELD_COUNT(CONTRACT_RENAME_SYMBOL),
        1,
        false,
        false,
        true,
    },
    {
        "delete_symbol",
        "Delete an asset, event definition, function or variable that nothing "
        "references, as one hash-checked change.",
        CONTRACT_DELETE_SYMBOL,
        CONTRACT_FIELD_COUNT(CONTRACT_DELETE_SYMBOL),
        1,
        false,
        false,
        true,
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
static int handle_edit_prototype(const char *root, yyjson_val *request, response *result, int *exit_code);
static int handle_edit_scene(const char *root, yyjson_val *request, response *result, int *exit_code);
static int handle_optimize_graph(const char *root, yyjson_val *request, response *result, int *exit_code);
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

static const ecs_component_desc *automation_query_component(const char *name, void *)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if ((component->name && !strcmp(component->name, name)) ||
            (component->key && !strcmp(component->key, name))) {
            return component;
        }
    }
    return NULL;
}

static const char *automation_query_component_name(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component->id == id) {
            return component->key ? component->key : component->name;
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
    return length > extension &&
           !strcmp(filename + length - extension, family->extension);
}

/* Sets `ref`'s file name to `name`, with its family's extension unless it
 * has it; NULL, or why the name is refused. */
static const char *name_asset_file(asset_ref *ref, const char *name)
{
    const char *extension = ref->family->extension;
    if (strstr(name, extension)) {
        if (!valid_asset_filename(ref->family, name) ||
            strlen(name) >= sizeof ref->filename) {
            return "asset name is invalid";
        }
        snprintf(ref->filename, sizeof ref->filename, "%s", name);
        return NULL;
    }
    if (strlen(name) + strlen(extension) >= sizeof ref->filename) {
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
    return json ? json : xstrdup("{\"contract\":1,\"ok\":false}");
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

static char *asset_full_path(const char *root, const asset_ref *ref)
{
    size_t size = strlen(root) + strlen(ref->relative) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", root, ref->relative);
    return path;
}

/* Asset paths are rooted at a real project directory. Do not follow a
 * symlink at either content level: a local MCP client must not be able to
 * turn a project-relative asset path into an arbitrary filesystem read. */
static int verify_asset_directory(const char *root, const asset_family *family, char *error, size_t error_size)
{
    size_t content_size = strlen(root) + strlen("/content") + 1;
    char *content_path = xmalloc(content_size);
    snprintf(content_path, content_size, "%s/content", root);
    struct stat info;
    if (lstat(content_path, &info) != 0) {
        int missing = errno == ENOENT;
        free(content_path);
        if (missing) {
            return 0;
        }
        return set_errorf(error, error_size, "could not inspect %s", "content"), -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        free(content_path);
        return set_error(error, error_size, "content is not a directory"), -1;
    }
    free(content_path);

    size_t directory_size = strlen(root) + strlen("/content/") +
                            strlen(family->folder) + 1;
    char *directory_path = xmalloc(directory_size);
    snprintf(directory_path, directory_size, "%s/content/%s", root, family->folder);
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
    yyjson_val *entities = yyjson_obj_get(root, "entities");
    yyjson_val *entity;
    yyjson_arr_foreach(entities, index, count, entity)
    {
        if (yyjson_is_obj(entity)) {
            catalog_member_add(asset, "entity", request_string(entity, "name"));
        }
    }
    qsort(asset->members, (size_t)acount(asset->members), sizeof *asset->members, catalog_member_compare);
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
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    component_presentation presentation = { 0 };
    bool ok = component_presentation_read(json, &registry, &presentation, error, error_size);
    component_presentation_free(&presentation);
    return ok;
}

/* The ui folder of the project at `root`. */
static void ui_folder(const char *root, char *out, size_t size)
{
    snprintf(out, size, "%s/content/ui", root);
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

/* UI assets hold RML markup, and RCSS beside it, that GameUi components
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

/* UI language service (workstream B): validateAsset for a ui asset whose
 * markup is `rml`: its syntax, widgets, imports and RmlUi's warnings.
 * Returns the error count. */
static int validate_ui(
    const char *root,
    const asset_ref *ref,
    const char *rml,
    yyjson_mut_doc *doc,
    yyjson_mut_val *diagnostics
)
{
    char *name = asset_name_from_filename(ref->filename);
    char content[1024];
    snprintf(content, sizeof content, "%s/content", root);
    char *rcss = ui_styles(root, name);
    int errors = automation_ui_validate(content, name, rml, rcss, doc, diagnostics);
    free(rcss);
    free(name);
    return errors;
}

/* What graph assets link with: the libraries, shader libraries and event
   definitions, loaded once per catalog. `error` is why they did not load. */
typedef struct
{
    behavior_asset **assets;
    char error[256];
} catalog_links;

static bool links_family(const asset_family *family)
{
    return !strcmp(family->kind, "library") ||
           !strcmp(family->kind, "shader-library") || !strcmp(family->kind, "event");
}

static void load_catalog_links(const char *root, const asset_entry *entries, catalog_links *links)
{
    for (int i = 0; i < acount(entries) && !links->error[0]; i++) {
        if (!links_family(entries[i].family)) {
            continue;
        }
        size_t path_size = strlen(root) + strlen(entries[i].relative) + 2;
        char *path = xmalloc(path_size);
        snprintf(path, path_size, "%s/%s", root, entries[i].relative);
        char *json = read_file(path, NULL, NULL, links->error, sizeof links->error);
        free(path);
        behavior_asset *asset = json ? behavior_asset_load_json(json) : NULL;
        if (json && !asset) {
            snprintf(
                links->error,
                sizeof links->error,
                "could not load %s: %s",
                entries[i].relative,
                graph_last_error()
            );
        }
        free(json);
        if (asset) {
            apush(links->assets, asset);
        }
    }
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

static void catalog_validate_asset(const catalog_links *links, catalog_asset *asset, const char *json)
{
    const asset_family *family = asset->asset.family;
    if (family->loader == ASSET_GRAPH) {
        behavior_asset *behavior = behavior_asset_load_json(json);
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
            catalog_add_report(asset, &report);
            compile_report_free(&report);
        }
        behavior_asset_free(behavior);
    } else if (family->loader == ASSET_SYSTEM) {
        char error[256] = { 0 };
        game_system_document *document = game_system_document_load(
            json,
            automation_query_component,
            NULL,
            error,
            sizeof error
        );
        if (!document) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        } else {
            compile_report report;
            compile_report_init(&report);
            behavior_asset_compile_for_system(game_system_document_behavior_asset(document), &report);
            catalog_add_report(asset, &report);
            compile_report_free(&report);
            game_system_document_free(document);
        }
    } else if (family->loader == ASSET_PROTOTYPE) {
        game_prototype_registry registry = {
            .components = GAME_REGISTRY_COMPONENTS,
            .component_count = GAME_REGISTRY_COMPONENT_COUNT,
        };
        char error[256] = { 0 };
        game_prototype *prototype = game_prototype_load(json, &registry, error, sizeof error);
        if (!prototype) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        } else {
            game_prototype_free(prototype);
        }
    } else if (family->loader == ASSET_EDITOR) {
        char error[256] = { 0 };
        if (!read_editor_asset(json, error, sizeof error)) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        }
    } else {
        char error[256] = { 0 };
        game_scene_document *scene = game_scene_document_load(json, error, sizeof error);
        if (!scene) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        } else {
            game_scene_document_free(scene);
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

/* The project's default page, the ui asset project.yaml's ui: names, owned;
 * NULL for none. */
static char *manifest_page(const char *root)
{
    char path[1024], error[128];
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    char *text = text_file_read(path);
    yyjson_doc *manifest = text ? asset_text_read(text, error, sizeof error) : NULL;
    free(text);
    const char *ui = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(manifest), "ui"));
    char *page = ui && ui[0] ? xstrdup(ui) : NULL;
    yyjson_doc_free(manifest);
    return page;
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
    char error[256];
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    switch (asset->asset.family->loader) {
        case ASSET_GRAPH: {
            behavior_asset *graph = behavior_asset_load_json(text);
            if (graph) {
                reference_index_add_graphs(index, asset->reference, graph);
            }
            behavior_asset_free(graph);
            return;
        }
        case ASSET_SYSTEM: {
            game_system_document *system = game_system_document_load(
                text,
                automation_query_component,
                NULL,
                error,
                sizeof error
            );
            if (system) {
                reference_index_add_graphs(index, asset->reference, game_system_document_behavior_asset(system));
                game_system_document_free(system);
            }
            return;
        }
        case ASSET_PROTOTYPE: {
            game_prototype *prototype = game_prototype_load(text, &registry, error, sizeof error);
            if (prototype) {
                reference_index_add_prototype(index, asset->reference, prototype);
                game_prototype_free(prototype);
            }
            return;
        }
        case ASSET_SCENE: {
            game_scene_document *scene = game_scene_document_load(text, error, sizeof error);
            for (int i = 0; scene && i < acount(scene->entities); i++) {
                const game_scene_entity *entity = &scene->entities[i];
                reference_object object = {
                    .name = entity->name,
                    .prototype = entity->prototype,
                    .overrides = entity->components_json,
                    .behaviors = (const char *const *)entity->behaviors,
                    .behavior_count = acount(entity->behaviors),
                };
                reference_index_add_object(index, asset->reference, &object);
            }
            if (scene) {
                reference_index_add_page(index, asset->reference, scene->ui, page);
            }
            game_scene_document_free(scene);
            return;
        }
        case ASSET_UI:
            reference_index_add_ui(index, asset->reference, text);
            return;
        case ASSET_EDITOR: return;
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
            used ? used->asset.family->kind : family_by_reference(named->kind)->kind,
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
        qsort(dependencies, (size_t)acount(dependencies), sizeof *dependencies, catalog_reference_compare);
        qsort(dependents, (size_t)acount(dependents), sizeof *dependents, catalog_reference_compare);
    }
}

static char *catalog_path(const char *root, const catalog_asset *asset)
{
    size_t size = strlen(root) + strlen(asset->asset.relative) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", root, asset->asset.relative);
    return path;
}

/* Every asset first, then what each holds, so references find the event
 * definitions they name. */
static int catalog_index_references(
    const char *root,
    catalog_asset *assets,
    reference_index **out,
    char *error,
    size_t error_size
)
{
    reference_index *index = reference_index_new();
    for (int i = 0; i < acount(assets); i++) {
        reference_asset_kind kind = assets[i].asset.family->reference;
        char *path = catalog_path(root, &assets[i]);
        assets[i]
            .reference = kind == REFERENCE_ASSET_KIND_COUNT
                           ? -1
                           : reference_index_add_asset(index, kind, assets[i].name, path, NULL);
        free(path);
    }
    char *page = manifest_page(root);
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].reference < 0) {
            continue;
        }
        char *path = catalog_path(root, &assets[i]);
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
    catalog_link_references(assets, index);
    *out = index;
    return 0;
}

static int enumerate_assets(const char *root, asset_entry **out, char *error, size_t error_size);

/* Parses asset file YAML or in-memory JSON where callers report their own
 * error. */
static yyjson_doc *parse_asset_text(const char *text)
{
    char error[160];
    return asset_text_read(text, error, sizeof error);
}

/* A ui asset's markup and styles, which are not YAML, load as a GameUi
 * shows them. */
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
            .bytes = entry->bytes,
            .hash = entry->hash,
        },
        .name = asset_name_from_filename(entry->filename),
        .valid = true,
    };
    size_t path_size = strlen(root) + strlen(entry->relative) + 2;
    char *path = xmalloc(path_size);
    snprintf(path, path_size, "%s/%s", root, entry->relative);
    char read_error[256] = { 0 };
    char *json = read_file(path, NULL, NULL, read_error, sizeof read_error);
    free(path);
    if (!json) {
        free_catalog_assets(asset);
        return set_error(error, error_size, read_error);
    }
    if (entry->family->loader == ASSET_UI) {
        catalog_validate_ui(root, asset);
        free(json);
        return 0;
    }
    yyjson_doc *document = parse_asset_text(json);
    if (!document) {
        asset->valid = false;
        asset->errors = 1;
        catalog_add_diagnostic(asset, "error", 0, "JSON", "asset is not valid JSON");
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
        catalog_validate_asset(links, asset, json);
    }
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
    catalog_asset *assets = NULL;
    bool graphs_cached = previous && catalog_links_unchanged(previous, entries);
    catalog_links links = { 0 };
    bool linked = false;
    int status = 0;
    for (int i = 0; i < acount(entries) && !status; i++) {
        bool graph = entries[i].family->loader == ASSET_GRAPH;
        int previous_index = previous && (!graph || graphs_cached)
                               ? cached_catalog_asset_index(previous, &entries[i])
                               : -1;
        if (previous_index >= 0) {
            catalog_asset asset = previous[previous_index];
            previous[previous_index] = (catalog_asset){ 0 };
            reset_catalog_derived_data(&asset);
            apush(assets, asset);
            continue;
        }
        if (graph && !linked) {
            load_catalog_links(root, entries, &links);
            linked = true;
        }
        catalog_asset asset = { 0 };
        status = load_catalog_asset(root, &links, &entries[i], &asset, error, error_size);
        if (!status) {
            apush(assets, asset);
        }
    }
    free_behavior_assets(links.assets);
    if (status) {
        free_catalog_assets(assets);
        return -1;
    }
    if (catalog_index_references(root, assets, references, error, error_size) != 0) {
        free_catalog_assets(assets);
        return -1;
    }
    *out = assets;
    return 0;
}

static int enumerate_assets(const char *root, asset_entry **out, char *error, size_t error_size)
{
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
        size_t directory_size = strlen(root) + strlen("/content/") +
                                strlen(family->folder) + 1;
        char *directory_path = xmalloc(directory_size);
        snprintf(directory_path, directory_size, "%s/content/%s", root, family->folder);
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
            free(contents);
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
                .bytes = bytes,
                .hash = hash,
            };
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
    qsort(entries, (size_t)acount(entries), sizeof *entries, asset_entry_compare);
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
        return -1;
    }
    if (CATALOG_SNAPSHOT.assets && !strcmp(CATALOG_SNAPSHOT.root, root) &&
        asset_entries_equal(entries, CATALOG_SNAPSHOT.entries)) {
        free_asset_entries(entries);
        *out = CATALOG_SNAPSHOT.assets;
        *revision = CATALOG_SNAPSHOT.revision;
        *content_hash = CATALOG_SNAPSHOT.content_hash;
        return 0;
    }
    catalog_asset *assets = NULL;
    reference_index *references = NULL;
    if (build_catalog_from_entries(root, entries, CATALOG_SNAPSHOT.assets, &assets, &references, error, error_size) !=
        0) {
        free_asset_entries(entries);
        free_catalog_snapshot(&CATALOG_SNAPSHOT);
        return -1;
    }
    uint64_t next_revision = CATALOG_SNAPSHOT.revision + 1;
    free_catalog_snapshot(&CATALOG_SNAPSHOT);
    CATALOG_SNAPSHOT.root = xstrdup(root);
    CATALOG_SNAPSHOT.entries = entries;
    CATALOG_SNAPSHOT.assets = assets;
    CATALOG_SNAPSHOT.references = references;
    CATALOG_SNAPSHOT.revision = next_revision;
    CATALOG_SNAPSHOT.content_hash = catalog_content_hash(assets);
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
    char *name = asset_name_from_filename(entry->filename);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", entry->family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", entry->relative);
    yyjson_mut_obj_add_uint(doc, object, "bytes", entry->bytes);
    add_hash(doc, object, entry->hash);
    free(name);
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
    yyjson_mut_obj_add_int(doc, object, "packs", REGISTRY_PACK_COUNT);
    yyjson_mut_obj_add_int(doc, object, "nodes", registry_node_count());
    yyjson_mut_obj_add_int(doc, object, "types", registry_type_count());
    yyjson_mut_obj_add_int(doc, object, "services", registry_service_count());
    yyjson_mut_obj_add_int(doc, object, "components", GAME_REGISTRY_COMPONENT_COUNT);
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
    char *name = asset_name_from_filename(ref->filename);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", ref->family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", ref->relative);
    yyjson_mut_obj_add_uint(doc, object, "bytes", bytes);
    add_hash(doc, object, hash);
    free(name);
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

static void add_catalog_diagnostics(yyjson_mut_doc *doc, yyjson_mut_val *object, const catalog_asset *asset)
{
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(doc, object, "diagnostics");
    for (int i = 0; i < acount(asset->diagnostics); i++) {
        const catalog_diagnostic *diagnostic = &asset->diagnostics[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, diagnostics);
        yyjson_mut_obj_add_strcpy(doc, item, "severity", diagnostic->severity);
        yyjson_mut_obj_add_int(doc, item, "rule", diagnostic->rule);
        yyjson_mut_obj_add_strcpy(doc, item, "rule_name", diagnostic->rule_name);
        yyjson_mut_obj_add_strcpy(doc, item, "message", diagnostic->message);
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
    add_catalog_diagnostics(doc, object, asset);
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

static bool text_contains_casefold(const char *text, const char *query)
{
    if (!query || !query[0]) {
        return true;
    }
    if (!text) {
        return false;
    }
    size_t query_length = strlen(query);
    size_t text_length = strlen(text);
    if (query_length > text_length) {
        return false;
    }
    for (size_t i = 0; i + query_length <= text_length; i++) {
        size_t j = 0;
        while (j < query_length && tolower((unsigned char)text[i + j]) ==
                                       tolower((unsigned char)query[j])) {
            j++;
        }
        if (j == query_length) {
            return true;
        }
    }
    return false;
}

static bool catalog_search_match(yyjson_val *request, const catalog_asset *asset)
{
    const char *query = request_string(request, "query");
    if (query && query[0]) {
        bool match = text_contains_casefold(asset->asset.family->kind, query) ||
                     text_contains_casefold(asset->name, query) ||
                     text_contains_casefold(asset->asset.relative, query) ||
                     text_contains_casefold(asset->description, query);
        for (int i = 0; !match && i < acount(asset->members); i++) {
            match = text_contains_casefold(asset->members[i].name, query);
        }
        if (!match) {
            return false;
        }
    }
    yyjson_val *kinds = yyjson_obj_get(request, "kinds");
    if (!kinds) {
        return true;
    }
    size_t index, count;
    yyjson_val *kind;
    yyjson_arr_foreach(kinds, index, count, kind)
    {
        if (yyjson_is_str(kind) &&
            family_by_kind(yyjson_get_str(kind)) == asset->asset.family) {
            return true;
        }
    }
    return false;
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
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error) !=
        0) {
        response_error(result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
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
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error) !=
        0) {
        response_error(result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
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
    int total = 0;
    for (int i = 0; i < acount(assets); i++) {
        total += catalog_search_match(request, &assets[i]);
    }
    uint64_t start = page.cursor < (uint64_t)total ? page.cursor : (uint64_t)total;
    uint64_t count = (uint64_t)total - start;
    if (page.has_limit && page.limit < count) {
        count = page.limit;
    }
    add_page_metadata(result->doc, value, total, start, count);
    yyjson_mut_val *matches = yyjson_mut_obj_add_arr(result->doc, value, "assets");
    uint64_t match_index = 0;
    for (int i = 0; i < acount(assets); i++) {
        if (catalog_search_match(request, &assets[i])) {
            if (match_index >= start && match_index < start + count) {
                add_catalog_asset(result->doc, matches, &assets[i], assets, acount(assets), page.summary);
            }
            match_index++;
        }
    }
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
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error) !=
        0) {
        response_error(result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
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
                         : (navigation_asset){ family_by_reference(asset->kind)->kind,
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
        int status = automation_ui_references(root, request, result->doc, found, failure, sizeof failure);
        if (status != CHIBI_AUTOMATION_OK) {
            response_error(result, exit_code, status, failure);
        }
        return status == CHIBI_AUTOMATION_OK ? 0 : -1;
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0, content_hash = 0;
    char error[256] = { 0 };
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash, error, sizeof error) !=
        0) {
        response_error(result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
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
        qsort(items, (size_t)item_count, sizeof *items, inspect_item_compare);
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
    qsort(nodes, (size_t)count, sizeof *nodes, inspect_node_compare);
    for (int i = 0; i < count; i++) {
        const node *call = nodes[i];
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
            if (target_graph && target_graph->name) {
                yyjson_mut_obj_add_strcpy(doc, target, "name", target_graph->name);
            }
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
    qsort(ordered_reads, (size_t)read_count, sizeof *ordered_reads, compare_uint64);
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
    qsort(ordered_writes, (size_t)write_count, sizeof *ordered_writes, compare_uint64);
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
    }
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
    char *serialized = graph_save_json(source);
    if (include_layout && serialized) {
        /* Asset files omit automatic positions; report the arranged ones. */
        graph *arranged = graph_load_json(serialized);
        free(serialized);
        serialized = NULL;
        if (arranged) {
            graph_place_unplaced(arranged);
            serialized = graph_save_json(arranged);
            graph_free(arranged);
            free(arranged);
        }
    }
    yyjson_doc *document = serialized
                             ? yyjson_read(serialized, strlen(serialized), 0)
                             : NULL;
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, object, "id", source->graph_id);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", graph_kind_name(source->kind));
    yyjson_mut_obj_add_strcpy(doc, object, "name", source->name ? source->name : "");
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
        yyjson_mut_obj_add_strcpy(doc, object, "math_expression", source->math_expression);
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
    inspect_add_sorted_array(doc, object, "edges", yyjson_obj_get(serialized_root, "edges"), true, include_layout);
    inspect_add_calls(doc, object, asset, source, report);
    if (document) {
        yyjson_doc_free(document);
    }
    free(serialized);
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
    qsort(items, (size_t)item_count, sizeof *items, inspect_item_compare);
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
    yyjson_doc *source_document = parse_asset_text(json);
    behavior_asset *asset = behavior_asset_load_json(json);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_obj_add_bool(result->doc, value, "layout_included", include_layout);
    if (!source_document || !asset) {
        yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
        add_load_diagnostic(result->doc, diagnostics, source_document ? graph_last_error() : "asset is not valid JSON");
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
    behavior_asset_compile(linked ? linked : asset, &report);
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
        yyjson_mut_obj_add_strcpy(
            result->doc,
            summary,
            "name",
            source->name ? source->name : ""
        );
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

static void inspect_add_system_graphs(yyjson_mut_doc *doc, yyjson_mut_val *object, const behavior_asset *asset)
{
    yyjson_mut_val *graphs = yyjson_mut_obj_add_arr(doc, object, "graphs");
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *graph_value = asset->graphs[i];
        yyjson_mut_val *summary = yyjson_mut_arr_add_obj(doc, graphs);
        yyjson_mut_obj_add_uint(doc, summary, "id", graph_value->graph_id);
        yyjson_mut_obj_add_strcpy(
            doc,
            summary,
            "kind",
            graph_kind_name(graph_value->kind)
        );
        yyjson_mut_obj_add_strcpy(
            doc,
            summary,
            "name",
            graph_value->name ? graph_value->name : ""
        );
        yyjson_mut_obj_add_int(doc, summary, "nodes", acount(graph_value->nodes));
        yyjson_mut_obj_add_int(doc, summary, "edges", acount(graph_value->edges));
        yyjson_mut_obj_add_bool(doc, summary, "dataflow", graph_value->dataflow != 0);
        yyjson_mut_obj_add_uint(doc, summary, "topology_revision", graph_value->topology_revision);
    }
}

static int handle_inspect_system(const char *root, yyjson_val *request, response *result, int *exit_code)
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
    if (ref.family->loader != ASSET_SYSTEM) {
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "asset is not a system");
        return -1;
    }

    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    game_system_document *document = game_system_document_load(
        json,
        automation_query_component,
        NULL,
        error,
        sizeof error
    );
    if (!document) {
        yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
        add_load_diagnostic(result->doc, diagnostics, error);
        yyjson_mut_obj_add_bool(result->doc, value, "valid", false);
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "system inspection failed");
        return -1;
    }

    char *query_json = game_query_save(
        game_system_document_query(document),
        automation_query_component,
        automation_query_component_name,
        NULL
    );
    if (query_json) {
        yyjson_doc *query_document = yyjson_read(query_json, strlen(query_json), 0);
        if (query_document) {
            yyjson_mut_obj_add_val(
                result->doc,
                value,
                "query",
                yyjson_val_mut_copy(result->doc, yyjson_doc_get_root(query_document))
            );
            yyjson_doc_free(query_document);
        } else {
            yyjson_mut_obj_add_null(result->doc, value, "query");
        }
        free(query_json);
    } else {
        yyjson_mut_obj_add_null(result->doc, value, "query");
    }

    const behavior_asset *asset = game_system_document_behavior_asset(document);
    inspect_add_system_graphs(result->doc, value, asset);
    compile_report report;
    compile_report_init(&report);
    behavior_asset_compile_for_system(asset, &report);
    inspect_add_validation(result->doc, value, asset, &report);
    int errors = report.error_count;
    compile_report_free(&report);
    game_system_document_free(document);
    free(json);
    if (errors) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "system validation failed");
        return -1;
    }
    return 0;
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

static bool system_has_tick(const behavior_asset *asset)
{
    if (!asset) {
        return false;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *graph_value = asset->graphs[i];
        for (int j = 0; j < acount(graph_value->nodes); j++) {
            const node *node_value = graph_value->nodes[j];
            if (node_value->desc && node_value->desc->flow_kind == NODE_FLOW_EVENT &&
                graph_host_event_is(node_value->reference_name, GRAPH_HOST_EVENT_TICK)) {
                return true;
            }
        }
    }
    return false;
}

static int add_system_execution_status(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    game_system *system,
    const behavior_asset *asset,
    bool *ready
)
{
    const bool jit_available = graph_sljit_available() != 0;
    const bool has_tick = system_has_tick(asset);
    const compile_report *report = game_system_compile(system);
    const bool valid = report && !compile_report_blocked(report);
    const graph_program *program = game_system_program(system);
    const bool sljit = game_system_uses_sljit(system) != 0;
    const bool typed = game_system_uses_typed_bytecode(system) != 0;
    const bool bytecode = game_system_uses_bytecode(system) != 0;
    const char *backend = "unavailable";
    const char *reason = "the system has no runnable Tick execution path";
    if (sljit) {
        backend = "sljit";
        reason = "the system is executing compiled SLJIT machine code";
    } else if (typed) {
        backend = "typed_bytecode";
        reason = jit_available ? "typed bytecode is active; this graph was not "
                                 "eligible for SLJIT"
                               : "typed bytecode is active; the SLJIT backend "
                                 "is unavailable";
    } else if (bytecode) {
        backend = "bytecode";
        reason = "generic bytecode is active; typed storage or SLJIT was "
                 "unavailable";
    }
    const bool interpreter = !bytecode && has_tick && asset && !asset->graph->dataflow;
    if (interpreter) {
        backend = "interpreter";
        reason = "this system graph requires the flow interpreter";
    }
    bool runtime_ready = valid && has_tick && (bytecode || interpreter);
    flow_error runtime_error = { 0 };
    if (runtime_ready) {
        runtime_ready = game_system_tick_mode(system, 0, SCENARIO_DEFAULT_FIXED_DT, &runtime_error, GAME_SYSTEM_EXEC_AUTO) ==
                        FLOW_OK;
        if (!runtime_ready) {
            backend = "unavailable";
            reason = runtime_error.message ? runtime_error.message
                                           : "system execution failed";
        }
    }
    add_execution_status(doc, object, backend, runtime_ready, jit_available, sljit, sljit, reason);
    yyjson_mut_val *execution = yyjson_mut_obj_get(object, "execution");
    yyjson_mut_obj_add_bool(doc, execution, "has_tick", has_tick);
    yyjson_mut_obj_add_bool(doc, execution, "bytecode", bytecode);
    yyjson_mut_obj_add_bool(doc, execution, "typed_bytecode", typed);
    yyjson_mut_obj_add_int(doc, execution, "query_count", game_system_query_count(system));
    yyjson_mut_obj_add_int(doc, execution, "match_count", game_system_match_count(system));
    yyjson_mut_val *sljit_value = yyjson_mut_obj_add_obj(doc, execution, "sljit");
    yyjson_mut_obj_add_bool(doc, sljit_value, "available", jit_available);
    yyjson_mut_obj_add_bool(doc, sljit_value, "used", sljit);
    yyjson_mut_obj_add_bool(doc, sljit_value, "batch", game_system_uses_sljit_batch(system) != 0);
    yyjson_mut_obj_add_bool(doc, sljit_value, "registers", game_system_uses_sljit_registers(system) != 0);
    yyjson_mut_obj_add_bool(doc, sljit_value, "simd", game_system_uses_sljit_simd(system) != 0);
    yyjson_mut_obj_add_int(
        doc,
        sljit_value,
        "column_count",
        program && program->sljit ? graph_sljit_column_count(program->sljit) : 0
    );
    if (runtime_error.message) {
        yyjson_mut_obj_add_strcpy(doc, execution, "runtime_error", runtime_error.message);
    }
    if (ready) {
        *ready = runtime_ready;
    }
    return runtime_ready ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_EXECUTION_ERROR;
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
    char *json = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    char error[256] = { 0 };
    int status = load_asset(root, request, &ref, &json, &bytes, &hash, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    if ((strcmp(kind, "behavior") == 0 && (ref.family->loader != ASSET_GRAPH ||
                                           strcmp(ref.family->kind, "behavior"))) ||
        (strcmp(kind, "system") == 0 && (ref.family->loader != ASSET_SYSTEM ||
                                         strcmp(ref.family->kind, "system")))) {
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "execution_status kind does not match the asset");
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    if (!strcmp(kind, "behavior")) {
        behavior_asset *asset = behavior_asset_load_json(json);
        free(json);
        if (!asset) {
            yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
            add_load_diagnostic(result->doc, diagnostics, graph_last_error());
            add_execution_status(
                result->doc,
                value,
                "unavailable",
                false,
                graph_sljit_available() != 0,
                false,
                false,
                "behavior asset could not be loaded"
            );
            response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "behavior execution status failed");
            return -1;
        }
        compile_report report;
        compile_report_init(&report);
        behavior_asset_compile(asset, &report);
        inspect_add_validation(result->doc, value, asset, &report);
        int errors = report.error_count;
        compile_report_free(&report);
        add_execution_status(
            result->doc,
            value,
            errors ? "unavailable" : "interpreter",
            errors == 0,
            graph_sljit_available() != 0,
            false,
            false,
            errors ? "behavior validation failed" : "behavior event graphs execute through the flow interpreter"
        );
        behavior_asset_free(asset);
        if (errors) {
            response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "behavior validation failed");
            return -1;
        }
        return 0;
    }

    game_system_document *document = game_system_document_load(
        json,
        automation_query_component,
        NULL,
        error,
        sizeof error
    );
    if (!document) {
        free(json);
        yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
        add_load_diagnostic(result->doc, diagnostics, error);
        add_execution_status(
            result->doc,
            value,
            "unavailable",
            false,
            graph_sljit_available() != 0,
            false,
            false,
            "system document could not be loaded"
        );
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "system execution status failed");
        return -1;
    }
    const behavior_asset *document_asset = game_system_document_behavior_asset(document);
    char *definition_json = behavior_asset_save_json(document_asset);
    behavior_asset *definition = definition_json
                                   ? behavior_asset_load_json(definition_json)
                                   : NULL;
    free(definition_json);
    free(json);
    ecs_world *world = ecs_world_new();
    for (int i = 0; definition && i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        if (ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) !=
            ECS_OK) {
            set_error(error, sizeof error, "could not register system components");
            behavior_asset_free(definition);
            definition = NULL;
        }
    }
    game_system *system = definition ? game_system_new(world, definition) : NULL;
    bool query_failed = false;
    if (system && game_system_document_query(document) &&
        game_system_set_query(system, game_system_document_query(document)) != 0) {
        set_error(error, sizeof error, "could not apply system query");
        query_failed = true;
    }
    bool ready = false;
    int execution_status = system && !query_failed
                             ? add_system_execution_status(
                                   result->doc,
                                   value,
                                   system,
                                   game_system_behavior_asset(system),
                                   &ready
                               )
                             : CHIBI_AUTOMATION_EXECUTION_ERROR;
    if (!system) {
        yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
        add_load_diagnostic(result->doc, diagnostics, error[0] ? error : "system could not be created");
        add_execution_status(
            result->doc,
            value,
            "unavailable",
            false,
            graph_sljit_available() != 0,
            false,
            false,
            "system could not be created"
        );
    } else if (query_failed) {
        add_execution_status(
            result->doc,
            value,
            "unavailable",
            false,
            graph_sljit_available() != 0,
            false,
            false,
            "system query could not be applied"
        );
        yyjson_mut_val *execution = yyjson_mut_obj_get(value, "execution");
        yyjson_mut_obj_add_bool(result->doc, execution, "query_failed", true);
        inspect_add_validation(result->doc, value, NULL, game_system_compile(system));
    } else {
        inspect_add_validation(result->doc, value, NULL, game_system_compile(system));
    }
    if (system) {
        game_system_free(system);
    }
    game_system_document_free(document);
    ecs_world_free(world);
    if (execution_status != CHIBI_AUTOMATION_OK || !ready) {
        response_error(result, exit_code, CHIBI_AUTOMATION_EXECUTION_ERROR, error[0] ? error : "system execution is unavailable");
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
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_root", "content");
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
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error) !=
        0) {
        response_error(result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
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
    int ref_status = resolve_asset_ref(request, ref, error, error_size);
    if (ref_status != 0) {
        return ref_status == -2 ? CHIBI_AUTOMATION_NOT_FOUND
                                : CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    int directory_status = verify_asset_directory(root, ref->family, error, error_size);
    if (directory_status <= 0) {
        if (directory_status == 0) {
            set_errorf(error, error_size, "asset not found: %s", ref->relative);
            return CHIBI_AUTOMATION_NOT_FOUND;
        }
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    char *path = asset_full_path(root, ref);
    struct stat info;
    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        free(path);
        return set_errorf(error, error_size, "asset not found: %s", ref->relative),
               CHIBI_AUTOMATION_NOT_FOUND;
    }
    *json = read_file(path, bytes, hash, error, error_size);
    free(path);
    return *json ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_IO_ERROR;
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
)
{
    if (!family || !name || !name[0] || strchr(name, '/') ||
        strchr(name, '\\') || strstr(name, "..")) {
        return set_error(error, error_size, "asset name is invalid"),
               CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    memset(ref, 0, sizeof *ref);
    ref->family = family;
    const char *refused = name_asset_file(ref, name);
    if (refused) {
        return set_error(error, error_size, refused), CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    snprintf(ref->relative, sizeof ref->relative, "content/%s/%s", family->folder, ref->filename);
    int directory_status = verify_asset_directory(root, family, error, error_size);
    if (directory_status <= 0) {
        if (directory_status == 0) {
            return set_errorf(error, error_size, "asset not found: %s", ref->relative),
                   CHIBI_AUTOMATION_NOT_FOUND;
        }
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    char *path = asset_full_path(root, ref);
    struct stat info;
    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        free(path);
        return set_errorf(error, error_size, "asset not found: %s", ref->relative),
               CHIBI_AUTOMATION_NOT_FOUND;
    }
    *json = read_file(path, bytes, hash, error, error_size);
    free(path);
    return *json ? CHIBI_AUTOMATION_OK : CHIBI_AUTOMATION_IO_ERROR;
}

/* Replaces an edited in-memory document with its asset file text. */
static int to_disk_text(char **edited, char *error, size_t error_size)
{
    char *text = asset_text_to_disk(*edited, error, error_size);
    free(*edited);
    *edited = text;
    return text ? 0 : -1;
}

/* Owned canonical file text for one asset, produced by its native loader and
 * saver. */
static char *canonical_asset(const asset_family *family, const char *json, char *error, size_t error_size)
{
    switch (family->loader) {
        case ASSET_GRAPH: {
            behavior_asset *asset = behavior_asset_load_json(json);
            if (!asset) {
                set_error(error, error_size, graph_last_error());
                return NULL;
            }
            char *text = behavior_asset_save_json(asset);
            behavior_asset_free(asset);
            return text;
        }
        case ASSET_SYSTEM: {
            game_system_document *document = game_system_document_load(
                json,
                automation_query_component,
                NULL,
                error,
                error_size
            );
            if (!document) {
                return NULL;
            }
            char *text = game_system_document_save(
                document,
                automation_query_component,
                automation_query_component_name,
                NULL
            );
            game_system_document_free(document);
            return text;
        }
        case ASSET_PROTOTYPE: {
            game_prototype_registry registry = {
                .components = GAME_REGISTRY_COMPONENTS,
                .component_count = GAME_REGISTRY_COMPONENT_COUNT,
            };
            game_prototype *prototype = game_prototype_load(json, &registry, error, error_size);
            if (!prototype) {
                return NULL;
            }
            char *text = game_prototype_save(prototype);
            game_prototype_free(prototype);
            return text;
        }
        case ASSET_SCENE: {
            game_scene_document *scene = game_scene_document_load(json, error, error_size);
            if (!scene) {
                return NULL;
            }
            char *text = game_scene_document_save(scene);
            game_scene_document_free(scene);
            return text;
        }
        case ASSET_EDITOR:
            /* Validated as written; the shared YAML writer canonicalizes it. */
            return read_editor_asset(json, error, error_size) ? xstrdup(json) : NULL;
        case ASSET_UI: break; /* markup, kept as written */
    }
    return NULL;
}

int chibi_automation_format_assets(const char *root, bool check)
{
    asset_entry *entries = NULL;
    char error[256] = { 0 };
    if (enumerate_assets(root, &entries, error, sizeof error)) {
        fprintf(stderr, "chibi_ctl --format: %s\n", error);
        return -1;
    }
    int differing = 0;
    for (int i = 0; i < acount(entries) && differing >= 0; i++) {
        if (entries[i].family->loader == ASSET_UI) {
            continue;
        }
        size_t size = strlen(root) + strlen(entries[i].relative) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s/%s", root, entries[i].relative);
        size_t bytes = 0;
        uint64_t hash = 0;
        char *json = read_file(path, &bytes, &hash, error, sizeof error);
        char *text = json ? canonical_asset(entries[i].family, json, error, sizeof error)
                          : NULL;
        if (text && to_disk_text(&text, error, sizeof error)) {
            text = NULL;
        }
        if (!text) {
            fprintf(stderr, "chibi_ctl --format: %s: %s\n", entries[i].relative, error);
            differing = -1;
        } else if (strcmp(json, text)) {
            differing++;
            fprintf(
                stderr,
                "%s %s\n",
                check ? "not canonical:" : "formatted",
                entries[i].relative
            );
            FILE *file = check ? NULL : fopen(path, "wb");
            if (!check && (!file || fputs(text, file) < 0 || fclose(file))) {
                fprintf(
                    stderr,
                    "chibi_ctl --format: could not write %s\n",
                    entries[i].relative
                );
                differing = -1;
            }
        }
        free(text);
        free(json);
        free(path);
    }
    free_asset_entries(entries);
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
                             : parse_asset_text(json);
    if (!document) {
        free(json);
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "asset is not valid JSON");
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_obj_add_val(
        result->doc,
        value,
        "document",
        yyjson_val_mut_copy(result->doc, yyjson_doc_get_root(document))
    );
    yyjson_doc_free(document);
    free(json);
    return 0;
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
    const char *family = asset->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                                 asset->domain == BEHAVIOR_DOMAIN_SHAPE ||
                                 asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY
                           ? "shader-library"
                           : "library";
    int loaded = load_behavior_family_assets(root, family_by_kind(family), &libraries, error, error_size);
    behavior_asset **events = NULL;
    if (loaded == CHIBI_AUTOMATION_OK && !strcmp(family, "library")) {
        loaded = load_behavior_family_assets(root, family_by_kind("event"), &events, error, error_size);
        for (int i = 0; i < acount(events); i++) {
            apush(libraries, events[i]);
        }
        afree(events);
    }
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
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
    int errors = 0;
    int warnings = 0;
    int notes = 0;
    if (ref.family->loader == ASSET_GRAPH) {
        behavior_asset *asset = behavior_asset_load_json(json);
        if (!asset) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, graph_last_error());
        } else {
            compile_report report;
            compile_report_init(&report);
            behavior_asset *linked = link_project_libraries(root, asset, error, sizeof error);
            if (linked) {
                errors = behavior_asset_compile(linked, &report);
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
    } else if (ref.family->loader == ASSET_SYSTEM) {
        char load_error[256] = { 0 };
        game_system_document *document = game_system_document_load(
            json,
            automation_query_component,
            NULL,
            load_error,
            sizeof load_error
        );
        if (!document) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        } else {
            char *query_json = game_query_save(
                game_system_document_query(document),
                automation_query_component,
                automation_query_component_name,
                NULL
            );
            if (query_json) {
                yyjson_doc *query_document = yyjson_read(query_json, strlen(query_json), 0);
                if (query_document) {
                    yyjson_mut_obj_add_val(
                        result->doc,
                        value,
                        "query",
                        yyjson_val_mut_copy(result->doc, yyjson_doc_get_root(query_document))
                    );
                    yyjson_doc_free(query_document);
                }
                free(query_json);
            }
            compile_report report;
            compile_report_init(&report);
            behavior_asset_compile_for_system(game_system_document_behavior_asset(document), &report);
            errors = report.error_count;
            warnings = report.warning_count;
            for (int i = 0; i < acount(report.items); i++) {
                add_diagnostic(
                    result->doc,
                    diagnostics,
                    game_system_document_behavior_asset(document),
                    &report.items[i]
                );
            }
            compile_report_free(&report);
            game_system_document_free(document);
        }
    } else if (ref.family->loader == ASSET_PROTOTYPE) {
        game_prototype_registry registry = {
            .components = GAME_REGISTRY_COMPONENTS,
            .component_count = GAME_REGISTRY_COMPONENT_COUNT,
        };
        char load_error[256] = { 0 };
        game_prototype *prototype = game_prototype_load(json, &registry, load_error, sizeof load_error);
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
            game_prototype_free(prototype);
        }
    } else if (ref.family->loader == ASSET_UI) {
        errors = validate_ui(root, &ref, json, result->doc, diagnostics);
    } else if (ref.family->loader == ASSET_EDITOR) {
        char load_error[256] = { 0 };
        if (!read_editor_asset(json, load_error, sizeof load_error)) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        }
    } else {
        char load_error[256] = { 0 };
        game_scene_document *scene = game_scene_document_load(json, load_error, sizeof load_error);
        if (!scene) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        } else {
            yyjson_mut_obj_add_strcpy(result->doc, value, "scene_name", scene->name);
            yyjson_mut_obj_add_int(result->doc, value, "entities", acount(scene->entities));
            game_scene_document_free(scene);
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
        size_t path_size = strlen(root) + strlen(entries[i].relative) + 2;
        char *path = xmalloc(path_size);
        snprintf(path, path_size, "%s/%s", root, entries[i].relative);
        char read_error[256] = { 0 };
        char *json = read_file(path, NULL, NULL, read_error, sizeof read_error);
        free(path);
        if (!json) {
            free_asset_entries(entries);
            free_behavior_assets(assets);
            set_error(error, error_size, read_error);
            return CHIBI_AUTOMATION_IO_ERROR;
        }
        behavior_asset *asset = behavior_asset_load_json(json);
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

static int register_preview_assets(
    game_prototype_preview *preview,
    behavior_asset **assets,
    char *error,
    size_t error_size
)
{
    for (int i = 0; i < acount(assets); i++) {
        const char *name = assets[i]->name;
        if (game_prototype_preview_register_behavior(preview, name, assets[i]) != 0) {
            snprintf(error, error_size, "could not register behavior %s: %s", name, graph_last_error());
            return -1;
        }
    }
    return 0;
}

/* What every scenario shares, as the game loads it: libraries and events,
   then shader libraries and the materials and shapes behaviors write to. */
static int register_shared_assets(const char *root, game_prototype_preview *preview, char *error, size_t error_size)
{
    static const char *const FAMILIES[] = { "library",
                                            "event",
                                            "shader-library",
                                            "material",
                                            "shape" };
    int status = CHIBI_AUTOMATION_OK;
    for (int f = 0; f < (int)(sizeof FAMILIES / sizeof *FAMILIES) &&
                    status == CHIBI_AUTOMATION_OK;
         f++) {
        behavior_asset **assets = NULL;
        status = load_behavior_family_assets(root, family_by_kind(FAMILIES[f]), &assets, error, error_size);
        if (status == CHIBI_AUTOMATION_OK &&
            register_preview_assets(preview, assets, error, error_size) != 0) {
            status = CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
        free_behavior_assets(assets);
    }
    return status;
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
            const char *text = value->str ? value->str : "";
            uint64_t text_size = strlen(text);
            *hash = scenario_hash_bytes(*hash, &text_size, sizeof text_size);
            *hash = scenario_hash_bytes(*hash, text, (size_t)text_size);
            break;
        }
        case PIN_STRUCT: {
            const type_desc *struct_type = registry_type_by_name(type_name);
            if (!struct_type || !value->ptr) {
                break;
            }
            for (int i = 0; i < struct_type->field_count; i++) {
                const type_field *field = &struct_type->fields[i];
                uint64_t field_size = strlen(field->name ? field->name : "");
                *hash = scenario_hash_bytes(*hash, &field_size, sizeof field_size);
                *hash = scenario_hash_bytes(*hash, field->name, (size_t)field_size);
                pin_value field_value;
                pin_read_field(value->ptr, field, &field_value);
                scenario_hash_value(hash, field->type, field->type_name, &field_value);
            }
            break;
        }
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
                pin_value value;
                pin_read_field(data, field, &value);
                scenario_hash_value(&hash, field->type, field->type_name, &value);
            }
        }
    }
    return hash;
}

static char *scenario_prototype_json(const char *json, uint64_t hash)
{
    yyjson_doc *document = parse_asset_text(json);
    if (!document) {
        return NULL;
    }
    yyjson_val *source = yyjson_doc_get_root(document);
    if (!yyjson_is_obj(source) || yyjson_obj_get(source, "identity")) {
        char *copy = xstrdup(json);
        yyjson_doc_free(document);
        return copy;
    }
    yyjson_mut_doc *mutable = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_val_mut_copy(mutable, source);
    yyjson_mut_doc_set_root(mutable, root);
    char identity[32];
    snprintf(identity, sizeof identity, "scenario-%016llx", (unsigned long long)hash);
    yyjson_mut_obj_add_strcpy(mutable, root, "identity", identity);
    char *copy = yyjson_mut_write(mutable, 0, NULL);
    yyjson_mut_doc_free(mutable);
    yyjson_doc_free(document);
    return copy;
}

typedef struct
{
    bool scene_target;
    asset_ref prototype_ref;
    size_t prototype_bytes;
    uint64_t prototype_hash;
    game_prototype_registry registry;
    game_prototype *prototype;
    game_scene_document *scene;
    game_prototype **scene_prototypes;
    game_prototype_preview_scene_entity *scene_entities;
    game_prototype_preview *preview;
    asset_entry *runtime_assets;
    uint64_t runtime_hash;
    char *initial_saved_state;
    CF_V2 aim;
    bool aim_firing, aim_point, controls_sampled;
    uint64_t controls;
} scenario_context;

static uint64_t scenario_state_hash(scenario_context *context)
{
    uint64_t hash = scenario_world_hash(game_prototype_preview_world(context->preview));
    char *saved = game_save_export(
        game_behavior_save(game_prototype_preview_behaviors(context->preview))
    );
    hash = scenario_hash_bytes(hash, saved, strlen(saved));
    free(saved);
    engine *runtime = game_prototype_preview_runtime(context->preview);
    GameFog fog;
    const unsigned char *cells = game_visibility_cells(engine_visibility(runtime), &fog);
    if (cells) {
        hash = scenario_hash_bytes(
            hash,
            cells,
            (size_t)fog.columns * (size_t)fog.rows * 4
        );
    }
    uint64_t parameters = render_shader_overrides_hash(
        game_prototype_preview_world(context->preview)
    );
    hash = scenario_hash_bytes(hash, &parameters, sizeof parameters);
    uint64_t sounds = game_audio_hash(engine_audio(runtime));
    if (game_materials_enabled(engine_materials(runtime))) {
        game_material_state materials = game_materials_inspect(engine_materials(runtime));
        hash = scenario_hash_bytes(hash, &materials.hash, sizeof materials.hash);
    }
    return scenario_hash_bytes(hash, &sounds, sizeof sounds);
}

static void add_presentation_state(yyjson_mut_doc *doc, yyjson_mut_val *value, const scenario_context *context)
{
    engine *runtime = game_prototype_preview_runtime(context->preview);
    yyjson_mut_obj_add_uint(doc, value, "sound_cues", game_audio_count(engine_audio(runtime)));
    if (game_materials_enabled(engine_materials(runtime))) {
        game_material_state state = game_materials_inspect(engine_materials(runtime));
        yyjson_mut_val *material = yyjson_mut_obj_add_obj(doc, value, "materials");
        yyjson_mut_obj_add_int(doc, material, "particles", state.particles);
        yyjson_mut_obj_add_int(doc, material, "active_particles", state.active_particles);
        yyjson_mut_val *stream = yyjson_mut_obj_add_obj(doc, material, "streaming");
        yyjson_mut_obj_add_int(doc, stream, "resident", state.streaming.resident);
        yyjson_mut_obj_add_int(doc, stream, "stored", state.streaming.stored);
        yyjson_mut_obj_add_int(doc, stream, "loaded", state.streaming.loaded);
        yyjson_mut_obj_add_int(doc, stream, "unloaded", state.streaming.unloaded);
        yyjson_mut_obj_add_uint(doc, stream, "resident_bytes", state.streaming.resident_bytes);
        yyjson_mut_obj_add_uint(doc, stream, "stored_bytes", state.streaming.stored_bytes);
        yyjson_mut_obj_add_int(doc, material, "fuel_particles", state.fuel_particles);
        yyjson_mut_obj_add_int(doc, material, "burning_particles", state.burning_particles);
        yyjson_mut_obj_add_int(doc, material, "solids", state.solids);
        yyjson_mut_obj_add_int(doc, material, "cells", state.cells);
        yyjson_mut_obj_add_int(doc, material, "fragments", state.fragments);
        yyjson_mut_obj_add_int(doc, material, "chunks", state.chunks);
        add_hash_named(doc, material, "hash", state.hash);
    }
    GameFog fog;
    const unsigned char *cells = game_visibility_cells(engine_visibility(runtime), &fog);
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
    char *json = state ? yyjson_val_write(state, 0, NULL) : xstrdup("{}");
    game_save *save = game_behavior_save(
        game_prototype_preview_behaviors(context->preview)
    );
    bool valid = game_save_import(save, json);
    free(json);
    if (!valid) {
        set_error(error, size, game_save_error(save));
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    free(context->initial_saved_state);
    context->initial_saved_state = game_save_export(save);
    return CHIBI_AUTOMATION_OK;
}

static void add_saved_state(yyjson_mut_doc *doc, yyjson_mut_val *value, const scenario_context *context)
{
    char *json = game_save_export(
        game_behavior_save(game_prototype_preview_behaviors(context->preview))
    );
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

static void scenario_context_free(scenario_context *context)
{
    if (!context) {
        return;
    }
    free(context->initial_saved_state);
    free_asset_entries(context->runtime_assets);
    game_prototype_preview_free(context->preview);
    game_prototype_free(context->prototype);
    for (int i = 0; i < acount(context->scene_prototypes); i++) {
        game_prototype_free(context->scene_prototypes[i]);
    }
    afree(context->scene_prototypes);
    afree(context->scene_entities);
    game_scene_document_free(context->scene);
    *context = (scenario_context){ 0 };
}

static int register_preview_behavior_name(
    const char *root,
    game_prototype_preview *preview,
    const char *name,
    char *error,
    size_t error_size
)
{
    if (game_behavior_asset_registered(game_prototype_preview_behaviors(preview), name)) {
        return CHIBI_AUTOMATION_OK;
    }
    asset_ref behavior_ref = { 0 };
    char *behavior_json = NULL;
    int status = load_named_asset(
        root,
        family_by_kind("behavior"),
        name,
        &behavior_ref,
        &behavior_json,
        NULL,
        NULL,
        error,
        error_size
    );
    if (status != CHIBI_AUTOMATION_OK) {
        free(behavior_json);
        return status;
    }
    behavior_asset *behavior = behavior_asset_load_json(behavior_json);
    free(behavior_json);
    if (!behavior) {
        snprintf(error, error_size, "%s", graph_last_error());
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    int result = game_prototype_preview_register_behavior(preview, name, behavior);
    behavior_asset_free(behavior);
    if (result != 0) {
        snprintf(error, error_size, "could not register behavior %s", name);
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
}

static bool scenario_runtime_asset(const scenario_context *context, const asset_entry *entry)
{
    const char *kind = entry->family->kind;
    return !strcmp(kind, "prototype") || !strcmp(kind, "behavior") ||
           !strcmp(kind, "library") || !strcmp(kind, "event") ||
           !strcmp(kind, "field") || !strcmp(kind, "system") ||
           (context->scene_target &&
            !strcmp(entry->relative, context->prototype_ref.relative));
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
            char *name = asset_name_from_filename(entries[i].filename), *json = NULL;
            asset_ref ref;
            status = load_named_asset(root, entries[i].family, name, &ref, &json, NULL, NULL, error, error_size);
            if (status == CHIBI_AUTOMATION_OK) {
                if (!strcmp(kind, "system")) {
                    if (game_prototype_preview_register_system(context->preview, name, json, error, error_size) !=
                        0) {
                        status = CHIBI_AUTOMATION_VALIDATION_ERROR;
                    }
                } else {
                    behavior_asset *asset = behavior_asset_load_json(json);
                    if (!asset ||
                        game_prototype_preview_register_behavior(context->preview, name, asset) !=
                            0) {
                        snprintf(error, error_size, "Could not compile field %s", name);
                        status = CHIBI_AUTOMATION_VALIDATION_ERROR;
                    }
                    behavior_asset_free(asset);
                }
            }
            free(name);
            free(json);
            continue;
        }
        if (strcmp(entries[i].family->kind, "prototype")) {
            continue;
        }
        char *json = NULL;
        asset_ref ref;
        uint64_t hash;
        char *name = asset_name_from_filename(entries[i].filename);
        status = load_named_asset(root, entries[i].family, name, &ref, &json, NULL, &hash, error, error_size);
        free(name);
        if (status != CHIBI_AUTOMATION_OK) {
            free(json);
            break;
        }
        char *normalized = scenario_prototype_json(json, hash);
        free(json);
        game_prototype *prototype = normalized
                                      ? game_prototype_load(normalized, &context->registry, error, error_size)
                                      : NULL;
        free(normalized);
        if (!prototype) {
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            break;
        }
        status = game_prototype_preview_register_prototype(context->preview, prototype) == 0
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
        game_prototype_free(prototype);
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

    context->registry = (game_prototype_registry){
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    char *scenario_json = scenario_prototype_json(prototype_json, context->prototype_hash);
    context->prototype = scenario_json
                           ? game_prototype_load(scenario_json, &context->registry, error, error_size)
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

    context->preview = game_prototype_preview_new(&context->registry);
    if (!context->preview) {
        scenario_context_free(context);
        set_error(error, error_size, "could not create prototype preview");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }

    int result = register_shared_assets(root, context->preview, error, error_size);
    if (result != CHIBI_AUTOMATION_OK) {
        scenario_context_free(context);
        return result;
    }

    for (int i = 0; i < acount(context->prototype->graphs); i++) {
        asset_ref behavior_ref = { 0 };
        char *behavior_json = NULL;
        status = load_named_asset(
            root,
            family_by_kind("behavior"),
            context->prototype->graphs[i],
            &behavior_ref,
            &behavior_json,
            NULL,
            NULL,
            error,
            error_size
        );
        if (status != CHIBI_AUTOMATION_OK) {
            free(behavior_json);
            scenario_context_free(context);
            return status;
        }
        behavior_asset *behavior = behavior_asset_load_json(behavior_json);
        free(behavior_json);
        if (!behavior) {
            set_error(error, error_size, graph_last_error());
            scenario_context_free(context);
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        int registered = game_prototype_preview_register_behavior(
            context->preview,
            context->prototype->graphs[i],
            behavior
        );
        behavior_asset_free(behavior);
        if (registered != 0) {
            set_error(error, error_size, "could not register prototype behavior");
            scenario_context_free(context);
            return CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
    }
    status = register_scenario_prototypes(root, context, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(context);
    }
    return status;
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
    context->scene = game_scene_document_load(scene_json, error, error_size);
    free(scene_json);
    if (!context->scene) {
        scenario_context_free(context);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    if (!acount(context->scene->entities)) {
        scenario_context_free(context);
        set_error(error, error_size, "scene has no entities");
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    context->registry = (game_prototype_registry){
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    context->preview = game_prototype_preview_new(&context->registry);
    if (!context->preview) {
        scenario_context_free(context);
        set_error(error, error_size, "could not create scene preview");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    status = register_shared_assets(root, context->preview, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(context);
        return status;
    }
    for (int i = 0; i < acount(context->scene->entities); i++) {
        game_scene_entity *entity = &context->scene->entities[i];
        asset_ref prototype_ref = { 0 };
        char *prototype_json = NULL;
        uint64_t prototype_hash = 0;
        status = load_named_asset(
            root,
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
            scenario_context_free(context);
            return status;
        }
        char *scenario_json = scenario_prototype_json(prototype_json, prototype_hash);
        game_prototype *
            prototype = scenario_json
                          ? game_prototype_load(scenario_json, &context->registry, error, error_size)
                          : NULL;
        free(scenario_json);
        free(prototype_json);
        if (!prototype) {
            scenario_context_free(context);
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        if (game_prototype_preview_register_prototype(context->preview, prototype) != 0) {
            game_prototype_free(prototype);
            scenario_context_free(context);
            set_error(error, error_size, "could not register scene prototype");
            return CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
        apush(context->scene_prototypes, prototype);
        apush(
            context->scene_entities,
            ((game_prototype_preview_scene_entity){
                .prototype = prototype,
                .components_json = entity->components_json,
                .behaviors = (const char *const *)entity->behaviors,
                .behavior_count = acount(entity->behaviors),
            })
        );
        for (int behavior = 0; behavior < acount(prototype->graphs); behavior++) {
            status = register_preview_behavior_name(
                root,
                context->preview,
                prototype->graphs[behavior],
                error,
                error_size
            );
            if (status != CHIBI_AUTOMATION_OK) {
                scenario_context_free(context);
                return status;
            }
        }
        for (int behavior = 0; behavior < acount(entity->behaviors); behavior++) {
            status = register_preview_behavior_name(
                root,
                context->preview,
                entity->behaviors[behavior],
                error,
                error_size
            );
            if (status != CHIBI_AUTOMATION_OK) {
                scenario_context_free(context);
                return status;
            }
        }
    }
    status = register_scenario_prototypes(root, context, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        scenario_context_free(context);
    }
    return status;
}

static void add_behavior_failures(yyjson_mut_doc *doc, yyjson_mut_val *value, const scenario_context *context);

static int handle_scene_scenario(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *name = request_string(request, "name");
    if (!name || !name[0]) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "scene name is required");
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
        if (!yyjson_is_num(fixed_dt_value) ||
            !isfinite(yyjson_get_num(fixed_dt_value)) ||
            fabs(yyjson_get_num(fixed_dt_value) - SCENARIO_FIXED_DT) > 1e-12) {
            response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "fixed_dt must equal the engine fixed timestep (1/60)");
            return -1;
        }
        fixed_dt = yyjson_get_num(fixed_dt_value);
    }
    scenario_context context = { 0 };
    char error[256] = { 0 };
    const char *kind = request_string(request, "kind");
    int status = kind && !strcmp(kind, "scene")
                   ? prepare_scene_scenario(root, name, &context, error, sizeof error)
                   : prepare_scenario(root, name, &context, error, sizeof error);
    if (status == CHIBI_AUTOMATION_OK) {
        status = advance_request_scenario(&context, request, frames, fixed_dt, error, sizeof error);
    }
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
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "live_entities",
        ecs_world_live_count(game_prototype_preview_world(context.preview))
    );
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "authored_entities",
        game_prototype_preview_entity_count(context.preview)
    );
    add_scenario_timing(result->doc, value, frames, fixed_dt);
    add_saved_state(result->doc, value, &context);
    add_behavior_failures(result->doc, value, &context);
    scenario_context_free(&context);
    return 0;
}

/* Runs that stopped on an error, grouped by node and message, so a scenario
   that looks fine but aborts a Tick every frame says so. */
static void add_behavior_failures(yyjson_mut_doc *doc, yyjson_mut_val *value, const scenario_context *context)
{
    enum
    {
        FAILURES_MAX = 16
    };
    game_behavior_world *behaviors = game_prototype_preview_behaviors(context->preview);
    yyjson_mut_val *failures = yyjson_mut_obj_add_arr(doc, value, "behavior_failures");
    game_behavior_log_info seen[FAILURES_MAX];
    int counts[FAILURES_MAX] = { 0 }, kinds = 0;
    for (int i = 0; i < game_behavior_log_count(behaviors); i++) {
        game_behavior_log_info info = { 0 };
        if (game_behavior_log_info_at(behaviors, i, &info) != 0 || !info.failure) {
            continue;
        }
        int k = 0;
        while (k < kinds && (seen[k].origin != info.origin ||
                             strcmp(seen[k].message, info.message))) {
            k++;
        }
        if (k == FAILURES_MAX) {
            continue;
        }
        if (k == kinds) {
            seen[kinds++] = info;
        }
        counts[k]++;
    }
    for (int k = 0; k < kinds; k++) {
        yyjson_mut_val *failure = yyjson_mut_arr_add_obj(doc, failures);
        yyjson_mut_obj_add_strcpy(
            doc,
            failure,
            "behavior",
            seen[k].asset_name ? seen[k].asset_name : ""
        );
        if (seen[k].origin) {
            yyjson_mut_obj_add_uint(doc, failure, "node", seen[k].origin->id);
            yyjson_mut_obj_add_strcpy(
                doc,
                failure,
                "node_type",
                seen[k].origin->desc->name
            );
        }
        yyjson_mut_obj_add_strcpy(doc, failure, "message", seen[k].message);
        yyjson_mut_obj_add_int(doc, failure, "count", counts[k]);
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

static int advance_scenario(
    scenario_context *context,
    uint64_t frames,
    double fixed_dt,
    char *error,
    size_t error_size
)
{
    char launch_error[256] = { 0 };
    int launch = context->scene_target ? game_prototype_preview_launch_scene(
                                             context->preview,
                                             context->scene_entities,
                                             acount(context->scene_entities),
                                             launch_error,
                                             sizeof launch_error
                                         )
                                       : game_prototype_preview_launch(
                                             context->preview,
                                             context->prototype,
                                             launch_error,
                                             sizeof launch_error
                                         );
    if (launch != ECS_OK) {
        set_error(error, error_size, launch_error[0] ? launch_error : "could not launch prototype");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    if (game_prototype_preview_tick(context->preview, 0) != 0) {
        set_error(error, error_size, "could not initialize scenario clock");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    for (uint64_t frame = 0; frame < frames; frame++) {
        if (game_prototype_preview_tick(context->preview, (double)(frame + 1) * fixed_dt) !=
            0) {
            set_error(
                error,
                error_size,
                engine_error(game_prototype_preview_runtime(context->preview))
            );
            return CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
    }
    return CHIBI_AUTOMATION_OK;
}

#define SCENARIO_PROFILE_MAX_ROWS 1024
#define SCENARIO_PROFILE_MAX_TOTAL 1200

typedef struct
{
    const char *behavior;
    const char *event;
    uint64_t starts;
    uint64_t resumes;
    double inclusive_cpu_ms;
    double cpu_ms;
} scenario_profile_row;

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
} scenario_profile_timing;

typedef struct
{
    uint64_t calls;
    uint64_t requested_bytes;
} scenario_profile_allocations;

static void profile_add_row(
    scenario_profile_row **rows,
    const game_behavior_profile_row *source,
    bool *truncated
)
{
    if (!source || (!source->starts && !source->resumes && source->cpu_ms == 0)) {
        return;
    }
    for (int i = 0; i < acount(*rows); i++) {
        scenario_profile_row *row = &(*rows)[i];
        if (!strcmp(
                row->behavior ? row->behavior : "",
                source->behavior ? source->behavior : ""
            ) &&
            !strcmp(row->event ? row->event : "", source->event ? source->event : "")) {
            row->starts += source->starts;
            row->resumes += source->resumes;
            row->inclusive_cpu_ms += source->inclusive_cpu_ms;
            row->cpu_ms += source->cpu_ms;
            return;
        }
    }
    if (acount(*rows) >= SCENARIO_PROFILE_MAX_ROWS) {
        *truncated = true;
        return;
    }
    apush(
        *rows,
        ((scenario_profile_row){
            .behavior = source->behavior,
            .event = source->event,
            .starts = source->starts,
            .resumes = source->resumes,
            .inclusive_cpu_ms = source->inclusive_cpu_ms,
            .cpu_ms = source->cpu_ms,
        })
    );
}

static int scenario_profile_compare(const void *left, const void *right)
{
    const scenario_profile_row *a = left;
    const scenario_profile_row *b = right;
    if (a->cpu_ms != b->cpu_ms) {
        return a->cpu_ms > b->cpu_ms ? -1 : 1;
    }
    int behavior = strcmp(a->behavior ? a->behavior : "", b->behavior ? b->behavior : "");
    if (behavior) {
        return behavior;
    }
    return strcmp(a->event ? a->event : "", b->event ? b->event : "");
}

static void profile_add_timing(
    scenario_profile_timing *total,
    const engine_frame_timing *timing,
    uint64_t previous_steps,
    uint64_t previous_commands,
    double previous_dropped,
    xalloc_stats previous_allocations,
    scenario_profile_allocations *allocations
)
{
    xalloc_stats current_allocations;
    xalloc_stats_get(&current_allocations);
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

static void add_profile_timing(yyjson_mut_doc *doc, yyjson_mut_val *object, const scenario_profile_timing *timing)
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
    double total_ms = timing->behavior_ms + timing->physics_ms +
                      timing->systems_ms + timing->rendering_ms;
    yyjson_mut_obj_add_real(doc, value, "total_ms", total_ms);
    yyjson_mut_obj_add_real(
        doc,
        value,
        "average_frame_ms",
        timing->frames ? total_ms / timing->frames : 0
    );
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

static void add_profile_rows(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    scenario_profile_row *rows,
    int row_count,
    bool truncated
)
{
    if (row_count > 1) {
        qsort(rows, (size_t)row_count, sizeof *rows, scenario_profile_compare);
    }
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, "behaviors");
    for (int i = 0; i < row_count; i++) {
        const scenario_profile_row *row = &rows[i];
        yyjson_mut_val *value = yyjson_mut_arr_add_obj(doc, array);
        yyjson_mut_obj_add_strcpy(doc, value, "behavior", row->behavior ? row->behavior : "");
        yyjson_mut_obj_add_strcpy(doc, value, "event", row->event ? row->event : "");
        yyjson_mut_obj_add_uint(doc, value, "starts", row->starts);
        yyjson_mut_obj_add_uint(doc, value, "resumes", row->resumes);
        yyjson_mut_obj_add_real(doc, value, "inclusive_cpu_ms", row->inclusive_cpu_ms);
        yyjson_mut_obj_add_real(doc, value, "cpu_ms", row->cpu_ms);
        uint64_t activations = row->starts + row->resumes;
        yyjson_mut_obj_add_real(
            doc,
            value,
            "average_cpu_us",
            activations ? row->cpu_ms * 1000.0 / (double)activations : 0
        );
        yyjson_mut_obj_add_real(
            doc,
            value,
            "average_inclusive_cpu_us",
            activations ? row->inclusive_cpu_ms * 1000.0 / (double)activations : 0
        );
    }
    yyjson_mut_obj_add_bool(doc, object, "behaviors_truncated", truncated);
}

static int handle_profile_scenario(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *kind = request_string(request, "kind");
    if (!kind) {
        kind = "prototype";
    }
    if (strcmp(kind, "prototype") && strcmp(kind, "prototypes") &&
        strcmp(kind, "scene") && strcmp(kind, "scenes")) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "profile_scenario supports prototype or scene targets");
        return -1;
    }
    const char *name = request_string(request, "name");
    if (!name || !name[0]) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "profile target name is required");
        return -1;
    }
    uint64_t frames = SCENARIO_DEFAULT_FRAMES;
    uint64_t warmup = 0;
    if (yyjson_obj_get(request, "frames") &&
        (!request_uint(request, "frames", &frames) || frames > SCENARIO_MAX_FRAMES)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "frames must be a non-negative integer no greater than 1800");
        return -1;
    }
    if (yyjson_obj_get(request, "warmup_frames") &&
        (!request_uint(request, "warmup_frames", &warmup) ||
         warmup > SCENARIO_MAX_FRAMES)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "warmup_frames must be a non-negative integer no greater than 1800");
        return -1;
    }
    if (warmup + frames > SCENARIO_PROFILE_MAX_TOTAL) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "warmup_frames plus frames must not exceed 1200");
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
    scenario_context context = { 0 };
    char error[256] = { 0 };
    bool scene = !strcmp(kind, "scene") || !strcmp(kind, "scenes");
    int status = scene
                   ? prepare_scene_scenario(root, name, &context, error, sizeof error)
                   : prepare_scenario(root, name, &context, error, sizeof error);
    scenario_profile_row *rows = NULL;
    scenario_profile_timing timing = { 0 };
    scenario_profile_allocations allocations = { 0 };
    bool rows_truncated = false;
    if (status == CHIBI_AUTOMATION_OK) {
        char launch_error[256] = { 0 };
        int launch = context.scene_target ? game_prototype_preview_launch_scene(
                                                context.preview,
                                                context.scene_entities,
                                                acount(context.scene_entities),
                                                launch_error,
                                                sizeof launch_error
                                            )
                                          : game_prototype_preview_launch(
                                                context.preview,
                                                context.prototype,
                                                launch_error,
                                                sizeof launch_error
                                            );
        if (launch != ECS_OK) {
            snprintf(error, sizeof error, "%s", launch_error[0] ? launch_error : "could not launch scenario");
            status = CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
    }
    if (status == CHIBI_AUTOMATION_OK) {
        engine_set_behavior_profiling(game_prototype_preview_runtime(context.preview), true);
    }
    if (status == CHIBI_AUTOMATION_OK &&
        game_prototype_preview_tick(context.preview, 0) != 0) {
        snprintf(error, sizeof error, "could not initialize scenario clock");
        status = CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    uint64_t previous_steps = 0;
    uint64_t previous_commands = 0;
    double previous_dropped = 0;
    xalloc_stats previous_allocations = { 0 };
    if (status == CHIBI_AUTOMATION_OK) {
        const engine_frame_timing *initial = engine_last_timing(
            game_prototype_preview_runtime(context.preview)
        );
        previous_steps = initial->simulation_steps;
        previous_commands = initial->command_apply_count;
        previous_dropped = initial->dropped_seconds;
        xalloc_stats_get(&previous_allocations);
        for (uint64_t frame = 0; frame < warmup + frames; frame++) {
            if (game_prototype_preview_tick(context.preview, (double)(frame + 1) * fixed_dt) !=
                0) {
                snprintf(error, sizeof error, "scenario profiling failed");
                status = CHIBI_AUTOMATION_EXECUTION_ERROR;
                break;
            }
            if (frame < warmup) {
                const engine_frame_timing *sample = engine_last_timing(
                    game_prototype_preview_runtime(context.preview)
                );
                previous_steps = sample->simulation_steps;
                previous_commands = sample->command_apply_count;
                previous_dropped = sample->dropped_seconds;
                xalloc_stats_get(&previous_allocations);
                continue;
            }
            const engine *runtime = game_prototype_preview_runtime(context.preview);
            const engine_frame_timing *sample = engine_last_timing(runtime);
            profile_add_timing(
                &timing,
                sample,
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
            const game_behavior_profile_frame *profile = engine_behavior_profile(runtime);
            if (profile) {
                for (int row = 0; row < profile->count; row++) {
                    profile_add_row(&rows, &profile->rows[row], &rows_truncated);
                }
                profile_add_row(&rows, &profile->overflow, &rows_truncated);
            }
        }
    }
    if (status != CHIBI_AUTOMATION_OK) {
        afree(rows);
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
    yyjson_mut_obj_add_uint(result->doc, value, "warmup_frames", warmup);
    yyjson_mut_obj_add_real(result->doc, value, "fixed_dt", fixed_dt);
    add_hash_named(result->doc, value, "world_hash", scenario_state_hash(&context));
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "live_entities",
        ecs_world_live_count(game_prototype_preview_world(context.preview))
    );
    yyjson_mut_obj_add_uint(
        result->doc,
        value,
        "authored_entities",
        game_prototype_preview_entity_count(context.preview)
    );
    add_execution_status(
        result->doc,
        value,
        "interpreter",
        true,
        graph_sljit_available() != 0,
        false,
        false,
        "behavior event graphs execute through the flow interpreter"
    );
    add_profile_timing(result->doc, value, &timing);
    add_profile_allocations(result->doc, value, &allocations, timing.frames);
    add_profile_rows(result->doc, value, rows, acount(rows), rows_truncated);
    afree(rows);
    scenario_context_free(&context);
    return 0;
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
    yyjson_val *events;
    scenario_action actions[SCENARIO_MAX_ACTIONS];
    int action_count;
    bool has_controls;
    uint64_t controls;
    yyjson_val *ui;
} scenario_input;

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
        return game_prototype_preview_entity(context->preview);
    }
    if (!context->scene_target) {
        return (ecs_entity){ 0 };
    }
    int index = game_scene_document_entity_index(context->scene, name);
    return index < 0 ? (ecs_entity){ 0 }
                     : game_prototype_preview_entity_at(context->preview, index);
}

static bool scenario_event_value(const scenario_context *context, yyjson_val *item, pin_value *out)
{
    if (!yyjson_is_obj(item)) {
        return false;
    }
    const char *type = request_string(item, "type");
    yyjson_val *value = yyjson_obj_get(item, "value");
    if (!type || !value) {
        return false;
    }
    if (!strcmp(type, "bool") && yyjson_is_bool(value)) {
        *out = (pin_value){ .type = PIN_BOOL, .b = yyjson_get_bool(value) };
        return true;
    }
    if ((!strcmp(type, "name") || !strcmp(type, "string")) &&
        yyjson_is_str(value) && yyjson_get_len(value) <= 4096) {
        *out = (pin_value){ .type = !strcmp(type, "name") ? PIN_NAME : PIN_STR,
                            .str = yyjson_get_str(value) };
        return true;
    }
    if (!strcmp(type, "entity") && yyjson_is_str(value) && yyjson_get_len(value)) {
        ecs_entity entity = context
                              ? scenario_entity(context, yyjson_get_str(value))
                              : (ecs_entity){ 0 };
        if (context &&
            !ecs_entity_is_alive(game_prototype_preview_world(context->preview), entity)) {
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
    if (!strcmp(type, "int") && yyjson_is_int(value) && number >= INT32_MIN &&
        number <= INT32_MAX) {
        *out = (pin_value){ .type = PIN_I32, .i = (int32_t)number };
        return true;
    }
    if (!strcmp(type, "uint") && yyjson_is_int(value) && number >= 0 &&
        number <= UINT32_MAX) {
        *out = (pin_value){ .type = PIN_U32, .u = (uint32_t)number };
        return true;
    }
    if (!strcmp(type, "float") && fabs(number) <= FLT_MAX) {
        *out = (pin_value){ .type = PIN_F32, .f = (float)number };
        return true;
    }
    if (!strcmp(type, "double")) {
        *out = (pin_value){ .type = PIN_F64, .d = number };
        return true;
    }
    return false;
}

static bool parse_scenario_input(yyjson_val *value, scenario_input *out, char *error, size_t error_size)
{
    if (!value || !yyjson_is_obj(value)) {
        snprintf(error, error_size, "each scenario input must be an object");
        return false;
    }
    *out = (scenario_input){ 0 };
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
            if (!name || !name[0] || strlen(name) > 128 ||
                (values && (!yyjson_is_arr(values) || yyjson_arr_size(values) > 16))) {
                snprintf(error, error_size, "events require a bounded name and at most 16 typed payload values");
                return false;
            }
            yyjson_val *target = yyjson_obj_get(event, "target");
            if (target && (!yyjson_is_str(target) || !yyjson_get_len(target) ||
                           yyjson_get_len(target) > 512)) {
                snprintf(error, error_size, "event target must be a scene entity name");
                return false;
            }
            for (size_t j = 0; j < yyjson_arr_size(values); j++) {
                pin_value parsed;
                if (!scenario_event_value(NULL, yyjson_arr_get(values, j), &parsed)) {
                    snprintf(error, error_size, "event payload requires a supported type and matching value");
                    return false;
                }
            }
        }
    }
    yyjson_val *controls = yyjson_obj_get(value, "controls");
    if (controls) {
        if (!yyjson_is_arr(controls) || yyjson_arr_size(controls) >= GAME_CONTROL_COUNT) {
            snprintf(error, error_size, "controls must be an array of GameControl values");
            return false;
        }
        out->has_controls = true;
        for (size_t i = 0; i < yyjson_arr_size(controls); i++) {
            yyjson_val *source = yyjson_arr_get(controls, i);
            int64_t code = yyjson_get_sint(source);
            if (!yyjson_is_int(source) || code <= GAME_CONTROL_NONE ||
                code >= GAME_CONTROL_COUNT) {
                snprintf(error, error_size, "control code is outside GameControl");
                return false;
            }
            out->controls |= UINT64_C(1) << code;
        }
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
        for (int i = 1; i < GAME_CONTROL_COUNT; i++) {
            if (input->controls & (UINT64_C(1) << i)) {
                yyjson_mut_arr_add_int(doc, controls, i);
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
    engine *runtime = game_prototype_preview_runtime(context->preview);
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
    if (input->has_controls) {
        context->controls = input->controls;
        context->controls_sampled = true;
    }
    bool firing = context->aim_firing;
    if (context->controls_sampled &&
        engine_sample_controls(runtime, context->controls, &firing) < 0) {
        snprintf(error, error_size, "invalid authored action binding");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    if (engine_sample_aim(runtime, context->aim, firing, context->aim_point) < 0) {
        snprintf(error, error_size, "scenario aim input failed");
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    /* A UI activation as the presenter delivers it: a value first, then the
       event the element emits to its GameUi entity. */
    for (size_t i = 0; i < yyjson_arr_size(input->ui); i++) {
        yyjson_val *action = yyjson_arr_get(input->ui, i);
        ecs_entity target = scenario_entity(context, request_string(action, "target"));
        yyjson_val *value = yyjson_obj_get(action, "value");
        if (!GAME_GameUi_has(game_prototype_preview_world(context->preview), target)) {
            snprintf(error, error_size, "UI target must be an entity with GameUi");
            return CHIBI_AUTOMATION_INVALID_REQUEST;
        }
        if (value) {
            game_ui_set_value(
                engine_ui(runtime),
                ecs_entity_to_u64(target),
                request_string(action, "element"),
                (float)yyjson_get_num(value)
            );
        }
        game_entities_request_event(
            game_prototype_preview_entities(context->preview),
            target,
            request_string(action, "event"),
            (flow_inputs){ 0 }
        );
    }
    game_behavior_world *behaviors = game_prototype_preview_behaviors(context->preview);
    flow_error flow = { 0 };
    if (input->events) {
        size_t count = yyjson_arr_size(input->events);
        for (size_t i = 0; i < count; i++) {
            yyjson_val *event = yyjson_arr_get(input->events, i);
            const char *name = request_string(event, "name");
            ecs_entity entity = scenario_entity(context, request_string(event, "target"));
            if (!ecs_entity_is_alive(game_prototype_preview_world(context->preview), entity)) {
                snprintf(error, error_size, "event target is missing or no longer alive");
                return CHIBI_AUTOMATION_INVALID_REQUEST;
            }
            yyjson_val *values = yyjson_obj_get(event, "values");
            pin_value payload[16];
            int value_count = (int)yyjson_arr_size(values);
            for (int j = 0; j < value_count; j++) {
                if (!scenario_event_value(context, yyjson_arr_get(values, (size_t)j), &payload[j])) {
                    snprintf(error, error_size, "event payload contains an unresolved entity");
                    return CHIBI_AUTOMATION_INVALID_REQUEST;
                }
            }
            flow = (flow_error){ 0 };
            if (game_behavior_dispatch(
                    behaviors,
                    entity,
                    name,
                    (flow_inputs){ .values = payload, .count = value_count },
                    &flow
                ) < 0) {
                snprintf(error, error_size, "%s", flow.message ? flow.message : "scenario event failed");
                return CHIBI_AUTOMATION_EXECUTION_ERROR;
            }
        }
    }

    for (int i = 0; i < input->action_count; i++) {
        flow = (flow_error){ 0 };
        if (game_prototype_preview_input(
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
        if (apply_scenario_input(context, &inputs[frame], error, error_size) !=
                CHIBI_AUTOMATION_OK ||
            game_prototype_preview_tick(context->preview, (double)(frame + 1) * fixed_dt) !=
                0) {
            if (!error[0]) {
                snprintf(error, error_size, "prototype scenario failed");
            }
            return CHIBI_AUTOMATION_EXECUTION_ERROR;
        }
        observations->world_hash[frame] = scenario_state_hash(context);
        observations->live_entities[frame] = ecs_world_live_count(
            game_prototype_preview_world(context->preview)
        );
    }
    observations->has_live_entities = true;
    return CHIBI_AUTOMATION_OK;
}

typedef struct
{
    scenario_input *inputs;
    uint64_t *at;
    size_t count, next;
    uint64_t frame;
    bool sampled;
} scenario_sequence;

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
    if (game_prototype_preview_tick(context->preview, (double)(++sequence->frame) * fixed_dt) !=
        0) {
        set_error(
            error,
            error_size,
            engine_error(game_prototype_preview_runtime(context->preview))
        );
        return CHIBI_AUTOMATION_EXECUTION_ERROR;
    }
    return CHIBI_AUTOMATION_OK;
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
    yyjson_doc *request;
    char *page; /* the root ui document its UI shows on, or NULL */
};

game_prototype_preview *chibi_preview_runtime(chibi_preview_run *run)
{
    return run->context.preview;
}

const char *chibi_preview_page(const chibi_preview_run *run)
{
    return run->page;
}

/* The scene's root ui document, else the default in the manifest at `root`. */
static char *preview_page(const char *root, const game_scene_document *scene)
{
    return scene && scene->ui ? xstrdup(scene->ui) : manifest_page(root);
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
    yyjson_doc_free(run->request);
    free(run->page);
    free(run);
}

int chibi_automation_preview(
    const char *root,
    const char *request,
    chibi_preview_run **out,
    bool *perspective,
    char *error,
    size_t error_size
)
{
    *out = NULL;
    *perspective = false;
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
    chibi_preview_run *run = xcalloc(1, sizeof *run);
    run->request = doc;
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
        *perspective = context->scene
                         ? context->scene->perspective
                         : GAME_GameGeometry3D_has(
                               game_prototype_preview_world(context->preview),
                               game_prototype_preview_entity(context->preview)
                           );
        run->page = preview_page(root, context->scene);
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
    char *name = asset_name_from_filename(context->prototype_ref.filename);
    yyjson_mut_obj_add_strcpy(doc, recording, "name", name);
    free(name);
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
    const game_prototype_registry *registry,
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

    char *fields_json = game_prototype_entity_component_json(world, entity, component, registry);
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
            pin_read_field(ecs_component_data_const(world, entity, component->id), field, &handle);
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

static void add_snapshot_behavior(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    game_behavior_world *behaviors,
    ecs_entity entity,
    int index
)
{
    game_behavior_attachment_info info = { 0 };
    if (game_behavior_attachment_info_at(behaviors, entity, index, &info) != 0) {
        return;
    }
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "asset", info.asset_name ? info.asset_name : "");
    yyjson_mut_obj_add_uint(doc, object, "sequence", info.sequence);
    yyjson_mut_obj_add_str(
        doc,
        object,
        "backend",
        behavior_backend_name(behavior_instance_backend(info.instance))
    );
    yyjson_mut_obj_add_bool(doc, object, "debug_hold", info.debug_hold != 0);
    node *tick_entry = NULL;
    const behavior_function *
        tick = behavior_program_batch_event(info.instance->asset->program, "Tick", &tick_entry);
    yyjson_mut_obj_add_bool(doc, object, "tick_system_compiled", tick && behavior_function_system_entry(tick));
    yyjson_mut_obj_add_bool(doc, object, "tick_parallel_safe", behavior_function_parallel(tick));
    const compile_report *report = behavior_program_report(info.instance->asset->program);
    yyjson_mut_val *performance = yyjson_mut_obj_add_arr(doc, object, "performance_diagnostics");
    for (int i = 0; report && i < acount(report->items); i++) {
        const compile_diagnostic *d = &report->items[i];
        if (d->rule == CMPL_RULE_QUERY_FUSION ||
            d->rule == CMPL_RULE_QUERY_SCAN || d->rule == CMPL_RULE_CONTAINER) {
            add_diagnostic(doc, performance, info.instance->asset, d);
        }
    }
    const behavior_instance_failure *failure = behavior_instance_last_failure(info.instance);
    if (failure) {
        yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "last_failure");
        yyjson_mut_obj_add_int(doc, value, "code", failure->code);
        yyjson_mut_obj_add_int(doc, value, "pin", failure->pin);
        yyjson_mut_obj_add_uint(doc, value, "steps", failure->steps);
        yyjson_mut_obj_add_uint(doc, value, "graph_id", failure->graph_id);
        yyjson_mut_obj_add_uint(doc, value, "node_id", failure->node_id);
        yyjson_mut_obj_add_uint(doc, value, "program_revision", failure->program_revision);
        yyjson_mut_obj_add_str(
            doc,
            value,
            "backend",
            behavior_backend_name(failure->backend)
        );
        yyjson_mut_obj_add_strcpy(doc, value, "event", failure->event_name);
        yyjson_mut_obj_add_strcpy(doc, value, "graph", failure->graph_name);
        yyjson_mut_obj_add_strcpy(doc, value, "message", failure->message);
    } else {
        yyjson_mut_obj_add_null(doc, object, "last_failure");
    }
    game_behavior_entity_state state = { 0 };
    if (game_behavior_entity_state_at(behaviors, entity, info.asset_name, &state) == 0) {
        yyjson_mut_val *state_value = yyjson_mut_obj_add_obj(doc, object, "state");
        yyjson_mut_obj_add_bool(doc, state_value, "active", state.active != 0);
        yyjson_mut_obj_add_bool(doc, state_value, "suspended", state.suspended != 0);
        yyjson_mut_obj_add_real(doc, state_value, "wake_time", state.wake_time);
    }
}

/* What behaviors set on GameUi documents, as a presenter would show it: each
 * entity's selectors with the text, value, visibility and classes set under
 * them. */
static void add_snapshot_ui(yyjson_mut_doc *doc, yyjson_mut_val *object, game_behavior_world *behaviors)
{
    game_ui *ui = game_behavior_ui(behaviors);
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, "ui");
    for (int i = 0; i < game_ui_element_count(ui); i++) {
        const game_ui_element *e = game_ui_element_at(ui, i);
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
    game_behavior_world *behaviors,
    const game_prototype_registry *registry,
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

    int behavior_count = game_behavior_attachment_count_for_entity(behaviors, entity);
    yyjson_mut_obj_add_int(doc, object, "behavior_count", behavior_count);
    yyjson_mut_val *attachments = yyjson_mut_obj_add_arr(doc, object, "behaviors");
    int emitted_behaviors = 0;
    for (int i = 0; i < behavior_count; i++) {
        if ((uint64_t)emitted_behaviors >= behavior_limit) {
            break;
        }
        add_snapshot_behavior(doc, attachments, behaviors, entity, i);
        emitted_behaviors++;
    }
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
    ecs_world *world = game_prototype_preview_world(context.preview);
    game_behavior_world *behaviors = game_prototype_preview_behaviors(context.preview);
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
        game_particles_count(game_prototype_preview_particles(context.preview))
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
            ecs_entity authored = game_prototype_preview_entity_at(context.preview, i);
            if (ecs_entity_to_u64(authored) == ecs_entity_to_u64(entity)) {
                yyjson_mut_obj_add_strcpy(
                    result->doc,
                    record,
                    "name",
                    context.scene->entities[i].name
                );
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
    int log_count = game_behavior_log_count(behaviors);
    yyjson_mut_obj_add_int(result->doc, value, "log_count", log_count);
    yyjson_mut_val *logs = yyjson_mut_obj_add_arr(result->doc, value, "logs");
    int emitted_logs = 0;
    for (int i = 0; i < log_count && emitted_logs < RUNTIME_SNAPSHOT_MAX_LOGS; i++) {
        game_behavior_log_info info = { 0 };
        if (game_behavior_log_info_at(behaviors, i, &info) != 0) {
            continue;
        }
        yyjson_mut_val *log = yyjson_mut_arr_add_obj(result->doc, logs);
        yyjson_mut_obj_add_uint(result->doc, log, "entity", ecs_entity_to_u64(info.entity));
        yyjson_mut_obj_add_strcpy(result->doc, log, "asset", info.asset_name ? info.asset_name : "");
        yyjson_mut_obj_add_strcpy(result->doc, log, "message", info.message);
        if (info.failure) {
            yyjson_mut_obj_add_bool(result->doc, log, "failure", true);
        }
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

    ecs_world *world = game_prototype_preview_world(context.preview);
    game_behavior_world *behaviors = game_prototype_preview_behaviors(context.preview);
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
        game_particles_count(game_prototype_preview_particles(context.preview))
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
    int log_count = game_behavior_log_count(behaviors);
    yyjson_mut_obj_add_int(result->doc, value, "log_count", log_count);
    yyjson_mut_val *logs = yyjson_mut_obj_add_arr(result->doc, value, "logs");
    int emitted_logs = 0;
    for (int i = 0; i < log_count && emitted_logs < RUNTIME_SNAPSHOT_MAX_LOGS; i++) {
        game_behavior_log_info info = { 0 };
        if (game_behavior_log_info_at(behaviors, i, &info) != 0) {
            continue;
        }
        yyjson_mut_val *log = yyjson_mut_arr_add_obj(result->doc, logs);
        yyjson_mut_obj_add_uint(result->doc, log, "entity", ecs_entity_to_u64(info.entity));
        yyjson_mut_obj_add_strcpy(result->doc, log, "asset", info.asset_name ? info.asset_name : "");
        yyjson_mut_obj_add_strcpy(result->doc, log, "message", info.message);
        if (info.failure) {
            yyjson_mut_obj_add_bool(result->doc, log, "failure", true);
        }
        if (info.origin) {
            yyjson_mut_val *origin = yyjson_mut_obj_add_obj(result->doc, log, "origin");
            yyjson_mut_obj_add_uint(result->doc, origin, "id", info.origin->id);
            yyjson_mut_obj_add_strcpy(
                result->doc,
                origin,
                "name",
                info.origin->desc && info.origin->desc->name ? info.origin->desc->name : ""
            );
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
    const game_prototype_registry *registry
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
    }
    char *defaults_json = game_prototype_component_defaults_json(registry, component);
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
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
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
        for (int i = 0; i < registry_type_count(); i++) {
            add_type(doc, types, registry_type(i));
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
        for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
            const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
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

/* Behavior graphs author component nodes, which lower to the generated nodes
 * System graphs use directly; Systems offer the nodes their compiler turns
 * into bytecode. No domain offers every domain's authoring nodes. */
static bool domain_offers(const char *domain, const node_desc *node)
{
    if (domain && !strcmp(domain, "system")) {
        return game_system_node_supported(node);
    }
    bool shader = node->category && !strncmp(node->category, "Shader", 6);
    bool field = node->category && !strcmp(node->category, "Procedural Field");
    const char *own = shader ? "shader" : field ? "field" : "behavior";
    return (!domain || !strcmp(domain, own)) && !component_generated_kind(node);
}

/* Component node presets and events belong to behavior graphs. */
static bool behavior_vocabulary(const char *domain)
{
    return !domain || !strcmp(domain, "behavior");
}

/* The events a project names: its event definitions, and those its ui
 * documents send behaviors (assets/ui_events.h). */
typedef struct
{
    behavior_asset **definitions;
    ui_event *ui;
} project_events;

static bool defined_event(const project_events *events, const char *name)
{
    for (int i = 0; i < acount(events->definitions); i++) {
        if (!strcmp(events->definitions[i]->name, name)) {
            return true;
        }
    }
    return graph_event_host_name(name);
}

/* The names of the ui documents whose events reach `behavior`: the GameUi
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
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash, error, error_size)) {
        return CHIBI_AUTOMATION_IO_ERROR;
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
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", root, entries[i].relative);
        char *text = read_file(path, NULL, NULL, error, error_size);
        ui_document_event *found = NULL;
        if (text) {
            ui_document_events(text, &found);
        }
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
    const graph_host_event_desc *host;
    const behavior_asset *definition;
    const ui_event *ui;
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
static node *match_node(graph *g, const node_match *match)
{
    node *n = graph_add_node(g, match->desc);
    assert(n);
    const char *event = match_event(match);
    int result = 0;
    if (match->preset) {
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

/* A preset reads as its access, its type and its field: "Set GameHealth
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

static int match_score(const node_match *match, const char *query)
{
    return match->preset      ? preset_score(match->preset, query)
         : match_event(match) ? event_score(match, query)
                              : node_score(match->desc, query);
}

/* How Behavior Expression writes a node over its data inputs: "clamp(Value,
 * Min, Max)", "A + B" or "-Value". A component node reads as the node it
 * lowers to, so a Make CF_V2 is "vec2(x, y)". */
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
        if (!domain_offers(domain, node) || component_generated_kind(node)) {
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
        for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
            yyjson_mut_arr_add_str(doc, events, GRAPH_HOST_EVENTS[i].name);
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
        if (domain_offers(domain, registry_node(i))) {
            apush(matches, ((node_match){ .desc = registry_node(i) }));
        }
    }
    if (!behavior_vocabulary(domain)) {
        return matches;
    }
    for (int i = 0; i < acount(presets); i++) {
        apush(matches, ((node_match){ .desc = presets[i].desc, .preset = &presets[i] }));
    }
    for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
        apush(matches, ((node_match){ .desc = &NODE_Event, .host = &GRAPH_HOST_EVENTS[i] }));
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
    return matches;
}

/* A pin type as searchNodes writes it: "exec", "any", "float", "GameHealth",
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
    const type_desc *type = registry_type_by_name(base);
    const enum_desc *enumeration = registry_enum_by_name(base);
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
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "wire_from and wire_to take a pin type as searchNodes writes it, such as exec, float, GameHealth or array<int>");
        return -1;
    }
    if ((yyjson_obj_get(request, "query") && !query) ||
        (yyjson_obj_get(request, "domain") &&
         (!domain || (strcmp(domain, "shader") && strcmp(domain, "behavior") &&
                      strcmp(domain, "field") && strcmp(domain, "system")))) ||
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
    project_events events = { 0 };
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
        bool offered = listed || !match_event(&match);
        match.order = i;
        match.score = offered ? match_score(&match, query) : -1;
        match.wire_input = match.wire_output = -1;
        if (match.score < 0) {
            continue;
        }
        if (pin_filtered || wired) {
            bool configured = match.preset || match_event(&match);
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
    qsort(matches, (size_t)acount(matches), sizeof *matches, node_match_compare);
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
    behavior_asset *asset = behavior_asset_load_json(json);
    free(json);
    behavior_asset **libraries = NULL;
    if (!asset) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, "Invalid field asset");
        return -1;
    }
    status = load_behavior_family_assets(root, family_by_kind("library"), &libraries, error, sizeof error);
    behavior_asset *linked = status == CHIBI_AUTOMATION_OK
                               ? behavior_asset_link_libraries(
                                     asset,
                                     (const behavior_asset *const *)libraries,
                                     acount(libraries),
                                     error,
                                     sizeof error
                                 )
                               : NULL;
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    bool valid = linked &&
                 automation_field_preview(linked, request, result->doc, value, error, sizeof error);
    behavior_asset_free(asset);
    behavior_asset_free(linked);
    free_behavior_assets(libraries);
    if (!valid) {
        response_error(result, exit_code, CHIBI_AUTOMATION_VALIDATION_ERROR, error);
        return -1;
    }

    return 0;
}

static int handle_edit_graph(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *create_value = yyjson_obj_get(request, "create");
    yyjson_val *dry_value = yyjson_obj_get(request, "dry_run");
    const char *expected = request_string(request, "expected_hash");
    bool create = create_value && yyjson_get_bool(create_value);
    bool dry = dry_value && yyjson_get_bool(dry_value);
    if ((create_value && !yyjson_is_bool(create_value)) ||
        (dry_value && !yyjson_is_bool(dry_value)) || yyjson_obj_get(request, "layout") ||
        (!create && (!expected || strlen(expected) != 16))) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "editing requires expected_hash and boolean create/dry_run; placement is automatic");
        return -1;
    }
    asset_ref ref = { 0 };
    char error[256] = { 0 };
    if (resolve_asset_ref(request, &ref, error, sizeof error) ||
        (ref.family->loader != ASSET_GRAPH && ref.family->loader != ASSET_SYSTEM)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, error[0] ? error : "edit_graph requires a graph asset");
        return -1;
    }
    char *source = NULL;
    size_t bytes = 0;
    uint64_t before = 0;
    int status = CHIBI_AUTOMATION_OK;
    if (!create) {
        status = load_asset(root, request, &ref, &source, &bytes, &before, error, sizeof error);
    } else if (verify_asset_directory(root, ref.family, error, sizeof error) < 0) {
        status = CHIBI_AUTOMATION_INVALID_REQUEST;
    } else {
        char *path = asset_full_path(root, &ref);
        struct stat info;
        if (!lstat(path, &info)) {
            status = CHIBI_AUTOMATION_CONFLICT;
            snprintf(error, sizeof error, "asset already exists");
        } else if (errno != ENOENT) {
            status = CHIBI_AUTOMATION_IO_ERROR;
            snprintf(error, sizeof error, "cannot inspect asset destination");
        }
        free(path);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, before);
    yyjson_mut_obj_add_bool(result->doc, value, "published", false);
    add_hash_named(result->doc, value, "before_hash", before);
    char actual[17];
    snprintf(actual, sizeof actual, "%016llx", (unsigned long long)before);
    if (!create && strcmp(expected, actual)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_CONFLICT, "asset changed; inspect its current hash and retry");
        free(source);
        return -1;
    }
    char *name = asset_name_from_filename(ref.filename);
    char *edited = NULL;
    behavior_asset **libraries = NULL;
    bool shader = !strcmp(ref.family->kind, "material") ||
                  !strcmp(ref.family->kind, "shape") ||
                  !strcmp(ref.family->kind, "shader-library");
    status = load_behavior_family_assets(
        root,
        family_by_kind(shader ? "shader-library" : "library"),
        &libraries,
        error,
        sizeof error
    );
    if (status == CHIBI_AUTOMATION_OK && !shader) {
        behavior_asset **events = NULL;
        status = load_behavior_family_assets(root, family_by_kind("event"), &events, error, sizeof error);
        for (int i = 0; i < acount(events); i++) {
            apush(libraries, events[i]);
        }
        afree(events);
    }
    if (status == CHIBI_AUTOMATION_OK) {
        status = automation_graph_edit(
            source,
            ref.family->kind,
            name,
            (const behavior_asset *const *)libraries,
            acount(libraries),
            request,
            result->doc,
            value,
            &edited,
            error,
            sizeof error
        );
    }
    if (status == CHIBI_AUTOMATION_OK && ref.family->loader == ASSET_SYSTEM) {
        yyjson_doc *original = source ? parse_asset_text(source) : NULL;
        yyjson_val *query = yyjson_obj_get(request, "query");
        if (!query && original) {
            query = yyjson_obj_get(yyjson_doc_get_root(original), "query");
        }
        game_system_document *document = game_system_document_load(
            edited,
            automation_query_component,
            NULL,
            error,
            sizeof error
        );
        char *query_json = query ? yyjson_val_write(query, 0, NULL) : NULL;
        ecs_query_expr *expression = query && !yyjson_is_null(query)
                                       ? game_query_load(
                                             query_json,
                                             automation_query_component,
                                             NULL,
                                             error,
                                             sizeof error
                                         )
                                       : NULL;
        if (!document || (query && !yyjson_is_null(query) && !expression)) {
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        if (status == CHIBI_AUTOMATION_OK) {
            game_system_document_set_query(document, expression);
            compile_report report;
            compile_report_init(&report);
            if (behavior_asset_compile_for_system(game_system_document_behavior_asset(document), &report)) {
                snprintf(
                    error,
                    sizeof error,
                    "%s",
                    compile_report_first_error(&report)->message
                );
                status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            }
            compile_report_free(&report);
        }
        if (status == CHIBI_AUTOMATION_OK) {
            char *saved = game_system_document_save(
                document,
                automation_query_component,
                automation_query_component_name,
                NULL
            );
            free(edited);
            edited = saved;
        }
        ecs_query_expr_free(expression);
        free(query_json);
        game_system_document_free(document);
        yyjson_doc_free(original);
    }
    free_behavior_assets(libraries);
    free(name);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        free(edited);
        return -1;
    }
    if (to_disk_text(&edited, error, sizeof error)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_EXECUTION_ERROR, error);
        free(source);
        return -1;
    }
    size_t after_bytes = strlen(edited);
    uint64_t after = automation_content_hash(edited, after_bytes);
    bool changed = !source || strcmp(source, edited);
    yyjson_mut_obj_add_bool(result->doc, value, "changed", changed);
    yyjson_mut_obj_add_bool(result->doc, value, "dry_run", dry);
    add_hash_named(result->doc, value, "after_hash", after);
    yyjson_mut_obj_add_uint(result->doc, value, "after_bytes", after_bytes);
    if (!dry && changed) {
        status = automation_asset_publish(
            root,
            ref.family->folder,
            ref.filename,
            edited,
            expected,
            create,
            error,
            sizeof error
        );
        if (status != CHIBI_AUTOMATION_OK) {
            response_error(result, exit_code, status, error);
        }
    }
    if (status == CHIBI_AUTOMATION_OK && !dry) {
        yyjson_mut_obj_remove_key(value, "published");
        yyjson_mut_obj_add_bool(result->doc, value, "published", true);
        yyjson_mut_obj_remove_key(value, "hash");
        add_hash(result->doc, value, after);
        yyjson_mut_obj_remove_key(value, "bytes");
        yyjson_mut_obj_add_uint(result->doc, value, "bytes", after_bytes);
    }
    free(edited);
    free(source);
    return status == CHIBI_AUTOMATION_OK ? 0 : -1;
}

static int validate_prototype_behaviors(const char *root, const char *json, char *error, size_t error_size)
{
    yyjson_doc *document = parse_asset_text(json);
    yyjson_val *root_value = document ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *behaviors = root_value && yyjson_is_obj(root_value)
                              ? yyjson_obj_get(root_value, "behaviors")
                              : NULL;
    if (!document || (behaviors && !yyjson_is_arr(behaviors))) {
        yyjson_doc_free(document);
        snprintf(error, error_size, "edited prototype has an invalid behaviors array");
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    if (!behaviors) {
        yyjson_doc_free(document);
        return CHIBI_AUTOMATION_OK;
    }
    yyjson_arr_iter iterator = yyjson_arr_iter_with(behaviors);
    yyjson_val *name;
    while ((name = yyjson_arr_iter_next(&iterator))) {
        if (!yyjson_is_str(name) || !yyjson_get_len(name)) {
            yyjson_doc_free(document);
            snprintf(error, error_size, "prototype behavior names must be non-empty strings");
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        asset_ref ref = { 0 };
        char *behavior_json = NULL;
        int status = load_named_asset(
            root,
            family_by_kind("behavior"),
            yyjson_get_str(name),
            &ref,
            &behavior_json,
            NULL,
            NULL,
            error,
            error_size
        );
        if (status != CHIBI_AUTOMATION_OK) {
            yyjson_doc_free(document);
            free(behavior_json);
            return status;
        }
        behavior_asset *behavior = behavior_asset_load_json(behavior_json);
        free(behavior_json);
        if (!behavior) {
            yyjson_doc_free(document);
            snprintf(error, error_size, "%s", graph_last_error());
            return CHIBI_AUTOMATION_VALIDATION_ERROR;
        }
        behavior_asset_free(behavior);
    }
    yyjson_doc_free(document);
    return CHIBI_AUTOMATION_OK;
}

static char *new_prototype_json(const char *name)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "name", name);
    char identity[32];
    snprintf(
        identity,
        sizeof identity,
        "prototype-%016llx",
        (unsigned long long)automation_content_hash(name, strlen(name))
    );
    yyjson_mut_obj_add_strcpy(doc, root, "identity", identity);
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

static int validate_scene_behavior(const char *root, const char *name, char *error, size_t error_size)
{
    asset_ref ref = { 0 };
    char *json = NULL;
    int status = load_named_asset(root, family_by_kind("behavior"), name, &ref, &json, NULL, NULL, error, error_size);
    if (status != CHIBI_AUTOMATION_OK) {
        free(json);
        return status;
    }
    behavior_asset *behavior = behavior_asset_load_json(json);
    free(json);
    if (!behavior) {
        snprintf(error, error_size, "%s", graph_last_error());
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    if (behavior->owner_type_name && strcmp(behavior->owner_type_name, "ecs.entity")) {
        snprintf(error, error_size, "behavior %s cannot attach to a scene entity", name);
        behavior_asset_free(behavior);
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    behavior_asset_free(behavior);
    return CHIBI_AUTOMATION_OK;
}

static int validate_scene_dependencies(const char *root, const char *json, char *error, size_t error_size)
{
    game_scene_document *scene = game_scene_document_load(json, error, error_size);
    if (!scene) {
        return CHIBI_AUTOMATION_VALIDATION_ERROR;
    }
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    int status = CHIBI_AUTOMATION_OK;
    for (int i = 0; i < acount(scene->entities) && status == CHIBI_AUTOMATION_OK; i++) {
        game_scene_entity *entity = &scene->entities[i];
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
        if (status != CHIBI_AUTOMATION_OK) {
            free(prototype_json);
            break;
        }
        game_prototype *prototype = game_prototype_load(prototype_json, &registry, error, error_size);
        if (!prototype) {
            free(prototype_json);
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            break;
        }
        status = validate_prototype_behaviors(root, prototype_json, error, error_size);
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
                game_prototype_spawn_components(prototype, world, &actor, error, error_size) !=
                    ECS_OK) {
                status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            }
            if (status == CHIBI_AUTOMATION_OK &&
                game_prototype_apply_components_json(
                    world,
                    actor,
                    entity->components_json ? entity->components_json : "{}",
                    &registry,
                    error,
                    error_size
                ) != 0) {
                status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            }
            ecs_world_free(world);
        }
        game_prototype_free(prototype);
        for (int behavior = 0;
             behavior < acount(entity->behaviors) && status == CHIBI_AUTOMATION_OK;
             behavior++) {
            status = validate_scene_behavior(root, entity->behaviors[behavior], error, error_size);
        }
    }
    game_scene_document_free(scene);
    return status;
}

static char *new_scene_json(const char *name)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "version", 1);
    yyjson_mut_obj_add_strcpy(doc, root, "name", name);
    yyjson_mut_obj_add_str(doc, root, "presentation", "2d");
    yyjson_mut_obj_add_arr(doc, root, "entities");
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

static int handle_edit_prototype(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *create_value = yyjson_obj_get(request, "create");
    yyjson_val *dry_value = yyjson_obj_get(request, "dry_run");
    const char *expected = request_string(request, "expected_hash");
    bool create = create_value && yyjson_get_bool(create_value);
    bool dry = dry_value && yyjson_get_bool(dry_value);
    if ((create_value && !yyjson_is_bool(create_value)) ||
        (dry_value && !yyjson_is_bool(dry_value)) ||
        (!create && (!expected || strlen(expected) != 16))) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "editing requires expected_hash and boolean create/dry_run");
        return -1;
    }
    asset_ref ref = { 0 };
    char error[256] = { 0 };
    if (resolve_asset_ref(request, &ref, error, sizeof error) ||
        ref.family->loader != ASSET_PROTOTYPE) {
        response_error(
            result,
            exit_code,
            CHIBI_AUTOMATION_INVALID_REQUEST,
            error[0] ? error : "edit_prototype requires a prototype asset"
        );
        return -1;
    }
    char *source = NULL;
    size_t bytes = 0;
    uint64_t before = 0;
    int status = CHIBI_AUTOMATION_OK;
    if (!create) {
        status = load_asset(root, request, &ref, &source, &bytes, &before, error, sizeof error);
    } else if (verify_asset_directory(root, ref.family, error, sizeof error) < 0) {
        status = CHIBI_AUTOMATION_INVALID_REQUEST;
    } else {
        char *path = asset_full_path(root, &ref);
        struct stat info;
        if (!lstat(path, &info)) {
            status = CHIBI_AUTOMATION_CONFLICT;
            snprintf(error, sizeof error, "asset already exists");
        } else if (errno != ENOENT) {
            status = CHIBI_AUTOMATION_IO_ERROR;
            snprintf(error, sizeof error, "cannot inspect asset destination");
        }
        free(path);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, before);
    yyjson_mut_obj_add_bool(result->doc, value, "published", false);
    add_hash_named(result->doc, value, "before_hash", before);
    char actual[17];
    snprintf(actual, sizeof actual, "%016llx", (unsigned long long)before);
    if (!create && strcmp(expected, actual)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_CONFLICT, "asset changed; inspect its current hash and retry");
        free(source);
        return -1;
    }
    if (create) {
        char *name = asset_name_from_filename(ref.filename);
        source = new_prototype_json(name);
        free(name);
    }
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    char *edited = NULL;
    status = automation_prototype_edit(source, &registry, request, result->doc, value, &edited, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        free(edited);
        return -1;
    }
    status = validate_prototype_behaviors(root, edited, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        free(edited);
        return -1;
    }
    if (to_disk_text(&edited, error, sizeof error)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_EXECUTION_ERROR, error);
        free(source);
        return -1;
    }
    size_t after_bytes = strlen(edited);
    uint64_t after = automation_content_hash(edited, after_bytes);
    bool changed = !source || strcmp(source, edited);
    yyjson_mut_obj_add_bool(result->doc, value, "changed", changed);
    yyjson_mut_obj_add_bool(result->doc, value, "dry_run", dry);
    add_hash_named(result->doc, value, "after_hash", after);
    yyjson_mut_obj_add_uint(result->doc, value, "after_bytes", after_bytes);
    if (!dry && changed) {
        status = automation_asset_publish(
            root,
            ref.family->folder,
            ref.filename,
            edited,
            expected,
            create,
            error,
            sizeof error
        );
        if (status != CHIBI_AUTOMATION_OK) {
            response_error(result, exit_code, status, error);
        }
    }
    if (status == CHIBI_AUTOMATION_OK && !dry) {
        yyjson_mut_obj_remove_key(value, "published");
        yyjson_mut_obj_add_bool(result->doc, value, "published", true);
        yyjson_mut_obj_remove_key(value, "hash");
        add_hash(result->doc, value, after);
        yyjson_mut_obj_remove_key(value, "bytes");
        yyjson_mut_obj_add_uint(result->doc, value, "bytes", after_bytes);
    }
    free(edited);
    free(source);
    return status == CHIBI_AUTOMATION_OK ? 0 : -1;
}

/* Resolve creation defaults before the IO-free scene edit transaction. An
 * explicit empty list still opts out of the prototype's attached behaviors. */
static char *scene_request_defaults(const char *root, yyjson_val *request, char *error, size_t size)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *copy = yyjson_val_mut_copy(doc, request);
    yyjson_mut_doc_set_root(doc, copy);
    yyjson_mut_val *operations = yyjson_mut_obj_get(copy, "operations");
    size_t index, count;
    yyjson_mut_val *operation;
    bool valid = true;
    yyjson_mut_arr_foreach(operations, index, count, operation)
    {
        const char *op = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "op"));
        if (!op || strcmp(op, "add_entity") ||
            yyjson_mut_obj_get(operation, "behaviors")) {
            continue;
        }
        const char *name = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "prototype"));
        if (!name) {
            continue;
        }
        asset_ref ref = { 0 };
        char *json = NULL;
        if (load_named_asset(root, family_by_kind("prototype"), name, &ref, &json, NULL, NULL, error, size) !=
            CHIBI_AUTOMATION_OK) {
            valid = false;
            break;
        }
        yyjson_doc *prototype = parse_asset_text(json);
        yyjson_val *behaviors = yyjson_obj_get(yyjson_doc_get_root(prototype), "behaviors");
        yyjson_mut_obj_add_val(
            doc,
            operation,
            "behaviors",
            behaviors ? yyjson_val_mut_copy(doc, behaviors) : yyjson_mut_arr(doc)
        );
        yyjson_doc_free(prototype);
        free(json);
    }
    char *json = valid ? yyjson_mut_write(doc, 0, NULL) : NULL;
    yyjson_mut_doc_free(doc);
    return json;
}

static int handle_edit_scene(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *create_value = yyjson_obj_get(request, "create");
    yyjson_val *dry_value = yyjson_obj_get(request, "dry_run");
    const char *expected = request_string(request, "expected_hash");
    bool create = create_value && yyjson_get_bool(create_value);
    bool dry = dry_value && yyjson_get_bool(dry_value);
    if ((create_value && !yyjson_is_bool(create_value)) ||
        (dry_value && !yyjson_is_bool(dry_value)) ||
        (!create && (!expected || strlen(expected) != 16))) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "editing requires expected_hash and boolean create/dry_run");
        return -1;
    }
    asset_ref ref = { 0 };
    char error[256] = { 0 };
    if (resolve_asset_ref(request, &ref, error, sizeof error) ||
        ref.family->loader != ASSET_SCENE) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, error[0] ? error : "edit_scene requires a scene asset");
        return -1;
    }
    char *source = NULL;
    size_t bytes = 0;
    uint64_t before = 0;
    int status = CHIBI_AUTOMATION_OK;
    if (!create) {
        status = load_asset(root, request, &ref, &source, &bytes, &before, error, sizeof error);
    } else if (verify_asset_directory(root, ref.family, error, sizeof error) < 0) {
        status = CHIBI_AUTOMATION_INVALID_REQUEST;
    } else {
        char *path = asset_full_path(root, &ref);
        struct stat info;
        if (!lstat(path, &info)) {
            status = CHIBI_AUTOMATION_CONFLICT;
            snprintf(error, sizeof error, "asset already exists");
        } else if (errno != ENOENT) {
            status = CHIBI_AUTOMATION_IO_ERROR;
            snprintf(error, sizeof error, "cannot inspect asset destination");
        }
        free(path);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, before);
    yyjson_mut_obj_add_bool(result->doc, value, "published", false);
    add_hash_named(result->doc, value, "before_hash", before);
    char actual[17];
    snprintf(actual, sizeof actual, "%016llx", (unsigned long long)before);
    if (!create && strcmp(expected, actual)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_CONFLICT, "asset changed; inspect its current hash and retry");
        free(source);
        return -1;
    }
    if (create) {
        char *name = asset_name_from_filename(ref.filename);
        source = new_scene_json(name);
        free(name);
    }
    char *edited = NULL;
    char *defaulted = scene_request_defaults(root, request, error, sizeof error);
    yyjson_doc *defaults = defaulted ? yyjson_read(defaulted, strlen(defaulted), 0)
                                     : NULL;
    status = defaults ? automation_scene_edit(
                            source,
                            yyjson_doc_get_root(defaults),
                            result->doc,
                            value,
                            &edited,
                            error,
                            sizeof error
                        )
                      : CHIBI_AUTOMATION_VALIDATION_ERROR;
    yyjson_doc_free(defaults);
    free(defaulted);
    if (status == CHIBI_AUTOMATION_OK) {
        status = validate_scene_dependencies(root, edited, error, sizeof error);
    }
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        free(edited);
        return -1;
    }
    if (to_disk_text(&edited, error, sizeof error)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_EXECUTION_ERROR, error);
        free(source);
        return -1;
    }
    size_t after_bytes = strlen(edited);
    uint64_t after = automation_content_hash(edited, after_bytes);
    bool changed = !source || strcmp(source, edited);
    yyjson_mut_obj_add_bool(result->doc, value, "changed", changed);
    yyjson_mut_obj_add_bool(result->doc, value, "dry_run", dry);
    add_hash_named(result->doc, value, "after_hash", after);
    yyjson_mut_obj_add_uint(result->doc, value, "after_bytes", after_bytes);
    if (!dry && changed) {
        status = automation_asset_publish(
            root,
            ref.family->folder,
            ref.filename,
            edited,
            expected,
            create,
            error,
            sizeof error
        );
        if (status != CHIBI_AUTOMATION_OK) {
            response_error(result, exit_code, status, error);
        }
    }
    if (status == CHIBI_AUTOMATION_OK && !dry) {
        yyjson_mut_obj_remove_key(value, "published");
        yyjson_mut_obj_add_bool(result->doc, value, "published", true);
        yyjson_mut_obj_remove_key(value, "hash");
        add_hash(result->doc, value, after);
        yyjson_mut_obj_remove_key(value, "bytes");
        yyjson_mut_obj_add_uint(result->doc, value, "bytes", after_bytes);
    }
    free(edited);
    free(source);
    return status == CHIBI_AUTOMATION_OK ? 0 : -1;
}

static int handle_optimize_graph(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *create_value = yyjson_obj_get(request, "create");
    yyjson_val *dry_value = yyjson_obj_get(request, "dry_run");
    const char *expected = request_string(request, "expected_hash");
    bool dry = !dry_value || yyjson_get_bool(dry_value);
    if ((create_value && !yyjson_is_bool(create_value)) ||
        (create_value && yyjson_get_bool(create_value)) ||
        (dry_value && !yyjson_is_bool(dry_value)) ||
        (expected && strlen(expected) != 16) || (!dry && !expected)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "optimize_graph requires a graph asset, expected_hash unless dry_run, and boolean dry_run");
        return -1;
    }
    asset_ref ref = { 0 };
    char error[256] = { 0 };
    if (resolve_asset_ref(request, &ref, error, sizeof error) ||
        ref.family->loader != ASSET_GRAPH) {
        response_error(
            result,
            exit_code,
            CHIBI_AUTOMATION_INVALID_REQUEST,
            error[0] ? error : "optimize_graph requires a graph asset"
        );
        return -1;
    }
    char *source = NULL;
    size_t bytes = 0;
    uint64_t before = 0;
    int status = load_asset(root, request, &ref, &source, &bytes, &before, error, sizeof error);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, before);
    yyjson_mut_obj_add_bool(result->doc, value, "published", false);
    add_hash_named(result->doc, value, "before_hash", before);
    char actual[17];
    snprintf(actual, sizeof actual, "%016llx", (unsigned long long)before);
    if (expected && strcmp(expected, actual)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_CONFLICT, "asset changed; inspect its current hash and retry");
        free(source);
        return -1;
    }
    char *edited = NULL;
    status = automation_graph_optimize(source, request, result->doc, value, &edited, error, sizeof error);
    if (status == CHIBI_AUTOMATION_OK) {
        behavior_asset *asset = behavior_asset_load_json(edited);
        if (!asset) {
            snprintf(error, sizeof error, "optimized graph asset could not be loaded: %s", graph_last_error());
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
        } else {
            compile_report report;
            compile_report_init(&report);
            behavior_asset *linked = link_project_libraries(root, asset, error, sizeof error);
            if (linked) {
                behavior_asset_compile(linked, &report);
                inspect_add_validation(result->doc, value, linked, &report);
            }
            if (!linked || report.error_count) {
                snprintf(error, sizeof error, "optimized graph asset failed validation");
                status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            }
            compile_report_free(&report);
            behavior_asset_free(linked);
            behavior_asset_free(asset);
        }
    }
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        free(source);
        free(edited);
        return -1;
    }
    if (to_disk_text(&edited, error, sizeof error)) {
        response_error(result, exit_code, CHIBI_AUTOMATION_EXECUTION_ERROR, error);
        free(source);
        return -1;
    }
    size_t after_bytes = strlen(edited);
    uint64_t after = automation_content_hash(edited, after_bytes);
    bool changed = strcmp(source, edited) != 0;
    yyjson_mut_obj_add_bool(result->doc, value, "changed", changed);
    yyjson_mut_obj_add_bool(result->doc, value, "dry_run", dry);
    add_hash_named(result->doc, value, "after_hash", after);
    yyjson_mut_obj_add_uint(result->doc, value, "after_bytes", after_bytes);
    if (!dry && changed) {
        status = automation_asset_publish(
            root,
            ref.family->folder,
            ref.filename,
            edited,
            expected,
            false,
            error,
            sizeof error
        );
        if (status != CHIBI_AUTOMATION_OK) {
            response_error(result, exit_code, status, error);
        }
    }
    if (status == CHIBI_AUTOMATION_OK && !dry) {
        yyjson_mut_obj_remove_key(value, "published");
        yyjson_mut_obj_add_bool(result->doc, value, "published", true);
        yyjson_mut_obj_remove_key(value, "hash");
        add_hash(result->doc, value, after);
        yyjson_mut_obj_remove_key(value, "bytes");
        yyjson_mut_obj_add_uint(result->doc, value, "bytes", after_bytes);
    }
    free(edited);
    free(source);
    return status == CHIBI_AUTOMATION_OK ? 0 : -1;
}

/* ---------- Rename and safe delete ---------- */

/* The project as refactor.h plans on it: the catalog's index, the names
   navigation gives its assets, and each indexed asset's file text and hash. */
typedef struct
{
    catalog_asset *assets;
    navigation nav;
    int count;    /* the index's assets */
    char **texts; /* parallel to the index */
    uint64_t *hashes;
    int *catalog; /* the catalog asset at each index position, or -1 */
} refactor_view;

/* Publishing rebuilds the catalog, which frees the index and its assets. */
static void refactor_view_free(refactor_view *view)
{
    for (int i = 0; i < view->count; i++) {
        free(view->texts[i]);
    }
    free(view->texts);
    free(view->hashes);
    free(view->catalog);
    free((navigation_asset *)view->nav.assets);
}

static int refactor_view_load(const char *root, refactor_view *view, char *error, size_t size)
{
    uint64_t revision, content_hash;
    *view = (refactor_view){ 0 };
    if (catalog_snapshot_get(root, &view->assets, &revision, &content_hash, error, size)) {
        return CHIBI_AUTOMATION_IO_ERROR;
    }
    const reference_index *index = CATALOG_SNAPSHOT.references;
    view->nav = (navigation){ index, navigation_assets(view->assets, index) };
    view->count = reference_index_asset_count(index);
    view->catalog = catalog_positions(view->assets, index);
    view->texts = xcalloc((size_t)view->count + 1, sizeof *view->texts);
    view->hashes = xcalloc((size_t)view->count + 1, sizeof *view->hashes);
    for (int i = 0; i < view->count; i++) {
        if (view->catalog[i] < 0) {
            continue;
        }
        char *path = catalog_path(root, &view->assets[view->catalog[i]]);
        view->texts[i] = read_file(path, NULL, &view->hashes[i], error, size);
        free(path);
        if (!view->texts[i]) {
            return CHIBI_AUTOMATION_IO_ERROR;
        }
    }
    return CHIBI_AUTOMATION_OK;
}

/* References elsewhere, each placed as find_references places a use. */
static void add_refactor_uses(
    yyjson_mut_doc *doc,
    yyjson_mut_val *parent,
    const char *key,
    const refactor_view *view,
    const reference *const *uses
)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, parent, key);
    for (int i = 0; i < acount(uses); i++) {
        automation_add_use(&view->nav, doc, yyjson_mut_arr_add_obj(doc, array), uses[i], true);
    }
}

static void add_refactor_asset(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const refactor_view *view,
    int position
)
{
    const navigation_asset *asset = &view->nav.assets[position];
    yyjson_mut_obj_add_strcpy(doc, object, "kind", asset->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", asset->name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", asset->path);
}

static void add_refactor_edit(
    yyjson_mut_doc *doc,
    yyjson_mut_val *item,
    yyjson_mut_val *hashes,
    const refactor_view *view,
    const refactor_edit *edit
)
{
    const catalog_asset *asset = &view->assets[view->catalog[edit->asset]];
    char hash[17];
    hash_text(view->hashes[edit->asset], hash);
    add_refactor_asset(doc, item, view, edit->asset);
    yyjson_mut_obj_add_strcpy(doc, item, "hash", hash);
    yyjson_mut_obj_add(
        hashes,
        yyjson_mut_strcpy(doc, asset->asset.relative),
        yyjson_mut_strcpy(doc, hash)
    );
    if (edit->name) {
        char path[512];
        snprintf(
            path,
            sizeof path,
            "content/%s/%s%s",
            asset->asset.family->folder,
            edit->name,
            asset->asset.family->extension
        );
        yyjson_mut_obj_add_strcpy(doc, item, "new_name", edit->name);
        yyjson_mut_obj_add_strcpy(doc, item, "new_path", path);
    }
    if (edit->text) {
        add_hash_named(doc, item, "after_hash", automation_content_hash(edit->text, strlen(edit->text)));
    } else {
        yyjson_mut_obj_add_bool(doc, item, "deleted", true);
    }
    yyjson_mut_val *changes = yyjson_mut_obj_add_arr(doc, item, "changes");
    for (int i = 0; i < acount(edit->references); i++) {
        automation_add_use(&view->nav, doc, yyjson_mut_arr_add_obj(doc, changes), edit->references[i], false);
    }
}

/* Every asset the plan changes, with its changes and hashes. */
static void add_refactor_plan(
    yyjson_mut_doc *doc,
    yyjson_mut_val *value,
    const refactor_view *view,
    const refactor_plan *plan
)
{
    yyjson_mut_val *assets = yyjson_mut_obj_add_arr(doc, value, "assets");
    yyjson_mut_val *hashes = yyjson_mut_obj_add_obj(doc, value, "expected_hashes");
    for (int i = 0; i < acount(plan->edits); i++) {
        add_refactor_edit(doc, yyjson_mut_arr_add_obj(doc, assets), hashes, view, &plan->edits[i]);
    }
    yyjson_mut_val *mentions = yyjson_mut_obj_add_arr(doc, value, "mentions");
    for (int i = 0; i < acount(plan->mentions); i++) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, mentions);
        add_refactor_asset(doc, item, view, plan->mentions[i].asset);
        yyjson_mut_obj_add_strcpy(doc, item, "value", plan->mentions[i].path);
    }
    add_refactor_uses(doc, value, "dangling", view, plan->dangling);
}

/* The dry run's hashes name exactly the assets the plan changes, as they
   are now. */
static int refactor_expected(
    yyjson_val *request,
    const refactor_view *view,
    const refactor_plan *plan,
    char *error,
    size_t size
)
{
    yyjson_val *expected = yyjson_obj_get(request, "expected_hashes");
    if (!yyjson_is_obj(expected)) {
        set_error(error, size, "publishing requires expected_hashes from the dry run");
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    bool same = yyjson_obj_size(expected) == (size_t)acount(plan->edits);
    for (int i = 0; same && i < acount(plan->edits); i++) {
        const refactor_edit *edit = &plan->edits[i];
        char hash[17];
        hash_text(view->hashes[edit->asset], hash);
        const char *given = yyjson_get_str(
            yyjson_obj_get(expected, view->nav.assets[edit->asset].path)
        );
        same = given && !strcmp(given, hash);
    }
    if (!same) {
        set_error(error, size, "the assets changed since the dry run; run it again and pass its expected_hashes");
        return CHIBI_AUTOMATION_CONFLICT;
    }
    return CHIBI_AUTOMATION_OK;
}

/* A published asset: its path after the change, and whether it validated
   before. */
typedef struct
{
    char path[512];
    bool valid;
    bool deleted;
} refactor_published;

/* Counts the assets that validated before the change and no longer do, and
   lists their diagnostics; -1 when the catalog cannot be read. */
static int refactor_invalid(
    const char *root,
    const refactor_published *published,
    int count,
    response *result,
    yyjson_mut_val *value,
    char *error,
    size_t size
)
{
    catalog_asset *assets = NULL;
    uint64_t revision, content_hash;
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash, error, size)) {
        return -1;
    }
    yyjson_mut_val *invalid = yyjson_mut_obj_add_arr(result->doc, value, "invalid");
    int failed = 0;
    for (int i = 0; i < count; i++) {
        for (int j = 0;
             published[i].valid && !published[i].deleted && j < acount(assets);
             j++) {
            if (!strcmp(assets[j].asset.relative, published[i].path) &&
                !assets[j].valid) {
                yyjson_mut_val *item = yyjson_mut_arr_add_obj(result->doc, invalid);
                yyjson_mut_obj_add_strcpy(
                    result->doc,
                    item,
                    "path",
                    assets[j].asset.relative
                );
                add_catalog_diagnostics(result->doc, item, &assets[j]);
                failed++;
            }
        }
    }
    return failed;
}

/* A ui asset's styles, which follow its markup when a plan renames or
   deletes it. */
typedef struct
{
    char filename[256], rename[256], hash[17];
    char *text;
} refactor_styles;

/* The styles of a ui asset `edit` renames or deletes, as its change; false
   when it has none, or is no ui asset. */
static bool styles_change(
    const char *root,
    const catalog_asset *asset,
    const refactor_edit *edit,
    refactor_styles *styles,
    automation_asset_change *change
)
{
    if (asset->asset.family->loader != ASSET_UI || (edit->text && !edit->name)) {
        return false;
    }
    snprintf(
        styles->filename,
        sizeof styles->filename,
        "%s" UI_STYLE_EXTENSION,
        asset->name
    );
    char path[1024], error[160];
    snprintf(
        path,
        sizeof path,
        "%s/content/%s/%s",
        root,
        asset->asset.family->folder,
        styles->filename
    );
    struct stat info;
    if (lstat(path, &info) != 0) {
        return false;
    }
    uint64_t hash = 0;
    styles->text = read_file(path, NULL, &hash, error, sizeof error);
    hash_text(hash, styles->hash);
    snprintf(styles->rename, sizeof styles->rename, "%s" UI_STYLE_EXTENSION, edit->name ? edit->name : "");
    *change = (automation_asset_change){ .folder = asset->asset.family->folder,
                                         .filename = styles->filename,
                                         .rename_to = edit->name ? styles->rename : NULL,
                                         .text = edit->name ? styles->text : NULL,
                                         .expected_hash = styles->hash };
    return styles->text != NULL;
}

/* Publishes a plan's edits as one batch, validates each asset it changed,
   and puts every original back when one that validated no longer does. */
static int refactor_publish(
    const char *root,
    const refactor_view *view,
    const refactor_plan *plan,
    bool revalidate,
    response *result,
    yyjson_mut_val *value,
    char *error,
    size_t size
)
{
    int count = acount(plan->edits), total = count;
    automation_asset_change *changes = xcalloc((size_t)count * 2, sizeof *changes);
    refactor_styles *styles = xcalloc((size_t)count, sizeof *styles);
    refactor_published *published = xcalloc((size_t)count, sizeof *published);
    char (*hashes)[17] = xcalloc((size_t)count, sizeof *hashes);
    char (*names)[256] = xcalloc((size_t)count, sizeof *names);
    for (int i = 0; i < count; i++) {
        const refactor_edit *edit = &plan->edits[i];
        const catalog_asset *asset = &view->assets[view->catalog[edit->asset]];
        hash_text(view->hashes[edit->asset], hashes[i]);
        snprintf(
            names[i],
            sizeof names[i],
            "%s%s",
            edit->name ? edit->name : "",
            asset->asset.family->extension
        );
        const char *filename = edit->name ? names[i] : asset->asset.filename;
        changes[i] = (automation_asset_change){ .folder = asset->asset.family->folder,
                                                .filename = asset->asset.filename,
                                                .rename_to = edit->name ? names[i] : NULL,
                                                .text = edit->text,
                                                .expected_hash = hashes[i] };
        snprintf(
            published[i].path,
            sizeof published[i].path,
            "content/%s/%s",
            asset->asset.family->folder,
            filename
        );
        published[i].valid = asset->valid;
        published[i].deleted = !edit->text;
        total += styles_change(root, asset, edit, &styles[i], &changes[total]);
    }
    automation_asset_batch *batch = NULL;
    int status = automation_assets_publish(root, changes, total, &batch, error, size);
    /* The catalog is rebuilt from here on; the view is stale. */
    int invalid = status == CHIBI_AUTOMATION_OK && revalidate
                    ? refactor_invalid(root, published, count, result, value, error, size)
                    : 0;
    if (status == CHIBI_AUTOMATION_OK && invalid) {
        char why[256];
        snprintf(why, sizeof why, "%s", invalid < 0 ? error : "a changed asset no longer validates; every asset is restored");
        /* A failed rollback keeps its own error, which says where the
           originals are. */
        status = automation_assets_rollback(batch, error, size);
        if (status == CHIBI_AUTOMATION_OK) {
            status = CHIBI_AUTOMATION_VALIDATION_ERROR;
            set_error(error, size, why);
        }
    } else if (status == CHIBI_AUTOMATION_OK) {
        automation_assets_commit(batch);
    }
    for (int i = 0; i < count; i++) {
        free(styles[i].text);
    }
    free(styles);
    free(changes);
    free(published);
    free(hashes);
    free(names);
    return status;
}

/* rename_symbol and delete_symbol: plan on the catalog's files, list the
   plan, and publish it when the request brings the dry run's hashes. */
static int handle_refactor(const char *root, yyjson_val *request, bool rename, response *result, int *exit_code)
{
    yyjson_val *dry_value = yyjson_obj_get(request, "dry_run");
    yyjson_val *force_value = yyjson_obj_get(request, "force");
    const char *name = request_string(request, "new_name");
    bool dry = !dry_value || yyjson_get_bool(dry_value);
    bool force = yyjson_get_bool(force_value);
    char error[256] = { 0 };
    if ((dry_value && !yyjson_is_bool(dry_value)) ||
        (force_value && !yyjson_is_bool(force_value)) || (rename && !name)) {
        response_error(
            result,
            exit_code,
            CHIBI_AUTOMATION_INVALID_REQUEST,
            rename ? "rename_symbol requires new_name and a boolean dry_run"
                   : "delete_symbol takes boolean dry_run and force"
        );
        return -1;
    }
    refactor_view view;
    reference_symbol symbol = { 0 };
    int status = refactor_view_load(root, &view, error, sizeof error);
    status = status == CHIBI_AUTOMATION_OK
               ? automation_select_symbol(&view.nav, request, &symbol, error, sizeof error)
               : status;
    game_prototype_registry registry = { .components = GAME_REGISTRY_COMPONENTS,
                                         .component_count = GAME_REGISTRY_COMPONENT_COUNT };
    refactor_project project = { .index = view.nav.index,
                                 .texts = (const char *const *)view.texts,
                                 .registry = &registry };
    refactor_plan plan = { 0 };
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_bool(result->doc, value, "dry_run", dry);
    yyjson_mut_obj_add_bool(result->doc, value, "published", false);
    if (status == CHIBI_AUTOMATION_OK && !rename && !force) {
        const reference **blockers = NULL;
        refactor_blockers(view.nav.index, symbol, &blockers);
        add_refactor_uses(result->doc, value, "references", &view, blockers);
        afree(blockers);
    }
    if (status == CHIBI_AUTOMATION_OK &&
        (rename ? refactor_rename(&project, symbol, name, &plan, error, sizeof error)
                : refactor_delete(&project, symbol, force, &plan, error, sizeof error))) {
        status = CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    if (status == CHIBI_AUTOMATION_OK) {
        add_refactor_plan(result->doc, value, &view, &plan);
    }
    if (status == CHIBI_AUTOMATION_OK && !dry) {
        status = refactor_expected(request, &view, &plan, error, sizeof error);
    }
    if (status == CHIBI_AUTOMATION_OK && !dry) {
        status = refactor_publish(root, &view, &plan, rename || !force, result, value, error, sizeof error);
        yyjson_mut_obj_remove_key(value, "published");
        yyjson_mut_obj_add_bool(result->doc, value, "published", status == CHIBI_AUTOMATION_OK);
    }
    refactor_plan_free(&plan);
    refactor_view_free(&view);
    if (status != CHIBI_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    return 0;
}

/* ---------- UI language service (workstream B) ---------- */

/* Loads a ui asset; the loaded text is owned by the caller. */
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
        char content[1024];
        snprintf(content, sizeof content, "%s/content", root);
        status = automation_ui_complete(content, text, rcss, complete, result->doc, value, error, sizeof error);
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
    return !strcmp(operation, "edit_graph") || !strcmp(operation, "edit_prototype") ||
           !strcmp(operation, "edit_scene") || !strcmp(operation, "optimize_graph") ||
           !strcmp(operation, "execution_status") ||
           !strcmp(operation, "profile_scenario") || !strcmp(operation, "status") ||
           !strcmp(operation, "list_assets") || !strcmp(operation, "read_asset") ||
           !strcmp(operation, "validate_asset") ||
           !strcmp(operation, "preview_field") ||
           !strcmp(operation, "search_nodes") || !strcmp(operation, "catalog") ||
           !strcmp(operation, "search_assets") ||
           !strcmp(operation, "asset_dependencies") ||
           !strcmp(operation, "find_references") ||
           !strcmp(operation, "call_hierarchy") ||
           !strcmp(operation, "inspect_graph") ||
           !strcmp(operation, "inspect_system") ||
           !strcmp(operation, "run_scenario") ||
           !strcmp(operation, "runtime_snapshot") ||
           !strcmp(operation, "record_scenario") ||
           !strcmp(operation, "replay_scenario") ||
           !strcmp(operation, "rename_symbol") ||
           !strcmp(operation, "delete_symbol") || !strcmp(operation, "inspect_ui");
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
    char *root = NULL;
    if (operation_requires_root(operation)) {
        char error[256] = { 0 };
        root = root_path(request_string(root_value, "project_root"), default_root, error, sizeof error);
        if (!root) {
            response_error(&result, exit_code, CHIBI_AUTOMATION_IO_ERROR, error);
            yyjson_doc_free(document);
            return response_finish(&result);
        }
    }
    if (!strcmp(operation, "edit_graph")) {
        handle_edit_graph(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "edit_prototype")) {
        handle_edit_prototype(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "edit_scene")) {
        handle_edit_scene(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "optimize_graph")) {
        handle_optimize_graph(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "execution_status")) {
        handle_execution_status(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "profile_scenario")) {
        handle_profile_scenario(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "search_nodes")) {
        handle_search_nodes(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "describe_components")) {
        handle_describe_components(root_value, &result, exit_code);
    } else if (!strcmp(operation, "preview_field")) {
        handle_preview_field(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "preview_shader")) {
        response_error(&result, exit_code, CHIBI_AUTOMATION_INVALID_REQUEST, "preview_shader requires chibi_ctl --render");
    } else if (!strcmp(operation, "describe_contract")) {
        handle_describe_contract(&result);
    } else if (!strcmp(operation, "status")) {
        handle_status(root, &result, exit_code);
    } else if (!strcmp(operation, "list_assets")) {
        handle_list_assets(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "read_asset")) {
        handle_read_asset(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "validate_asset")) {
        handle_validate_asset(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "catalog")) {
        handle_catalog(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "search_assets")) {
        handle_search_assets(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "asset_dependencies")) {
        handle_asset_dependencies(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "find_references") || !strcmp(operation, "call_hierarchy")) {
        handle_navigation(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "inspect_graph")) {
        handle_inspect_graph(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "inspect_system")) {
        handle_inspect_system(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "run_scenario")) {
        handle_run_scenario(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "runtime_snapshot")) {
        handle_runtime_snapshot(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "record_scenario")) {
        handle_record_scenario(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "replay_scenario")) {
        handle_replay_scenario(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "read_recording")) {
        handle_read_recording(root_value, &result, exit_code);
    } else if (!strcmp(operation, "rename_symbol") || !strcmp(operation, "delete_symbol")) {
        handle_refactor(root, root_value, !strcmp(operation, "rename_symbol"), &result, exit_code);
    } else if (!strcmp(operation, "inspect_ui")) {
        handle_inspect_ui(root, root_value, &result, exit_code);
    } else {
        handle_describe_registry(root_value, &result, exit_code);
    }
    free(root);
    yyjson_doc_free(document);
    return response_finish(&result);
}
