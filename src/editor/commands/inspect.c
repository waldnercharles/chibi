/* Inspection for typed command clients: the catalog with optional reflected
 * node and component types, and object lookup by name. */
#include "editor/commands/internal.h"
#include "graph/model/registry.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RESOLVE_LIMIT 6
#define RESOLVE_WORDS 32

static bool requested(yyjson_val *kinds, const char *kind)
{
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(kinds, i, count, value)
    {
        if (yyjson_equals_str(value, kind)) {
            return true;
        }
    }
    return false;
}

/* Case-insensitive substring match; an empty query matches everything. */
static bool matches(const char *name, const char *query)
{
    size_t length = strlen(query);
    for (const char *at = name; *at; at++) {
        if (!strncasecmp(at, query, length)) {
            return true;
        }
    }
    return !length;
}

static void pins(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, const pin_spec *specs, int count)
{
    yyjson_mut_val *values = yyjson_mut_obj_add_arr(doc, object, key);
    for (int i = 0; i < count; i++) {
        const pin_spec *pin = &specs[i];
        yyjson_mut_val *field = yyjson_mut_arr_add_obj(doc, values);
        yyjson_mut_obj_add_str(doc, field, "name", pin->name);
        yyjson_mut_obj_add_int(doc, field, "index", i);
        yyjson_mut_obj_add_str(doc, field, "kind", pin->kind == PIN_EXEC ? "exec" : "data");
        yyjson_mut_obj_add_int(doc, field, "type", pin->type);
        if (pin->type_name) {
            yyjson_mut_obj_add_str(doc, field, "type_name", pin->type_name);
        }
    }
}

static void node_types(yyjson_mut_doc *doc, yyjson_mut_val *root, const char *query)
{
    yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(doc, root, "node_types");
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *node = registry_node(i);
        if (!matches(node->name, query)) {
            continue;
        }
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, nodes);
        yyjson_mut_obj_add_str(doc, object, "name", node->name);
        pins(doc, object, "inputs", node->inputs, node->input_count);
        pins(doc, object, "outputs", node->outputs, node->output_count);
    }
}

static void component_types(
    app_scene_authoring *scene,
    yyjson_mut_doc *doc,
    yyjson_mut_val *root,
    const char *query
)
{
    yyjson_mut_val *components = yyjson_mut_obj_add_arr(doc, root, "component_types");
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *component = scene->registry.components[i];
        if (!matches(component->name, query)) {
            continue;
        }
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, components);
        yyjson_mut_obj_add_str(doc, object, "name", component->name);
        yyjson_mut_obj_add_str(doc, object, "key", component->key);
        yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
        for (int f = 0; f < component->type->field_count; f++) {
            const type_field *spec = &component->type->fields[f];
            yyjson_mut_val *field = yyjson_mut_arr_add_obj(doc, fields);
            yyjson_mut_obj_add_str(doc, field, "name", spec->name);
            yyjson_mut_obj_add_int(doc, field, "type", spec->type);
            if (spec->type_name) {
                yyjson_mut_obj_add_str(doc, field, "type_name", spec->type_name);
            }
        }
    }
}

static void authoring_help(yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    yyjson_mut_val *authoring = yyjson_mut_obj_add_obj(doc, root, "authoring");
    yyjson_mut_obj_add_str(doc, authoring, "create_project", "name: unique folder under projects/. Creates the Main scene. Native editor only; reopen with --project-root.");
    yyjson_mut_obj_add_str(doc, authoring, "create_asset", "name, kind, definition. Prototype definition: components and behaviors. Scene definition: entities with name, prototype, components, behaviors; optional physics and presentation. Graph definition: owner_type and operations. Creates and opens a new saved asset; existing assets are never replaced. Asset creation has no document Undo.");
    yyjson_mut_obj_add_str(doc, authoring, "edit_graph", "target: observed asset ID of the open graph, definition: graph_id or graph_name and operations. Semantic operations are validated and arranged, with one Undo step; save separately.");
    yyjson_mut_obj_add_str(doc, authoring, "graph_operations", "add_node uses type (reflected node name), ref (batch identity), inputs (pin names to literals); Event nodes also use event_name. connect uses from,from_pin,to,to_pin and kind exec/data. add_variable uses name,type,default. set_pin uses node,side (input/output),pin,type and optional type_name; inspect pin types before connecting. bind_input uses node,pin,kind (component/variable/delta_time), with component key/field or variable name. Max 128 operations per graph edit.");
    yyjson_mut_obj_add_str(doc, authoring, "set_start_scene", "target: observed scene asset ID. Saves this scene as the project startup scene in project.json. Native editor only.");
    yyjson_mut_obj_add_str(doc, authoring, "inspect", "kinds: [nodes] or [components], optional query filters reflected names. Default returns current objects, values and revision.");
    yyjson_mut_obj_add_str(doc, authoring, "resolve_objects", "query: names to look up, optional kinds filter object kinds. Returns up to six observed objects ranked by name.");
    yyjson_mut_obj_add_str(doc, authoring, "input", "action: enabled GameActionBinding action name, pressed: boolean, frames: optional 1-600 (default 1). Holds the bound control for that many input samples, then releases it. Requires scene playback. Stop clears held input.");
    yyjson_mut_obj_add_str(doc, authoring, "capture", "path: local PNG filename. Captures the rendered editor including the playing scene.");
    yyjson_mut_obj_add_str(doc, authoring, "transactions", "Submit one authoring operation per transaction, with the latest inspect revision. dry_run validates without mutation. Asset definitions are validated before publication.");
}

char *command_inspect(app_scene_authoring *scene, const char *query, yyjson_val *kinds)
{
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *read = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    if (requested(kinds, "nodes")) {
        node_types(doc, root, query);
    }
    if (requested(kinds, "components")) {
        component_types(scene, doc, root, query);
    }
    authoring_help(doc, root);
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    free(catalog);
    return result;
}

/* Lowercase words of a name or request; camelCase starts a word. */
static int words(const char *text, char out[RESOLVE_WORDS][32])
{
    int count = 0, length = 0;
    for (const char *at = text;; at++) {
        bool word = isalnum((unsigned char)*at);
        bool split = word && length && isupper((unsigned char)*at) &&
                     islower((unsigned char)at[-1]);
        if ((!word || split) && length) {
            out[count++][length] = 0;
            length = 0;
        }
        if (!*at || count == RESOLVE_WORDS) {
            return count;
        }
        if (word && length < 31) {
            out[count][length++] = (char)tolower((unsigned char)*at);
        }
    }
}

/* 3 when the name's words appear in order in the query, else the share of
 * its words the query mentions. */
static double name_score(const char *name, char query[RESOLVE_WORDS][32], int query_count)
{
    char name_words[RESOLVE_WORDS][32];
    int count = words(name, name_words), shared = 0;
    for (int i = 0; i < count; i++) {
        for (int q = 0; q < query_count; q++) {
            if (!strcmp(name_words[i], query[q])) {
                shared++;
                break;
            }
        }
    }
    for (int start = 0; count && start + count <= query_count; start++) {
        int run = 0;
        while (run < count && !strcmp(name_words[run], query[start + run])) {
            run++;
        }
        if (run == count) {
            return 3 + count / 100.0;
        }
    }
    return count ? (double)shared / count : 0;
}

typedef struct
{
    yyjson_val *object;
    double score;
} resolved;

static int by_score(const void *a, const void *b)
{
    const resolved *x = a, *y = b;
    if (x->score != y->score) {
        return x->score > y->score ? -1 : 1;
    }
    return strcmp(command_string(x->object, "id"), command_string(y->object, "id"));
}

/* Observed objects whose names the query mentions, best first. Names only
 * find IDs; commands still act on the IDs they are given. */
char *command_resolve_objects(app_scene_authoring *scene, const char *query, yyjson_val *kinds)
{
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *read = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_val *root = yyjson_doc_get_root(read);
    char query_words[RESOLVE_WORDS][32];
    int query_count = words(query, query_words);
    resolved *found = NULL;
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(yyjson_obj_get(root, "objects"), i, count, object)
    {
        if (yyjson_arr_size(kinds) &&
            !requested(kinds, command_string(object, "kind"))) {
            continue;
        }
        double score = name_score(command_string(object, "name"), query_words, query_count);
        if (score > 0) {
            apush(found, ((resolved){ object, score }));
        }
    }
    qsort(found, (size_t)acount(found), sizeof *found, by_score);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, result);
    yyjson_mut_obj_add_bool(doc, result, "ok", true);
    yyjson_mut_obj_add_str(doc, result, "status", "resolved");
    yyjson_mut_obj_add_strcpy(doc, result, "revision", command_string(root, "revision"));
    yyjson_mut_val *objects = yyjson_mut_obj_add_arr(doc, result, "objects");
    static const char *const keys[] = { "id",        "name", "kind",
                                        "prototype", "path", "document" };
    for (int r = 0; r < acount(found) && r < RESOLVE_LIMIT; r++) {
        yyjson_mut_val *match = yyjson_mut_arr_add_obj(doc, objects);
        for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
            yyjson_val *value = yyjson_obj_get(found[r].object, keys[k]);
            if (value) {
                yyjson_mut_obj_add_val(doc, match, keys[k], yyjson_val_mut_copy(doc, value));
            }
        }
        yyjson_mut_obj_add_real(doc, match, "match", found[r].score);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    afree(found);
    yyjson_doc_free(read);
    free(catalog);
    return json;
}
