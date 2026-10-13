#include "assets/asset_schema.h"

#include "assets/new_asset.h"
#include "assets/project.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) ((int)(sizeof(a) / sizeof *(a)))
#define FIELD(n, t, d) { .name = n, .type = t, .description = d }
#define REQUIRED(n, t, d)                                                      \
    { .name = n, .type = t, .description = d, .required = true }
#define STRINGS(n, r, d)                                                       \
    { .name = n,                                                               \
      .type = ASSET_SCHEMA_ARRAY,                                              \
      .element_type = ASSET_SCHEMA_STRING,                                     \
      .reference_kind = r,                                                     \
      .description = d }
#define OBJECT(n, s, d)                                                        \
    { .name = n, .type = ASSET_SCHEMA_OBJECT, .object = s, .description = d }
#define CHILDREN                                                               \
    { .name = "children",                                                      \
      .type = ASSET_SCHEMA_ARRAY,                                              \
      .element_type = ASSET_SCHEMA_OBJECT,                                     \
      .element_object = &ENTITY,                                               \
      .description = "Child entities, with transforms relative to their "      \
                     "parent." }

static const asset_schema ENTITY;
static const asset_schema COMPONENTS = {
    .kind = "components",
    .description = "Map of reflected component names to sparse field "
                   "overrides. Native and project component reflection defines "
                   "fields; null removes a sparse override.",
};
static const asset_schema_field PHYSICS_FIELDS[] = {
    FIELD("workers", ASSET_SCHEMA_INTEGER, "Worker count, 0 through 32."),
    FIELD("substeps", ASSET_SCHEMA_INTEGER, "Substeps, 0 through 32; canonical physics validation checks valid combinations."),
    FIELD("sleep", ASSET_SCHEMA_BOOL, "Allow sleeping bodies."),
    FIELD("continuous", ASSET_SCHEMA_BOOL, "Enable continuous collision detection."),
    FIELD("force_drive", ASSET_SCHEMA_BOOL, "Enable force-driven movement."),
    FIELD("contact_hertz", ASSET_SCHEMA_NUMBER, "Contact frequency."),
    FIELD("contact_damping", ASSET_SCHEMA_NUMBER, "Contact damping ratio."),
};
static const asset_schema PHYSICS = { .kind = "physics",
                                      .fields = PHYSICS_FIELDS,
                                      .field_count = COUNT(PHYSICS_FIELDS),
                                      .complete = true };
static const asset_schema_field ENTITY_FIELDS[] = {
    REQUIRED("name", ASSET_SCHEMA_STRING, "Non-empty name, unique among siblings; '.' separates paths and is forbidden in names."),
    { .name = "prototype",
      .type = ASSET_SCHEMA_STRING,
      .required = true,
      .reference_kind = "prototype",
      .description = "Declared prototype name, independent of its file name." },
    OBJECT("components", &COMPONENTS, "Override the prototype's component fields."),
    OBJECT("retained_components", &COMPONENTS, "Retained overrides for detached component state."),
    STRINGS("behaviors", "behavior", "Replaces inherited behaviors; an empty array removes all; omission inherits."),
    CHILDREN,
};
static const asset_schema ENTITY = { .kind = "entity",
                                     .fields = ENTITY_FIELDS,
                                     .field_count = COUNT(ENTITY_FIELDS),
                                     .complete = true };
static const char *const PRESENTATION[] = { "2d", "3d" };
static const asset_schema_field SCENE_FIELDS[] = {
    REQUIRED("version", ASSET_SCHEMA_INTEGER, "Scene format version; currently 1."),
    REQUIRED("name", ASSET_SCHEMA_STRING, "Scene name."),
    { .name = "presentation",
      .type = ASSET_SCHEMA_STRING,
      .choices = PRESENTATION,
      .choice_count = COUNT(PRESENTATION),
      .description = "Omission uses 2d; authored 3D scenes select 3d." },
    OBJECT("physics", &PHYSICS, "Overrides project-independent physics defaults."),
    { .name = "ui",
      .type = ASSET_SCHEMA_STRING,
      .reference_kind = "ui",
      .description = "UI document name; omission uses project UI." },
    { .name = "entities",
      .type = ASSET_SCHEMA_ARRAY,
      .element_type = ASSET_SCHEMA_OBJECT,
      .element_object = &ENTITY,
      .required = true,
      .description = "Root entities in authored order." },
};
static const asset_schema_field PROTOTYPE_FIELDS[] = {
    REQUIRED("name", ASSET_SCHEMA_STRING, "Declared prototype identity used by references, independent of its file name."),
    FIELD("identity", ASSET_SCHEMA_STRING, "Stable identity; omission derives it from the declared name."),
    STRINGS("includes", "prototype", "Included prototypes, applied before local overrides; include cycles are rejected."),
    OBJECT("components", &COMPONENTS, "Local component field overrides."),
    OBJECT("detached_components", &COMPONENTS, "Detached inherited component data."),
    STRINGS("behaviors", "behavior", "Authored behaviors."),
    CHILDREN,
};
static const asset_schema_field APPLICATION_FIELDS[] = {
    FIELD("title", ASSET_SCHEMA_STRING, "Window title; omission uses the project name."),
    FIELD("width", ASSET_SCHEMA_INTEGER, "Window width, 1 through 16384; omission selects the default."),
    FIELD("height", ASSET_SCHEMA_INTEGER, "Window height, 1 through 16384; omission selects the default."),
    FIELD("fullscreen", ASSET_SCHEMA_BOOL, "Start fullscreen."),
    { .name = "icon",
      .type = ASSET_SCHEMA_STRING,
      .reference_kind = "resource",
      .description = "Project-relative PNG resource path." },
};
static const asset_schema APPLICATION = { .kind = "application",
                                          .fields = APPLICATION_FIELDS,
                                          .field_count = COUNT(APPLICATION_FIELDS),
                                          .complete = true };
static const asset_schema_field PROJECT_FIELDS[] = {
    REQUIRED("version", ASSET_SCHEMA_INTEGER, "Current project format; older projects require explicit migration."),
    FIELD("name", ASSET_SCHEMA_STRING, "Project name; omission uses the folder name."),
    FIELD("content", ASSET_SCHEMA_STRING, "Project-relative content folder; omission uses content."),
    { .name = "start_scene",
      .type = ASSET_SCHEMA_STRING,
      .reference_kind = "scene-path",
      .description = "Scene path relative to the selected content folder." },
    { .name = "ui",
      .type = ASSET_SCHEMA_STRING,
      .reference_kind = "ui",
      .description = "Default UI document name." },
    OBJECT("application", &APPLICATION, "Player window settings."),
};
/* Graph serialization has domain-specific nested fields. This envelope is
 * useful for discovery without claiming a complete graph grammar. */
static const asset_schema_field GRAPH_FIELDS[] = {
    REQUIRED("version", ASSET_SCHEMA_INTEGER, "Graph asset schema version; currently 3."),
    REQUIRED("name", ASSET_SCHEMA_STRING, "Declared graph asset name."),
    REQUIRED("id_counter", ASSET_SCHEMA_INTEGER, "Positive next stable graph identity."),
    REQUIRED("graphs", ASSET_SCHEMA_ARRAY, "Graph records; at least one, validated by the owning graph loader."),
    FIELD("domain", ASSET_SCHEMA_STRING, "Graph execution domain selected by the asset family."),
    FIELD("description", ASSET_SCHEMA_STRING, "Human-readable asset purpose."),
    FIELD("bounds_x", ASSET_SCHEMA_NUMBER, "Shape or model horizontal bounds."),
    FIELD("bounds_y", ASSET_SCHEMA_NUMBER, "Shape or model vertical bounds."),
    FIELD("variables", ASSET_SCHEMA_ARRAY, "Graph variable declarations."),
    FIELD("component_parameters", ASSET_SCHEMA_ARRAY, "Component parameter declarations."),
    FIELD("owner_type", ASSET_SCHEMA_STRING, "Owner component type."),
    FIELD("reference", ASSET_SCHEMA_OBJECT, "Model reference image and alignment."),
};
#define GRAPH(k, f)                                                            \
    { .kind = k,                                                               \
      .folder = f,                                                             \
      .extension = ".kdl",                                                     \
      .description = "Declarative graph asset; graph loader and reflected "    \
                     "nodes define the remaining nested grammar.",             \
      .fields = GRAPH_FIELDS,                                                  \
      .field_count = COUNT(GRAPH_FIELDS) }
#define CODE(k, f)                                                             \
    { .kind = k,                                                               \
      .folder = f,                                                             \
      .extension = ".chibi",                                                   \
      .description = "Executable .chibi source; canonical language analysis, " \
                     "completion and diagnostics own its grammar.",            \
      .executable = true }
static const asset_schema SCHEMAS[] = {
    { .kind = "scene",
      .folder = "scenes",
      .extension = ".kdl",
      .description = "Scene composition and presentation.",
      .fields = SCENE_FIELDS,
      .field_count = COUNT(SCENE_FIELDS),
      .complete = true },
    { .kind = "prototype",
      .folder = "prototypes",
      .extension = ".kdl",
      .description = "Reusable entity composition.",
      .fields = PROTOTYPE_FIELDS,
      .field_count = COUNT(PROTOTYPE_FIELDS),
      .complete = true },
    CODE("behavior", "behaviors"),
    GRAPH("event", "events"),
    CODE("system", "systems"),
    CODE("library", "libraries"),
    GRAPH("material", "materials"),
    GRAPH("shape", "shapes"),
    GRAPH("field", "fields"),
    GRAPH("shader-library", "shader-libraries"),
    GRAPH("model", "models"),
    { .kind = "project",
      .folder = "",
      .extension = ".kdl",
      .description = "project.kdl manifest; content and start scene paths are "
                     "relative to their documented roots.",
      .fields = PROJECT_FIELDS,
      .field_count = COUNT(PROJECT_FIELDS),
      .complete = true },
    CODE("component", "components"),
    { .kind = "ui", .folder = "ui", .extension = ".rml", .description = "RML UI markup; use the shared UI language service for validation and completion." },
    { .kind = "input", .folder = "", .extension = ".kdl", .description = "input.kdl action map; its node names define input event identities." },
};

int asset_schema_count()
{
    return COUNT(SCHEMAS);
}
const asset_schema *asset_schema_at(int index)
{
    assert(index >= 0 && index < COUNT(SCHEMAS));
    return &SCHEMAS[index];
}
const asset_schema *asset_schema_find(const char *kind)
{
    for (int i = 0; kind && i < COUNT(SCHEMAS); i++) {
        if (!strcmp(kind, SCHEMAS[i].kind)) {
            return &SCHEMAS[i];
        }
    }
    return NULL;
}
const asset_schema *asset_schema_for_path(const char *path)
{
    if (!path) {
        return NULL;
    }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!strcmp(base, "project.kdl")) {
        return asset_schema_find("project");
    }
    if (!strcmp(base, "input.kdl")) {
        return asset_schema_find("input");
    }
    const asset_schema *result = NULL;
    const char *nearest = NULL;
    for (int i = 0; i < COUNT(SCHEMAS); i++) {
        const asset_schema *schema = &SCHEMAS[i];
        size_t n = strlen(schema->folder), ext = strlen(schema->extension);
        size_t length = strlen(base);
        if (!n || length < ext || strcmp(base + length - ext, schema->extension)) {
            continue;
        }
        for (const char *p = path; (p = strstr(p, schema->folder)); p++) {
            if ((p == path || p[-1] == '/') && p[n] == '/' &&
                (!nearest || p > nearest)) {
                result = schema;
                nearest = p;
            }
        }
    }
    return result;
}
const asset_schema_field *asset_schema_field_find(const asset_schema *schema, const char *name)
{
    for (int i = 0; schema && name && i < schema->field_count; i++) {
        if (!strcmp(schema->fields[i].name, name)) {
            return &schema->fields[i];
        }
    }
    return NULL;
}
const char *asset_schema_value_name(asset_schema_value_kind type)
{
    static const char *const NAMES[] = { "any",     "string", "boolean",
                                         "integer", "number", "object",
                                         "array" };
    assert(type >= ASSET_SCHEMA_ANY && type <= ASSET_SCHEMA_ARRAY);
    return NAMES[type];
}
static bool matches(const av_value *value, asset_schema_value_kind type)
{
    switch (type) {
        case ASSET_SCHEMA_ANY: return true;
        case ASSET_SCHEMA_STRING: return av_is_str(value);
        case ASSET_SCHEMA_BOOL: return av_is_bool(value);
        case ASSET_SCHEMA_INTEGER: return av_is_int(value);
        case ASSET_SCHEMA_NUMBER: return av_is_num(value);
        case ASSET_SCHEMA_OBJECT: return av_is_obj(value);
        case ASSET_SCHEMA_ARRAY: return av_is_arr(value);
    }
    return false;
}
static void problem(
    asset_schema_diagnostic **out,
    const av_value *value,
    const char *text,
    const char *code,
    const char *path,
    const char *expected
)
{
    av_source source = av_get_source(value);
    asset_schema_diagnostic item = {
        .start = source.start == SIZE_MAX ? 0 : (int)source.start,
        .end = source.end == SIZE_MAX ? 0 : (int)source.end,
        .line = 1,
        .column = 1
    };
    for (int i = 0; text[i] && i < item.start; i++) {
        if (text[i] == '\n') {
            item.line++;
            item.column = 1;
        } else {
            item.column++;
        }
    }
    snprintf(item.code, sizeof item.code, "%s", code);
    snprintf(item.path, sizeof item.path, "%s", path);
    snprintf(item.expected, sizeof item.expected, "%s", expected);
    snprintf(item.message, sizeof item.message, "%s: expected %s", path[0] ? path : "document", expected);
    apush(*out, item);
}
static void validate_object(
    const asset_schema *schema,
    const av_value *value,
    const char *path,
    const char *text,
    int depth,
    asset_schema_diagnostic **out
)
{
    if (depth > 64) {
        problem(out, value, text, "DEPTH_LIMIT", path, "at most 64 nested objects");
        return;
    }
    if (!av_is_obj(value)) {
        problem(out, value, text, "INVALID_TYPE", path, "object");
        return;
    }
    for (int i = 0; i < schema->field_count; i++) {
        const asset_schema_field *field = &schema->fields[i];
        if (field->required && !av_obj_get(value, field->name)) {
            char child[384];
            snprintf(
                child,
                sizeof child,
                "%s%s%s",
                path,
                *path ? "." : "",
                field->name
            );
            problem(
                out,
                value,
                text,
                "MISSING_FIELD",
                child,
                asset_schema_value_name(field->type)
            );
        }
    }
    for (size_t i = 0; i < av_obj_size(value); i++) {
        const char *name = av_get_str(av_obj_key_at(value, i));
        const av_value *child = av_obj_value_at(value, i);
        const asset_schema_field *field = asset_schema_field_find(schema, name);
        char child_path[384];
        snprintf(child_path, sizeof child_path, "%s%s%s", path, *path ? "." : "", name);
        if (!field) {
            if (schema->complete) {
                char expected[256] = "one of: ";
                for (int j = 0; j < schema->field_count; j++) {
                    size_t used = strlen(expected);
                    snprintf(
                        expected + used,
                        sizeof expected - used,
                        "%s%s",
                        j ? ", " : "",
                        schema->fields[j].name
                    );
                }
                problem(out, child, text, "UNKNOWN_FIELD", child_path, expected);
            }
            continue;
        }
        if (!matches(child, field->type)) {
            problem(
                out,
                child,
                text,
                "INVALID_TYPE",
                child_path,
                asset_schema_value_name(field->type)
            );
            continue;
        }
        if (field->choice_count && av_is_str(child)) {
            bool found = false;
            char choices[256] = "";
            for (int j = 0; j < field->choice_count; j++) {
                found |= !strcmp(av_get_str(child), field->choices[j]);
                size_t used = strlen(choices);
                snprintf(
                    choices + used,
                    sizeof choices - used,
                    "%s%s",
                    j ? ", " : "",
                    field->choices[j]
                );
            }
            if (!found) {
                problem(out, child, text, "INVALID_CHOICE", child_path, choices);
            }
        }
        if (field->object) {
            validate_object(field->object, child, child_path, text, depth + 1, out);
        }
        if (field->type != ASSET_SCHEMA_ARRAY) {
            continue;
        }
        for (size_t j = 0; j < av_arr_size(child); j++) {
            const av_value *element = av_arr_get(child, j);
            char element_path[384];
            snprintf(element_path, sizeof element_path, "%.350s[%zu]", child_path, j);
            if (!matches(element, field->element_type)) {
                problem(
                    out,
                    element,
                    text,
                    "INVALID_TYPE",
                    element_path,
                    asset_schema_value_name(field->element_type)
                );
                continue;
            }
            if (field->element_object) {
                validate_object(field->element_object, element, element_path, text, depth + 1, out);
            }
        }
    }
}
asset_schema_diagnostic *asset_schema_validate(
    const asset_schema *schema,
    const av_value *value,
    const char *text
)
{
    asset_schema_diagnostic *out = NULL;
    if (schema && !schema->executable && schema->fields) {
        validate_object(schema, value, "", text, 0, &out);
    }
    return out;
}
static av_value *describe(const asset_schema *schema, av_doc *doc, const asset_schema **ancestors, int depth)
{
    av_value *value = av_obj(doc);
    av_obj_add_str(doc, value, "kind", schema->kind);
    av_obj_add_bool(doc, value, "complete", schema->complete);
    if (schema->description) {
        av_obj_add_str(doc, value, "description", schema->description);
    }
    for (int i = 0; i < depth; i++) {
        if (ancestors[i] == schema) {
            av_obj_add_bool(doc, value, "recursive", true);
            return value;
        }
    }
    assert(depth < 64);
    ancestors[depth] = schema;
    av_value *fields = av_obj_add_arr(doc, value, "fields");
    for (int i = 0; i < schema->field_count; i++) {
        const asset_schema_field *field = &schema->fields[i];
        av_value *item = av_obj(doc);
        av_arr_append(fields, item);
        av_obj_add_str(doc, item, "name", field->name);
        av_obj_add_str(doc, item, "type", asset_schema_value_name(field->type));
        av_obj_add_bool(doc, item, "required", field->required);
        if (field->description) {
            av_obj_add_str(doc, item, "description", field->description);
        }
        if (field->reference_kind) {
            av_obj_add_str(doc, item, "reference_kind", field->reference_kind);
        }
        if (field->object) {
            av_obj_add_val(doc, item, "object", describe(field->object, doc, ancestors, depth + 1));
        }
        if (field->type == ASSET_SCHEMA_ARRAY) {
            av_obj_add_str(
                doc,
                item,
                "element_type",
                asset_schema_value_name(field->element_type)
            );
            if (field->element_object) {
                av_obj_add_val(
                    doc,
                    item,
                    "element_object",
                    describe(field->element_object, doc, ancestors, depth + 1)
                );
            }
        }
        if (field->choice_count) {
            av_value *choices = av_obj_add_arr(doc, item, "choices");
            for (int j = 0; j < field->choice_count; j++) {
                av_arr_append(choices, av_str(doc, field->choices[j]));
            }
        }
    }
    return value;
}
av_value *asset_schema_describe(av_doc *doc, const asset_schema *schema, bool example)
{
    const asset_schema *ancestors[64];
    av_value *value = describe(schema, doc, ancestors, 0);
    av_obj_add_str(doc, value, "folder", schema->folder);
    av_obj_add_str(doc, value, "extension", schema->extension);
    av_obj_add_bool(doc, value, "executable", schema->executable);
    if (!strcmp(schema->kind, "project")) {
        av_obj_add_int(doc, value, "format_version", CHIBI_PROJECT_VERSION);
    }
    av_obj_add_str(doc, value, "validation_scope", "Structural fields only; canonical loaders and preparation own versions, references, values and execution constraints.");
    int index = -1;
    for (int i = 0; i < NEW_ASSET_KIND_COUNT; i++) {
        if (schema == &SCHEMAS[i]) {
            index = i;
        }
    }
    if (example && index >= 0) {
        char *text = new_asset_text((new_asset_kind)index, "Example");
        if (text) {
            av_obj_add_strcpy(doc, value, "example", text);
        }
        free(text);
    }
    return value;
}
