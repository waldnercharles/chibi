#include "authoring/workspace_runtime_definition.h"

#include "authoring/runtime_properties.h"
#include "execution/function.h"
#define P(N, K, T)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = K, .type_name = T }
#define I(N) P(N, PIN_I32, "int")
#define F(N) P(N, PIN_F32, "float")
#define B(N) P(N, PIN_BOOL, "bool")
#define S(N) P(N, PIN_STR, "const char *")
#define E(N) { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
static const native_parameter_definition PRESENTATION_IN[] = {
    E("Exec"),    I("Session"),   B("Unlit"),
    B("Fogless"), B("Colliders"), B("Cameras")
};
static const native_parameter_definition PRESENTATION_OUT[] = {
    E("Then"),      B("Ok"),      I("Unsupported"),
    B("Truncated"), B("Invalid"), S("Error")
};
static const native_parameter_definition CAMERA_IN[] = { E("Exec"),
                                                         I("Session"),
                                                         B("Apply") };
static const native_parameter_definition CAMERA_OUT[] = {
    E("Then"),        B("Ok"),       B("Found"),     F("X"),
    F("Y"),           F("Width"),    F("Height"),    F("FocalDistance"),
    F("Yaw"),         F("Pitch"),    F("Elevation"), F("Roll"),
    F("EyeX"),        F("EyeY"),     F("EyeZ"),      F("ForwardX"),
    F("ForwardY"),    F("ForwardZ"), F("UpX"),       F("UpY"),
    F("UpZ"),         F("Fov"),      F("Near"),      F("Far"),
    B("Perspective"), S("Error")
};
static const native_parameter_definition RAY_IN[] = { E("Exec"),
                                                      I("Session"),
                                                      F("X"),
                                                      F("Y") };
static const native_parameter_definition RAY_OUT[] = {
    E("Then"),       B("Ok"),         F("OriginX"),
    F("OriginY"),    F("OriginZ"),    F("DirectionX"),
    F("DirectionY"), F("DirectionZ"), S("Error")
};
static const native_parameter_definition PROJECT_IN[] = { E("Exec"),
                                                          I("Session"),
                                                          F("X"),
                                                          F("Y"),
                                                          F("Z") };
static const native_parameter_definition PROJECT_OUT[] = {
    E("Then"), B("Ok"), F("X"), F("Y"), F("Depth"), B("Visible"), S("Error")
};
static const native_parameter_definition EVIDENCE_BEGIN[] = { E("Exec"),
                                                              I("Session"),
                                                              S("Directory") };
static const native_parameter_definition EVIDENCE_JOB[] = { E("Exec"), I("Job") };
static const native_parameter_definition EVIDENCE_OUT[] = {
    E("Then"),  B("Ok"),        B("Pending"), I("Job"),
    S("State"), S("Directory"), S("Error")
};
static const native_parameter_definition VALUE_IN[] = { E("Exec"),
                                                        I("Session"),
                                                        I("Object"),
                                                        S("Component"),
                                                        S("Field") };
static const native_parameter_definition VALUE_OUT[] = { E("Then"),
                                                         B("Ok"),
                                                         S("Json"),
                                                         S("Error") };
static const native_parameter_definition NUMBER_IN[] = { E("Exec"),
                                                         I("Session"),
                                                         I("Object"),
                                                         S("Component"),
                                                         S("Field"),
                                                         B("Write"),
                                                         P("Value", PIN_F64, "double") };
static const native_parameter_definition NUMBER_OUT[] = { E("Then"),
                                                          B("Ok"),
                                                          P("Value", PIN_F64, "double"),
                                                          S("Error") };
static const native_parameter_definition PREPARE[] = { E("Exec"),
                                                       I("Document"),
                                                       I("Revision") };
static const native_parameter_definition EMPTY[] = { E("Exec") };
static const native_parameter_definition CONTROL[] = { E("Exec"),
                                                       I("Session"),
                                                       S("Operation") };
static const native_parameter_definition DRAW[] = { E("Exec"),
                                                    I("Session"),
                                                    I("Width"),
                                                    I("Height"),
                                                    F("Seconds") };
static const native_parameter_definition VIEW[] = {
    E("Exec"),
    I("Session"),
    F("X"),
    F("Y"),
    F("Width"),
    F("Height"),
    F("Yaw"),
    F("Pitch"),
    F("Elevation"),
    B("Perspective"),
    F("Grid"),
    { .name = "FocalDistance",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_F32,
      .type_name = "float",
      .has_default = true,
      .default_value = { .type = PIN_F32, .f = 0 } }
};
static const native_parameter_definition PICK[] = { E("Exec"),
                                                    I("Session"),
                                                    F("X"),
                                                    F("Y") };
static const native_parameter_definition PICK_OUT[] = { E("Then"),
                                                        B("Ok"),
                                                        I("Object"),
                                                        S("Error"),
                                                        S("Subject") };
static const native_parameter_definition SKIP[] = { E("Exec"),
                                                    I("Session"),
                                                    { .name = "Objects",
                                                      .kind = NATIVE_PARAMETER_DATA,
                                                      .storage = PIN_I32,
                                                      .type_name = "int",
                                                      .container = PIN_CONTAINER_ARRAY },
                                                    B("Picking") };
static const native_parameter_definition SESSION[] = { E("Exec"), I("Session") };
static const native_parameter_definition BOUNDS_IN[] = {
    E("Exec"),
    I("Session"),
    { .name = "Objects",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .container = PIN_CONTAINER_ARRAY }
};
static const native_parameter_definition BOUNDS_OUT[] = {
    E("Then"), B("Ok"),   F("MinX"), F("MinY"), F("MinZ"),
    F("MaxX"), F("MaxY"), F("MaxZ"), S("Error")
};
static const native_parameter_definition OBJECTS_OUT[] = {
    E("Then"),
    B("Ok"),
    { .name = "Names",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Parents",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .container = PIN_CONTAINER_ARRAY },
    S("Error"),
    B("Perspective")
};
static const native_parameter_definition OWNER_IN[] = { E("Exec"),
                                                        I("Session"),
                                                        P("Owner", PIN_U64, "uint64") };
static const native_parameter_definition OWNER_OUT[] = { E("Then"),
                                                         B("Ok"),
                                                         S("Subject"),
                                                         S("Error") };
static const native_parameter_definition COMPONENT_IN[] = { E("Exec"),
                                                            I("Session"),
                                                            I("Object"),
                                                            S("Component") };
static const native_parameter_definition COMPONENT_OUT[] = { E("Then"),
                                                             B("Ok"),
                                                             B("Present"),
                                                             S("Error") };
static const native_parameter_definition SUBJECT_IN[] = { E("Exec"),
                                                          I("Session"),
                                                          S("Subject"),
                                                          I("First"),
                                                          I("Count") };
static const native_parameter_definition SUBJECT_OUT[] = {
    E("Then"),
    B("Ok"),
    S("Name"),
    { .name = "Rows",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "RuntimeProperty",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Choices",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "RuntimePropertyChoice",
      .container = PIN_CONTAINER_ARRAY },
    I("Total"),
    S("Error")
};
static const native_parameter_definition SUBJECT_WRITE[] = {
    E("Exec"), I("Session"), S("Subject"), S("Component"), S("Field"), S("Json")
};
static const native_parameter_definition INPUT[] = { E("Exec"),
                                                     I("Session"),
                                                     S("Action"),
                                                     F("Value"),
                                                     B("Pressed") };
static const native_parameter_definition VALID[] = { E("Then"), B("Ok"), S("Error") };
static const native_parameter_definition PREPARE_SCENE_SOURCE[] = {
    E("Exec"),
    I("BaseDocument"),
    I("BaseRevision"),
    S("Path"),
    S("Source")
};
static const native_parameter_definition SUBJECT_ORIGIN_IN[] = { E("Exec"),
                                                                 I("Session"),
                                                                 S("Subject") };
static const native_parameter_definition SUBJECT_ORIGIN_OUT[] = {
    E("Then"),
    B("Ok"),
    I("Object"),
    S("Prototype"),
    P("Creation", PIN_U64, "uint64"),
    B("Spawned"),
    B("Authored"),
    B("Known"),
    S("Path"),
    S("SourcePath"),
    S("Error")
};
static const native_parameter_definition PREPARE_SOURCES[] = {
    E("Exec"),
    I("BaseDocument"),
    I("BaseRevision"),
    S("ScenePath"),
    { .name = "Paths",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Sources",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_ARRAY }
};

static const native_parameter_definition MODEL_SNAPSHOT_IN[] = { E("Exec"),
                                                                 I("Session"),
                                                                 I("Object"),
                                                                 I("Part") };
static const native_parameter_definition MODEL_SNAPSHOT_OUT[] = {
    E("Then"), B("Ok"),   B("Empty"), F("MinX"), F("MinY"),
    F("MinZ"), F("MaxX"), F("MaxY"),  F("MaxZ"), S("Error")
};
static const native_parameter_definition MODEL_INSPECTION_IN[] = {
    E("Exec"),    I("Session"), I("Object"),  I("Part"),  B("Section"),
    F("NormalX"), F("NormalY"), F("NormalZ"), F("Offset")
};
static const native_parameter_definition MODEL_PARAMETER_IN[] = {
    E("Exec"),  I("Session"), I("Object"),
    I("Slot"),  B("Write"),   P("Node", PIN_U64, "uint64"),
    S("Asset"), S("Scope"),   I("Width"),
    F("X"),     F("Y"),       F("Z"),
    F("W")
};
static const native_parameter_definition MODEL_PARAMETER_OUT[] = { E("Then"),
                                                                   B("Ok"),
                                                                   B("Found"),
                                                                   S("Name"),
                                                                   S("Asset"),
                                                                   S("Scope"),
                                                                   P("Node", PIN_U64, "uint64"),
                                                                   S("Type"),
                                                                   B("Hidden"),
                                                                   I("Width"),
                                                                   F("X"),
                                                                   F("Y"),
                                                                   F("Z"),
                                                                   F("W"),
                                                                   F("MinX"),
                                                                   F("MinY"),
                                                                   F("MinZ"),
                                                                   F("MinW"),
                                                                   F("MaxX"),
                                                                   F("MaxY"),
                                                                   F("MaxZ"),
                                                                   F("MaxW"),
                                                                   S("Error") };
static const native_parameter_definition TRACE_OUT[] = { E("Then"),
                                                         B("Ok"),
                                                         S("Json"),
                                                         S("Error") };
static const native_parameter_definition MODEL_REPLAY_IN[] = { E("Exec"),
                                                               I("Session"),
                                                               S("Json") };
static const native_parameter_definition OBJECT_SUBJECT_IN[] = { E("Exec"),
                                                                 I("Session"),
                                                                 I("Object") };
static const native_parameter_definition RESULT[] = {
    E("Then"),    B("Ok"),       B("Preparing"), B("Ready"), B("Playing"),
    I("Session"), I("Document"), I("Revision"),  I("Steps"), F("Time"),
    S("Image"),   S("Error"),    S("Status"),    S("Log")
};
#define D(N, SRC, IN, OUT)                                                     \
    { .canonical_name = N,                                                     \
      .source_name = SRC,                                                      \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = OUT,                                                          \
      .output_count = NATIVE_COUNT(OUT),                                       \
      .completion_output = 0 }
const native_capability_definition WORKSPACE_RUNTIME_DEFINITIONS[WORKSPACE_RUNTIME_OPERATION_COUNT] = {
    D("Workspace Runtime Presentation",
      "workspace_runtime_presentation",
      PRESENTATION_IN,
      PRESENTATION_OUT),
    D("Workspace Runtime Camera", "workspace_runtime_camera", CAMERA_IN, CAMERA_OUT),
    D("Workspace Runtime Ray", "workspace_runtime_ray", RAY_IN, RAY_OUT),
    D("Workspace Runtime Project", "workspace_runtime_project", PROJECT_IN, PROJECT_OUT),
    D("Capture Workspace Runtime Evidence",
      "workspace_runtime_evidence_begin",
      EVIDENCE_BEGIN,
      EVIDENCE_OUT),
    D("Poll Workspace Runtime Evidence", "workspace_runtime_evidence_poll", EVIDENCE_JOB, EVIDENCE_OUT),
    D("Cancel Workspace Runtime Evidence",
      "workspace_runtime_evidence_cancel",
      EVIDENCE_JOB,
      EVIDENCE_OUT),
    D("Workspace Runtime Value", "workspace_runtime_value", VALUE_IN, VALUE_OUT),
    D("Workspace Runtime Number", "workspace_runtime_number", NUMBER_IN, NUMBER_OUT),
    D("Prepare Workspace Runtime", "workspace_runtime_prepare", PREPARE, RESULT),
    D("Prepare Workspace Runtime Scene Source",
      "workspace_runtime_prepare_document_scene_source",
      PREPARE_SCENE_SOURCE,
      RESULT),
    D("Inspect Workspace Runtime Subject Origin",
      "workspace_runtime_subject_origin",
      SUBJECT_ORIGIN_IN,
      SUBJECT_ORIGIN_OUT),
    D("Prepare Workspace Runtime Sources",
      "workspace_runtime_prepare_document_sources",
      PREPARE_SOURCES,
      RESULT),
    D("Resolve Workspace Runtime Object Subject",
      "workspace_runtime_object_subject",
      OBJECT_SUBJECT_IN,
      OWNER_OUT),
    D("Inspect Runtime Trace", "workspace_runtime_trace", SESSION, TRACE_OUT),
    D("Replay Runtime Model Action", "workspace_runtime_model_replay", MODEL_REPLAY_IN, VALID),
    D("Runtime Model Snapshot", "workspace_runtime_model_snapshot", MODEL_SNAPSHOT_IN, MODEL_SNAPSHOT_OUT),
    D("Runtime Model Inspection", "workspace_runtime_model_inspection", MODEL_INSPECTION_IN, VALID),
    D("Runtime Model Parameter",
      "workspace_runtime_model_parameter",
      MODEL_PARAMETER_IN,
      MODEL_PARAMETER_OUT),
    D("Poll Workspace Runtime", "workspace_runtime_poll", EMPTY, RESULT),
    D("Cancel Workspace Runtime", "workspace_runtime_cancel", EMPTY, RESULT),
    D("Control Workspace Runtime", "workspace_runtime_control", CONTROL, RESULT),
    D("Draw Workspace Runtime", "workspace_runtime_draw", DRAW, RESULT),
    D("View Workspace Runtime", "workspace_runtime_view", VIEW, VALID),
    D("Pick Workspace Runtime", "workspace_runtime_pick", PICK, PICK_OUT),
    D("Skip Workspace Runtime", "workspace_runtime_skip", SKIP, VALID),
    D("Bounds Workspace Runtime", "workspace_runtime_bounds", BOUNDS_IN, BOUNDS_OUT),
    D("Inspect Workspace Runtime Objects", "workspace_runtime_objects", SESSION, OBJECTS_OUT),
    D("Inspect Workspace Runtime Subject",
      "workspace_runtime_subject_properties",
      SUBJECT_IN,
      SUBJECT_OUT),
    D("Write Workspace Runtime Subject", "workspace_runtime_subject_write", SUBJECT_WRITE, VALID),
    D("Query Workspace Runtime Component", "workspace_runtime_component", COMPONENT_IN, COMPONENT_OUT),
    D("Resolve Workspace Runtime Subject", "workspace_runtime_subject", OWNER_IN, OWNER_OUT),
    D("Input Workspace Runtime", "workspace_runtime_input", INPUT, RESULT)
};
static const char *requires_host(const behavior_function_context *, void *, const pin_value *, pin_value *)
{
    return "Workspace runtime requires a prepared-player authoring host";
}
static const node_host_call CALL = {
    .service = "workspace_runtime",
    .missing_service = "Workspace runtime requires an authoring host",
    .context = requires_host
};
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define C(N)                                                                   \
    {                                                                          \
        .canonical_name = N, .implementation = {                               \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    C("Workspace Runtime Presentation"),
    C("Workspace Runtime Camera"),
    C("Workspace Runtime Ray"),
    C("Workspace Runtime Project"),
    C("Capture Workspace Runtime Evidence"),
    C("Poll Workspace Runtime Evidence"),
    C("Cancel Workspace Runtime Evidence"),
    C("Workspace Runtime Value"),
    C("Workspace Runtime Number"),
    C("Prepare Workspace Runtime"),
    C("Prepare Workspace Runtime Scene Source"),
    C("Inspect Workspace Runtime Subject Origin"),
    C("Prepare Workspace Runtime Sources"),
    C("Resolve Workspace Runtime Object Subject"),
    C("Inspect Runtime Trace"),
    C("Replay Runtime Model Action"),
    C("Runtime Model Snapshot"),
    C("Runtime Model Inspection"),
    C("Runtime Model Parameter"),
    C("Poll Workspace Runtime"),
    C("Cancel Workspace Runtime"),
    C("Control Workspace Runtime"),
    C("Draw Workspace Runtime"),
    C("View Workspace Runtime"),
    C("Pick Workspace Runtime"),
    C("Skip Workspace Runtime"),
    C("Bounds Workspace Runtime"),
    C("Input Workspace Runtime"),
    C("Inspect Workspace Runtime Objects"),
    C("Inspect Workspace Runtime Subject"),
    C("Write Workspace Runtime Subject"),
    C("Query Workspace Runtime Component"),
    C("Resolve Workspace Runtime Subject")
};
static const type_desc *const TYPES[] = { &RUNTIME_PROPERTY_LAYOUT,
                                          &RUNTIME_PROPERTY_CHOICE_LAYOUT };
const native_definition_pack WORKSPACE_RUNTIME_DECLARATION_PACK = {
    .definitions = WORKSPACE_RUNTIME_DEFINITIONS,
    .definition_count = WORKSPACE_RUNTIME_OPERATION_COUNT,
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
    .layouts = TYPES,
    .layout_count = NATIVE_COUNT(TYPES)
};
