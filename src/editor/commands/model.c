#include "editor/commands/model.h"

#include "editor/commands/internal.h"
#include "editor/commands/quick_edit.h"
#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <yyjson.h>

#define MODEL_HOST_SIZE 256
#define MODEL_NAME_SIZE 128
#define MODEL_ENDPOINT_SIZE 512
#define MODEL_ERROR_SIZE 256
#define MODEL_DEFAULT_MAX_TOKENS 800

static char model_host[MODEL_HOST_SIZE];
static char model_name[MODEL_NAME_SIZE];
static bool model_initialized;
static bool model_json_schema;
static bool text_has_phrase(const char *text, const char *phrase);

static void set_error(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message ? message : "Command model failed");
    }
}

static void initialize_settings()
{
    if (model_initialized) {
        return;
    }
    snprintf(
        model_host,
        sizeof model_host,
        "%s",
        getenv("CHIBI_MODEL_URL") ? getenv("CHIBI_MODEL_URL") : "http://127.0.0.1:11435/api"
    );
    snprintf(model_name, sizeof model_name, "%s", getenv("CHIBI_MODEL") ? getenv("CHIBI_MODEL") : "gpt-oss:20b");
    model_initialized = true;
}

void editor_command_model_get_settings(char *host, size_t host_size, char *model, size_t model_size)
{
    initialize_settings();
    if (host_size) {
        snprintf(host, host_size, "%s", model_host);
    }
    if (model_size) {
        snprintf(model, model_size, "%s", model_name);
    }
}

bool editor_command_model_set_settings(const char *host, const char *model, char *error, size_t error_size)
{
    if (!host || !*host || strlen(host) >= MODEL_HOST_SIZE) {
        set_error(error, error_size, "Enter a model host");
        return false;
    }
    if (!model || !*model || strlen(model) >= MODEL_NAME_SIZE) {
        set_error(error, error_size, "Enter a model name");
        return false;
    }
    if (strncmp(host, "https://", 8) == 0) {
        set_error(error, error_size, "Use an HTTP OpenAI-compatible host");
        return false;
    }
    if (strncmp(host, "http://", 7) && strchr(host, ':') == NULL) {
        set_error(error, error_size, "Host must include an address or http://");
        return false;
    }
    initialize_settings();
    snprintf(model_host, sizeof model_host, "%s", host);
    snprintf(model_name, sizeof model_name, "%s", model);
    return true;
}

bool editor_command_model_json_schema()
{
    return model_json_schema;
}

void editor_command_model_set_json_schema(bool enabled)
{
    model_json_schema = enabled;
}

static bool append_endpoint(const char *host, char *endpoint, size_t size)
{
    const char *prefix = strncmp(host, "http://", 7) ? "http://" : "";
    int written = snprintf(endpoint, size, "%s%s", prefix, host);
    if (written < 0 || (size_t)written >= size) {
        return false;
    }
    while (written > 0 && endpoint[written - 1] == '/') {
        endpoint[--written] = 0;
    }
    if ((written >= 17 && !strcmp(endpoint + written - 17, "/chat/completions")) ||
        (written >= 9 && !strcmp(endpoint + written - 9, "/api/chat"))) {
        return true;
    }
    const char *suffix = written >= 4 && !strcmp(endpoint + written - 4, "/api")
                           ? "/chat"
                       : written >= 3 && !strcmp(endpoint + written - 3, "/v1")
                           ? "/chat/completions"
                           : "/v1/chat/completions";
    return snprintf(endpoint + written, size - (size_t)written, "%s", suffix) >= 0 &&
           strlen(endpoint) < size;
}

static yyjson_mut_val *schema_string(yyjson_mut_doc *doc, const char *const *values, int count)
{
    yyjson_mut_val *value = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, value, "type", "string");
    if (values) {
        yyjson_mut_val *enum_values = yyjson_mut_obj_add_arr(doc, value, "enum");
        for (int i = 0; i < count; i++) {
            yyjson_mut_arr_add_str(doc, enum_values, values[i]);
        }
    }
    return value;
}

static void schema_add_unique(yyjson_mut_doc *doc, yyjson_mut_val *values, const char *id)
{
    if (!id || !*id) {
        return;
    }
    for (size_t i = 0; i < yyjson_mut_arr_size(values); i++) {
        if (!strcmp(yyjson_mut_get_str(yyjson_mut_arr_get(values, i)), id)) {
            return;
        }
    }
    yyjson_mut_arr_add_strcpy(doc, values, id);
}

static yyjson_mut_val *schema_targets(yyjson_mut_doc *doc, yyjson_val *catalog)
{
    yyjson_mut_val *schema = schema_string(doc, NULL, 0);
    yyjson_mut_val *values = yyjson_mut_obj_add_arr(doc, schema, "enum");
    yyjson_val *objects = yyjson_obj_get(catalog, "objects");
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        schema_add_unique(doc, values, yyjson_get_str(yyjson_obj_get(object, "id")));
    }
    yyjson_val *panels = yyjson_obj_get(catalog, "panels");
    yyjson_arr_foreach(panels, i, count, object)
    {
        schema_add_unique(doc, values, yyjson_get_str(yyjson_obj_get(object, "id")));
    }
    schema_add_unique(doc, values, "scene");
    schema_add_unique(doc, values, "preview");
    return schema;
}

static bool verb_allowed(const char *verb, bool allow_duplicate, const char *request)
{
    if (!strcmp(verb, "duplicate")) {
        return allow_duplicate;
    }
    if (!strcmp(verb, "assert")) {
        return text_has_phrase(request, "assert") ||
               text_has_phrase(request, "check") ||
               text_has_phrase(request, "verify");
    }
    if (!strcmp(verb, "select")) {
        return text_has_phrase(request, "select") ||
               text_has_phrase(request, "highlight") ||
               text_has_phrase(request, "focus");
    }
    if (!strcmp(verb, "open")) {
        return text_has_phrase(request, "open") || text_has_phrase(request, "show") ||
               text_has_phrase(request, "navigate");
    }
    return true;
}

/* Operations that name a field of their target. */
static bool field_verb(const char *verb)
{
    return !strcmp(verb, "set") || !strcmp(verb, "assert") || !strcmp(verb, "connect");
}

/* The allowed verbs that do (fields) or do not name a field. */
static yyjson_mut_val *schema_verbs(yyjson_mut_doc *doc, bool allow_duplicate, const char *request, int fields)
{
    static const char *const verbs[] = { "set",     "assert", "duplicate",
                                         "connect", "open",   "select",
                                         "frame",   "panel",  "runtime",
                                         "save",    "undo",   "redo" };
    yyjson_mut_val *schema = schema_string(doc, NULL, 0);
    yyjson_mut_val *allowed = yyjson_mut_obj_add_arr(doc, schema, "enum");
    for (size_t i = 0; i < sizeof verbs / sizeof verbs[0]; i++) {
        bool named = field_verb(verbs[i]);
        if (verb_allowed(verbs[i], allow_duplicate, request) &&
            (fields < 0 || named == (fields > 0))) {
            yyjson_mut_arr_add_str(doc, allowed, verbs[i]);
        }
    }
    return schema;
}

/* One operation shape; `field` is NULL when the shape names no field. */
static yyjson_mut_val *schema_shape(
    yyjson_mut_doc *doc,
    yyjson_mut_val *verbs,
    yyjson_mut_val *target,
    yyjson_mut_val *field
)
{
    yyjson_mut_val *operation = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, operation, "type", "object");
    yyjson_mut_obj_add_bool(doc, operation, "additionalProperties", false);
    yyjson_mut_val *required = yyjson_mut_obj_add_arr(doc, operation, "required");
    yyjson_mut_arr_add_str(doc, required, "op");
    yyjson_mut_val *properties = yyjson_mut_obj_add_obj(doc, operation, "properties");
    yyjson_mut_obj_add_val(doc, properties, "op", verbs);
    yyjson_mut_obj_add_val(doc, properties, "target", target);
    if (field) {
        yyjson_mut_arr_add_str(doc, required, "target");
        yyjson_mut_arr_add_str(doc, required, "field");
        yyjson_mut_obj_add_val(doc, properties, "field", field);
    }
    const char *string_names[] = { "destination", "pin", "result", "result_target" };
    for (size_t i = 0; i < sizeof string_names / sizeof string_names[0]; i++) {
        yyjson_mut_obj_add_val(doc, properties, string_names[i], schema_string(doc, NULL, 0));
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, properties, "value");
    yyjson_mut_val *value_types = yyjson_mut_obj_add_arr(doc, value, "anyOf");
    const char *value_type_names[] = { "string", "boolean", "integer", "number", "null" };
    for (size_t i = 0; i < sizeof value_type_names / sizeof value_type_names[0]; i++) {
        yyjson_mut_val *type = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, type, "type", value_type_names[i]);
        yyjson_mut_arr_append(value_types, type);
    }
    yyjson_mut_val *alpha = yyjson_mut_obj_add_obj(doc, properties, "alpha");
    yyjson_mut_val *alpha_types = yyjson_mut_obj_add_arr(doc, alpha, "anyOf");
    yyjson_mut_val *alpha_number = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, alpha_number, "type", "integer");
    yyjson_mut_obj_add_int(doc, alpha_number, "minimum", 0);
    yyjson_mut_obj_add_int(doc, alpha_number, "maximum", 255);
    yyjson_mut_arr_append(alpha_types, alpha_number);
    yyjson_mut_val *alpha_null = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, alpha_null, "type", "null");
    yyjson_mut_arr_append(alpha_types, alpha_null);
    return operation;
}

/* The observed field IDs of an object, as an enum. */
static yyjson_mut_val *schema_fields(yyjson_mut_doc *doc, yyjson_val *object)
{
    yyjson_mut_val *schema = schema_string(doc, NULL, 0);
    yyjson_mut_val *values = yyjson_mut_obj_add_arr(doc, schema, "enum");
    size_t i, count;
    yyjson_val *field;
    yyjson_arr_foreach(yyjson_obj_get(object, "fields"), i, count, field)
    {
        schema_add_unique(doc, values, yyjson_get_str(yyjson_obj_get(field, "id")));
    }
    return schema;
}

/* Each observed target admits only its own fields; operations without a field
 * take any observed target. Duplicates name results the catalog does not
 * list yet, so their requests keep free-form targets and fields. */
static yyjson_mut_val *schema_operation(
    yyjson_mut_doc *doc,
    bool allow_duplicate,
    yyjson_val *catalog,
    const char *request
)
{
    if (allow_duplicate || !catalog) {
        return schema_shape(
            doc,
            schema_verbs(doc, allow_duplicate, request, -1),
            schema_string(doc, NULL, 0),
            schema_string(doc, NULL, 0)
        );
    }
    yyjson_mut_val *operation = yyjson_mut_obj(doc);
    yyjson_mut_val *shapes = yyjson_mut_obj_add_arr(doc, operation, "anyOf");
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(yyjson_obj_get(catalog, "objects"), i, count, object)
    {
        const char *id = yyjson_get_str(yyjson_obj_get(object, "id"));
        if (!id || !yyjson_arr_size(yyjson_obj_get(object, "fields"))) {
            continue;
        }
        const char *const target[] = { id };
        yyjson_mut_arr_append(
            shapes,
            schema_shape(
                doc,
                schema_verbs(doc, false, request, 1),
                schema_string(doc, target, 1),
                schema_fields(doc, object)
            )
        );
    }
    yyjson_mut_arr_append(
        shapes,
        schema_shape(doc, schema_verbs(doc, false, request, 0), schema_targets(doc, catalog), NULL)
    );
    return operation;
}

static yyjson_mut_val *model_schema(
    yyjson_mut_doc *doc,
    bool allow_duplicate,
    yyjson_val *catalog,
    const char *request
)
{
    static const char *const decisions[] = { "act", "ask", "unsupported" };
    yyjson_mut_val *schema = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, schema, "type", "object");
    yyjson_mut_obj_add_bool(doc, schema, "additionalProperties", false);
    yyjson_mut_val *required = yyjson_mut_obj_add_arr(doc, schema, "required");
    const char *required_names[] = { "decision" };
    for (int i = 0; i < (int)(sizeof required_names / sizeof required_names[0]); i++) {
        yyjson_mut_arr_add_str(doc, required, required_names[i]);
    }
    yyjson_mut_val *properties = yyjson_mut_obj_add_obj(doc, schema, "properties");
    yyjson_mut_obj_add_val(doc, properties, "decision", schema_string(doc, decisions, 3));
    yyjson_mut_val *operations = yyjson_mut_obj_add_obj(doc, properties, "operations");
    yyjson_mut_obj_add_str(doc, operations, "type", "array");
    yyjson_mut_obj_add_int(doc, operations, "maxItems", 8);
    yyjson_mut_obj_add_val(doc, operations, "items", schema_operation(doc, allow_duplicate, catalog, request));
    yyjson_mut_obj_add_val(doc, properties, "question", schema_string(doc, NULL, 0));
    yyjson_mut_val *choices = yyjson_mut_obj_add_obj(doc, properties, "choices");
    yyjson_mut_obj_add_str(doc, choices, "type", "array");
    yyjson_mut_obj_add_int(doc, choices, "maxItems", 12);
    yyjson_mut_obj_add_val(doc, choices, "items", schema_string(doc, NULL, 0));
    yyjson_mut_obj_add_val(doc, properties, "unsupported", schema_string(doc, NULL, 0));
    return schema;
}

static yyjson_val *packet_root(const char *packet, yyjson_doc **document, char *error, size_t error_size)
{
    *document = yyjson_read(packet, strlen(packet), 0);
    yyjson_val *root = *document ? yyjson_doc_get_root(*document) : NULL;
    if (!yyjson_is_obj(root)) {
        set_error(error, error_size, "Invalid command packet");
        yyjson_doc_free(*document);
        *document = NULL;
        return NULL;
    }
    return root;
}

static const char *text_find_phrase(const char *text, const char *phrase)
{
    size_t length = strlen(phrase);
    if (!length) {
        return NULL;
    }
    for (const char *start = text; *start; start++) {
        size_t i = 0;
        while (i < length && start[i] &&
               tolower((unsigned char)start[i]) == tolower((unsigned char)phrase[i])) {
            i++;
        }
        if (i == length && (start == text || !isalnum((unsigned char)start[-1])) &&
            !isalnum((unsigned char)start[i])) {
            return start;
        }
    }
    return NULL;
}

static bool text_has_phrase(const char *text, const char *phrase)
{
    return text_find_phrase(text, phrase) != NULL;
}

static bool text_contains_fold(const char *text, const char *part)
{
    for (const char *at = text; *at; at++) {
        size_t i = 0;
        while (part[i] && at[i] &&
               tolower((unsigned char)at[i]) == tolower((unsigned char)part[i])) {
            i++;
        }
        if (!part[i]) {
            return true;
        }
    }
    return false;
}

static bool request_instance(const char *request)
{
    if ((text_has_phrase(request, "every") || text_has_phrase(request, "all")) &&
        (text_has_phrase(request, "instance") || text_has_phrase(request, "instances"))) {
        return false;
    }
    const char *not = text_find_phrase(request, "not");
    const char *prototype = text_find_phrase(request, "prototype");
    const char *template = text_find_phrase(request, "template");
    const char *blueprint = text_find_phrase(request, "blueprint");
    if (not &&
        ((prototype && prototype < not) || (template && template < not) ||
         (blueprint && blueprint < not)) &&
        (text_find_phrase(not, "instance") || text_find_phrase(not, "copy"))) {
        return false;
    }
    return text_has_phrase(request, "instance") ||
           (text_has_phrase(request, "copy") &&
            !text_has_phrase(request, "make a copy") &&
            !text_has_phrase(request, "create a copy")) ||
           text_has_phrase(request, "itself") || text_has_phrase(request, "own") ||
           text_has_phrase(request, "currently in the scene") ||
           text_has_phrase(request, "in this scene") ||
           text_has_phrase(request, "in the scene") ||
           text_has_phrase(request, "on this scene") ||
           text_has_phrase(request, "override only") ||
           text_has_phrase(request, "this entity") ||
           text_has_phrase(request, "this object") ||
           text_has_phrase(request, "only this") ||
           text_has_phrase(request, "just this") ||
           text_has_phrase(request, "selected entity") ||
           text_has_phrase(request, "selected instance") ||
           (text_has_phrase(request, "prototype") &&
            (text_has_phrase(request, "untouched") ||
             text_has_phrase(request, "unchanged") ||
             text_has_phrase(request, "do not edit")));
}

static bool text_matches_request(const char *request, const char *text)
{
    static const char *const ignored[] = {
        "the",  "and",  "for",   "with",   "into",     "from",
        "this", "that", "make",  "change", "increase", "decrease",
        "set",  "its",  "their", "size"
    };
    for (const char *word = request; *word;) {
        while (*word && !isalnum((unsigned char)*word)) {
            word++;
        }
        const char *end = word;
        while (isalnum((unsigned char)*end)) {
            end++;
        }
        size_t length = (size_t)(end - word);
        if (!length) {
            break;
        }
        if (length >= 3 && length < 64) {
            char token[64];
            for (size_t i = 0; i < length; i++) {
                token[i] = (char)tolower((unsigned char)word[i]);
            }
            token[length] = 0;
            bool skip = false;
            for (size_t i = 0; i < sizeof ignored / sizeof ignored[0]; i++) {
                if (!strcmp(token, ignored[i])) {
                    skip = true;
                    break;
                }
            }
            if (!skip) {
                if (length > 4 && token[length - 1] == 's') {
                    token[--length] = 0;
                }
                for (const char *start = text; *start; start++) {
                    if (!strncasecmp(start, token, length)) {
                        return true;
                    }
                }
            }
        }
        word = end;
    }
    return false;
}

static bool model_field_matches(yyjson_val *source, const char *request)
{
    yyjson_val *fields = yyjson_obj_get(source, "fields");
    size_t i, count;
    yyjson_val *field;
    yyjson_arr_foreach(fields, i, count, field)
    {
        if (text_matches_request(request, command_string(field, "name"))) {
            return true;
        }
    }
    return false;
}

static bool model_component_matches(yyjson_val *source, const char *request)
{
    yyjson_val *components = yyjson_obj_get(source, "components");
    size_t i, count;
    yyjson_val *component;
    yyjson_arr_foreach(components, i, count, component)
    {
        if (text_matches_request(request, yyjson_get_str(component))) {
            return true;
        }
    }
    return false;
}

static bool model_has_object(yyjson_mut_val *objects, const char *id)
{
    size_t i, count;
    yyjson_mut_val *object;
    yyjson_mut_arr_foreach(objects, i, count, object)
    {
        if (!strcmp(yyjson_mut_get_str(yyjson_mut_obj_get(object, "id")), id)) {
            return true;
        }
    }
    return false;
}

static void model_add_object(
    yyjson_mut_doc *doc,
    yyjson_mut_val *objects,
    yyjson_val *source,
    bool fields,
    const char *request
)
{
    const char *id = command_string(source, "id");
    if (!*id || model_has_object(objects, id)) {
        return;
    }
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, objects);
    const char *const keys[] = { "id",        "name",       "kind",
                                 "prototype", "selected",   "entity",
                                 "node",      "asset_kind", "components" };
    for (int i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
        yyjson_val *value = yyjson_obj_get(source, keys[i]);
        if (value) {
            yyjson_mut_obj_add_val(doc, object, keys[i], yyjson_val_mut_copy(doc, value));
        }
    }
    yyjson_val *source_fields = yyjson_obj_get(source, "fields");
    if (!fields || !yyjson_is_arr(source_fields)) {
        return;
    }
    yyjson_mut_val *target_fields = yyjson_mut_obj_add_arr(doc, object, "fields");
    static const char *const field_keys[] = { "id",       "name",   "label",
                                              "type",     "value",  "choices",
                                              "editable", "output", "index",
                                              "pin" };
    size_t i, count;
    yyjson_val *field;
    yyjson_arr_foreach(source_fields, i, count, field)
    {
        if (request && !text_matches_request(request, command_string(field, "name"))) {
            continue;
        }
        yyjson_mut_val *target = yyjson_mut_arr_add_obj(doc, target_fields);
        for (int k = 0; k < (int)(sizeof field_keys / sizeof field_keys[0]); k++) {
            yyjson_val *value = yyjson_obj_get(field, field_keys[k]);
            if (value) {
                yyjson_mut_obj_add_val(doc, target, field_keys[k], yyjson_val_mut_copy(doc, value));
            }
        }
    }
}

static yyjson_mut_doc *model_catalog(yyjson_val *catalog, const char *request)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    const char *const keys[] = { "document", "playing", "capabilities", "panels" };
    for (int i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
        yyjson_val *value = yyjson_obj_get(catalog, keys[i]);
        if (value) {
            yyjson_mut_obj_add_val(doc, root, keys[i], yyjson_val_mut_copy(doc, value));
        }
    }
    yyjson_mut_val *objects = yyjson_mut_obj_add_arr(doc, root, "objects");
    yyjson_val *source_objects = yyjson_obj_get(catalog, "objects");
    bool instance = request_instance(request);
    bool focused = false;
    size_t i, count;
    yyjson_val *source;
    yyjson_arr_foreach(source_objects, i, count, source)
    {
        if (!text_has_phrase(request, command_string(source, "name"))) {
            continue;
        }
        if (instance && !strcmp(command_string(source, "kind"), "prototype")) {
            continue;
        }
        focused = true;
        bool entity = !strcmp(command_string(source, "kind"), "entity");
        model_add_object(doc, objects, source, !entity || instance, NULL);
        if (!entity) {
            continue;
        }
        if (instance) {
            continue;
        }
        const char *prototype_name = command_string(source, "prototype");
        size_t p, prototype_count;
        yyjson_val *prototype;
        yyjson_arr_foreach(source_objects, p, prototype_count, prototype)
        {
            if (!strcmp(command_string(prototype, "kind"), "prototype") &&
                !strcmp(command_string(prototype, "name"), prototype_name)) {
                model_add_object(doc, objects, prototype, true, NULL);
            }
        }
    }
    if (!focused) {
        yyjson_arr_foreach(source_objects, i, count, source)
        {
            bool field_match = model_field_matches(source, request);
            bool component_match = model_component_matches(source, request);
            if (!field_match && !component_match &&
                !text_matches_request(request, command_string(source, "name"))) {
                continue;
            }
            bool entity = !strcmp(command_string(source, "kind"), "entity");
            model_add_object(doc, objects, source, !entity || instance, field_match ? request : NULL);
            if (!entity) {
                continue;
            }
            const char *prototype_name = command_string(source, "prototype");
            size_t p, prototype_count;
            yyjson_val *prototype;
            yyjson_arr_foreach(source_objects, p, prototype_count, prototype)
            {
                if (!strcmp(command_string(prototype, "kind"), "prototype") &&
                    !strcmp(command_string(prototype, "name"), prototype_name)) {
                    bool matching = model_field_matches(prototype, request);
                    model_add_object(doc, objects, prototype, true, matching ? request : NULL);
                }
            }
        }
    }
    return doc;
}

static char *resolved_request(yyjson_val *packet)
{
    const char *request = command_string(packet, "command");
    yyjson_val *bindings = yyjson_obj_get(packet, "bindings");
    const char *answer = command_string(bindings, "answer");
    if (!*answer) {
        answer = command_string(bindings, "target");
    }
    if (!*answer) {
        return xstrdup(request);
    }
    size_t size = strlen(request) + strlen(answer) + 64;
    char *resolved = xmalloc(size);
    snprintf(resolved, size, "%s\nUser clarification: %s", request, answer);
    return resolved;
}

static char *prompt_for_packet(yyjson_val *packet, bool focus, char *error, size_t error_size)
{
    yyjson_val *catalog = yyjson_obj_get(packet, "catalog");
    char *command = resolved_request(packet);
    if (!yyjson_is_obj(catalog) || !*command) {
        set_error(error, error_size, "Command packet is missing its catalog or command");
        free(command);
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_doc *focused_catalog = focus ? model_catalog(catalog, command) : NULL;
    yyjson_mut_obj_add_val(
        doc,
        root,
        "catalog",
        focused_catalog
            ? yyjson_mut_val_mut_copy(doc, yyjson_mut_doc_get_root(focused_catalog))
            : yyjson_val_mut_copy(doc, catalog)
    );
    yyjson_mut_obj_add_strcpy(doc, root, "request", command);
    yyjson_val *context = yyjson_obj_get(packet, "context");
    yyjson_val *bindings = yyjson_obj_get(packet, "bindings");
    if (yyjson_is_obj(context)) {
        yyjson_mut_obj_add_val(doc, root, "context", yyjson_val_mut_copy(doc, context));
    }
    if (yyjson_is_obj(bindings)) {
        yyjson_mut_obj_add_val(doc, root, "reply", yyjson_val_mut_copy(doc, bindings));
    }
    char *prompt = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    free(command);
    if (focused_catalog) {
        yyjson_mut_doc_free(focused_catalog);
    }
    return prompt;
}

static int max_tokens()
{
    const char *value = getenv("CHIBI_MODEL_MAX_TOKENS");
    if (!value || !*value) {
        return MODEL_DEFAULT_MAX_TOKENS;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    return end && !*end && parsed >= 128 && parsed <= 4096 ? (int)parsed
                                                           : MODEL_DEFAULT_MAX_TOKENS;
}

static const char *instructions()
{
    return "You are a game editor command parser, not a coding agent. You have "
           "no shell, "
           "filesystem, source inspection, or external tools. Never request or "
           "invent them. "
           "Use only the observed catalog. Return one complete JSON proposal. "
           "Choose act, ask, or unsupported. If any requested action is "
           "unavailable, "
           "return unsupported with no operations. Use only objects, fields, "
           "panels, "
           "capabilities, and values in the observed catalog. Object and field "
           "IDs are "
           "opaque exact strings; copy them exactly. Return every requested "
           "action and "
           "no extras. If equally named objects fit, ask a short question and "
           "list their "
           "object IDs in choices. Use context.targets for unqualified "
           "follow-ups. A User clarification in the request answers your prior "
           "question; use it with the original request and do not repeat an "
           "answered question. Numeric values are JSON "
           "numbers; "
           "booleans are JSON booleans. Copy explicit hex colors exactly as "
           "#RRGGBB or #RRGGBBAA. Colors may also specify alpha 0-255. "
           "Common named colors use standard RGB values: blue is #0000FF, "
           "red is #FF0000, and green is #00FF00. Do not ask for a shade "
           "when a common named color is given. "
           "A color without alpha preserves the observed alpha. Use exact "
           "listed choice "
           "values. A set uses a final absolute value calculated from the "
           "observed value. "
           "Only entity objects can be selected, framed, or duplicated. To "
           "edit a copy, "
           "duplicate with result, then use the original target and that "
           "result_target. "
           "Editing only an existing scene instance means set its entity ID "
           "directly; do not duplicate it. "
           "A connect uses target/field for the source node and "
           "destination/pin for the "
           "destination node and input field. Graph creation and deletion are "
           "unsupported. "
           "Panel uses a panel ID and boolean value. Runtime uses scene or "
           "preview and "
           "play, pause, or stop. Save, undo, and redo contain only op and "
           "must be alone. "
           "Content mutations share one document and cannot mix with "
           "navigation. Ask when "
           "essential information is missing. Never invent a target, color, "
           "amount, or affected field for a qualitative request. Each proposed "
           "field must be named or unambiguously implied by the request. A "
           "clear "
           "request supported by the catalog "
           "is act, not ask or unsupported. Do not explain successful "
           "operations. "
           "An explicit component.field name identifies that exact field; "
           "do not ask between other fields with the same short name. "
           "Box width or height means the collider dimension unless the "
           "request explicitly says visual or appearance. "
           "When a request names a scene entity, edit its prototype by default "
           "so the "
           "change applies to all instances; only edit the scene instance if "
           "the user "
           "explicitly asks for that instance. An entity and its prototype "
           "with the same name are not ambiguous for an edit. For an "
           "unqualified color "
           "request, including tint, surface, sprite, or material color when "
           "there is no separate material field, use "
           "Appearance Color rather than border, glow, or material colors. "
           "Return only the fields needed for the selected decision and "
           "operation.";
}

static void add_message(yyjson_mut_doc *doc, yyjson_mut_val *messages, const char *role, const char *content)
{
    yyjson_mut_val *message = yyjson_mut_arr_add_obj(doc, messages);
    yyjson_mut_obj_add_str(doc, message, "role", role);
    yyjson_mut_obj_add_strcpy(doc, message, "content", content);
}

static bool ollama_api_endpoint(const char *endpoint)
{
    size_t size = strlen(endpoint);
    return size >= 9 && !strcmp(endpoint + size - 9, "/api/chat");
}

static char *immediate_response(char *body)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "local_response", body);
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    free(body);
    return result;
}

static char *policy_result(const char *status, const char *message)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_str(doc, root, "provider", "native");
    yyjson_mut_obj_add_str(doc, root, "status", status);
    yyjson_mut_obj_add_str(doc, root, "message", message);
    yyjson_mut_obj_add_arr(doc, root, "operations");
    if (!strcmp(status, "needs_clarification")) {
        yyjson_mut_obj_add_str(doc, root, "binding", "answer");
    }
    char *body = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return immediate_response(body);
}

static bool ambiguous_dimension(yyjson_val *catalog, const char *request)
{
    bool width = text_has_phrase(request, "width") ||
                 text_has_phrase(request, "wide") ||
                 text_has_phrase(request, "widen");
    bool height = text_has_phrase(request, "height") ||
                  text_has_phrase(request, "high") ||
                  text_has_phrase(request, "tall");
    if ((!width && !height) || text_has_phrase(request, "appearance") ||
        text_has_phrase(request, "GameAppearance") ||
        text_has_phrase(request, "sprite") || text_has_phrase(request, "canvas") ||
        text_has_phrase(request, "art") || text_has_phrase(request, "artwork") ||
        text_has_phrase(request, "visual") || text_has_phrase(request, "visible") ||
        text_has_phrase(request, "render") || text_has_phrase(request, "drawn") ||
        text_has_phrase(request, "drawing") || text_has_phrase(request, "rendered") ||
        text_has_phrase(request, "displayed") || text_has_phrase(request, "tint") ||
        text_has_phrase(request, "color") || text_has_phrase(request, "collider") ||
        text_has_phrase(request, "GameCollider") ||
        text_has_phrase(request, "collision") || text_has_phrase(request, "hitbox") ||
        text_has_phrase(request, "box") || text_has_phrase(request, "on screen")) {
        return false;
    }
    bool visual = false, collision = false;
    yyjson_val *objects = yyjson_obj_get(catalog, "objects");
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        yyjson_val *fields = yyjson_obj_get(object, "fields");
        size_t j, field_count;
        yyjson_val *field;
        yyjson_arr_foreach(fields, j, field_count, field)
        {
            const char *name = command_string(field, "name");
            if ((!width || strcmp(name, "width")) &&
                (!height || strcmp(name, "height"))) {
                continue;
            }
            visual |= !strcmp(command_string(field, "component"), "GameAppearance");
            collision |= !strcmp(command_string(field, "component"), "GameCollider");
        }
    }
    return visual && collision;
}

static char *policy_for_packet(const char *packet_json)
{
    yyjson_doc *doc = yyjson_read(packet_json, strlen(packet_json), 0);
    yyjson_val *packet = yyjson_doc_get_root(doc);
    const char *request = command_string(packet, "command");
    yyjson_val *catalog = yyjson_obj_get(packet, "catalog");
    if (!*request || !yyjson_is_obj(catalog)) {
        yyjson_doc_free(doc);
        return NULL;
    }
    if (text_has_phrase(request, "nearest") && text_has_phrase(request, "cursor")) {
        yyjson_doc_free(doc);
        return policy_result("unsupported", "Cursor position is not available to editor commands");
    }
    bool answered = *command_string(yyjson_obj_get(packet, "bindings"), "answer") ||
                  *command_string(yyjson_obj_get(packet, "bindings"), "target");
    yyjson_val *objects = yyjson_obj_get(catalog, "objects");
    bool named = false, selected = false, particle_target = false;
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        const char *name = command_string(object, "name");
        named |= *name && text_has_phrase(request, name);
        selected |= !strcmp(command_string(object, "kind"), "entity") &&
                    yyjson_get_bool(yyjson_obj_get(object, "selected"));
        particle_target |= text_contains_fold(name, "particle") ||
                           text_contains_fold(name, "emitter") ||
                           text_contains_fold(name, "burst");
        yyjson_val *fields = yyjson_obj_get(object, "fields");
        size_t j, field_count;
        yyjson_val *field;
        yyjson_arr_foreach(fields, j, field_count, field)
        {
            particle_target |= text_contains_fold(command_string(field, "id"), "particle") ||
                               text_contains_fold(command_string(field, "component"), "particle");
        }
    }
    bool particles = text_has_phrase(request, "particle") ||
                     text_has_phrase(request, "particles") ||
                     text_has_phrase(request, "emitter") ||
                     text_has_phrase(request, "sparks");
    if (particles && !particle_target) {
        yyjson_doc_free(doc);
        return policy_result("unsupported", "No particle target is available in this editor catalog");
    }
    if (!answered && text_has_phrase(request, "selected") &&
        (text_has_phrase(request, "if") || !selected)) {
        yyjson_doc_free(doc);
        return policy_result("needs_clarification", "Which selected instance should change?");
    }
    if (!answered && text_has_phrase(request, "cursor") &&
        (text_has_phrase(request, "position") || text_has_phrase(request, "move"))) {
        yyjson_doc_free(doc);
        return policy_result("needs_clarification", "What position should I use?");
    }
    bool percent = strchr(request, '%') || text_has_phrase(request, "percent");
    bool size_comparison = text_has_phrase(request, "bigger") ||
                           text_has_phrase(request, "larger") ||
                           text_has_phrase(request, "smaller");
    bool axis = text_has_phrase(request, "width") ||
                text_has_phrase(request, "height") ||
                text_has_phrase(request, "wide") ||
                text_has_phrase(request, "high") ||
                text_has_phrase(request, "tall") ||
                text_has_phrase(request, "scale");
    if (!answered && percent && size_comparison && !axis) {
        yyjson_doc_free(doc);
        return policy_result("needs_clarification", "Which size or dimension should change?");
    }
    yyjson_val *context = yyjson_obj_get(packet, "context");
    bool has_context = yyjson_arr_size(yyjson_obj_get(context, "targets")) > 0;
    bool generic = text_has_phrase(request, "it") ||
                   text_has_phrase(request, "the object") ||
                   text_has_phrase(request, "the character") ||
                   text_has_phrase(request, "character");
    bool edit = text_has_phrase(request, "make") || text_has_phrase(request, "set") ||
                text_has_phrase(request, "change") ||
                text_has_phrase(request, "turn") ||
                text_has_phrase(request, "give") ||
                text_has_phrase(request, "color") ||
                text_has_phrase(request, "paint") ||
                text_has_phrase(request, "recolor") ||
                text_has_phrase(request, "tint") ||
                text_has_phrase(request, "increase") ||
                text_has_phrase(request, "decrease") ||
                text_has_phrase(request, "resize") ||
                text_has_phrase(request, "move");
    if (!answered && !named && !has_context && generic && edit) {
        yyjson_doc_free(doc);
        return policy_result("needs_clarification", "Which object should change?");
    }
    if (!answered && ambiguous_dimension(catalog, request)) {
        yyjson_doc_free(doc);
        return policy_result("needs_clarification", "Appearance or collider dimension?");
    }
    bool shape = text_has_phrase(request, "circle") ||
                 text_has_phrase(request, "circular") ||
                 text_has_phrase(request, "round") ||
                 text_has_phrase(request, "rounder") ||
                 text_has_phrase(request, "rounded");
    bool collision = text_has_phrase(request, "collider") ||
                     text_has_phrase(request, "colliders") ||
                     text_has_phrase(request, "collision") ||
                     text_has_phrase(request, "GameCollider") ||
                     text_has_phrase(request, "collide") ||
                     text_has_phrase(request, "collides") ||
                     text_has_phrase(request, "hitbox") ||
                     text_has_phrase(request, "hit shape") ||
                     text_has_phrase(request, "hit area") ||
                     text_has_phrase(request, "hit region");
    bool appearance = text_has_phrase(request, "appearance") ||
                      text_has_phrase(request, "GameAppearance") ||
                      text_has_phrase(request, "sprite") ||
                      text_has_phrase(request, "visual") ||
                      text_has_phrase(request, "rendered");
    yyjson_doc_free(doc);
    if (!answered && shape && !collision && !appearance) {
        return policy_result("needs_clarification", "Should I change the visual shape or the collider?");
    }
    return NULL;
}

static bool allows_duplicate(const char *command)
{
    return text_has_phrase(command, "duplicate") ||
           text_has_phrase(command, "clone") ||
           text_has_phrase(command, "make a copy") ||
           text_has_phrase(command, "create a copy") ||
           !strncasecmp(command, "copy ", 5);
}

/* Native Ollama chat: one worked example, then schema-constrained content. */
static void ollama_body(yyjson_mut_doc *doc, yyjson_mut_val *body, yyjson_mut_val *messages, yyjson_mut_val *schema)
{
    add_message(doc, messages, "user", "{\"request\":\"Change Scout's color to blue\",\"catalog\":{\"revision\":\"example\",\"document\":\"scene-example\",\"capabilities\":[\"set\"],\"panels\":[],\"objects\":[{\"id\":\"entity:example:1\",\"name\":\"Scout\",\"kind\":\"entity\",\"prototype\":\"DemoShip\"},{\"id\":\"/example/content/prototypes/DemoShip.yaml\",\"name\":\"DemoShip\",\"kind\":\"prototype\",\"fields\":[{\"id\":\"GAME.GameAppearance.color\",\"name\":\"color\",\"label\":\"Appearance Color\",\"type\":\"color\",\"value\":4294967295}]}]}}");
    add_message(doc, messages, "assistant", "{\"decision\":\"act\",\"operations\":[{\"op\":\"set\",\"target\":\"/example/content/prototypes/DemoShip.yaml\",\"field\":\"GAME.GameAppearance.color\",\"value\":\"#0000FF\"}]}");
    yyjson_mut_obj_add_str(doc, body, "think", "low");
    yyjson_mut_obj_add_str(doc, body, "keep_alive", "30m");
    yyjson_mut_obj_add_val(doc, body, "format", schema);
    yyjson_mut_val *options = yyjson_mut_obj_add_obj(doc, body, "options");
    yyjson_mut_obj_add_int(doc, options, "temperature", 0);
    yyjson_mut_obj_add_int(doc, options, "num_predict", max_tokens());
    yyjson_mut_obj_add_bool(doc, body, "stream", false);
}

/* OpenAI-compatible chat completions: a JSON schema response when enabled,
 * otherwise one required submit_plan function call. */
static void openai_body(yyjson_mut_doc *doc, yyjson_mut_val *body, yyjson_mut_val *schema, bool json_schema)
{
    yyjson_mut_obj_add_int(doc, body, "temperature", 0);
    yyjson_mut_obj_add_int(doc, body, "max_tokens", max_tokens());
    yyjson_mut_obj_add_bool(doc, body, "stream", false);
    if (json_schema) {
        yyjson_mut_val *format = yyjson_mut_obj_add_obj(doc, body, "response_format");
        yyjson_mut_obj_add_str(doc, format, "type", "json_schema");
        yyjson_mut_val *spec = yyjson_mut_obj_add_obj(doc, format, "json_schema");
        yyjson_mut_obj_add_str(doc, spec, "name", "submit_plan");
        yyjson_mut_obj_add_bool(doc, spec, "strict", true);
        yyjson_mut_obj_add_val(doc, spec, "schema", schema);
        return;
    }
    yyjson_mut_obj_add_str(doc, body, "reasoning_effort", "low");
    yyjson_mut_val *tools = yyjson_mut_obj_add_arr(doc, body, "tools");
    yyjson_mut_val *tool = yyjson_mut_arr_add_obj(doc, tools);
    yyjson_mut_obj_add_str(doc, tool, "type", "function");
    yyjson_mut_val *function = yyjson_mut_obj_add_obj(doc, tool, "function");
    yyjson_mut_obj_add_str(doc, function, "name", "submit_plan");
    yyjson_mut_obj_add_str(doc, function, "description", "Submit one complete editor proposal");
    yyjson_mut_obj_add_bool(doc, function, "strict", false);
    yyjson_mut_obj_add_val(doc, function, "parameters", schema);
    yyjson_mut_val *choice = yyjson_mut_obj_add_obj(doc, body, "tool_choice");
    yyjson_mut_obj_add_str(doc, choice, "type", "function");
    yyjson_mut_val *choice_function = yyjson_mut_obj_add_obj(doc, choice, "function");
    yyjson_mut_obj_add_str(doc, choice_function, "name", "submit_plan");
    yyjson_mut_obj_add_bool(doc, body, "parallel_tool_calls", false);
}

static char *request_body(yyjson_val *packet, const char *prompt, bool ollama)
{
    const char *command = command_string(packet, "command");
    bool json_schema = model_json_schema && !ollama;
    yyjson_doc *prompt_doc = yyjson_read(prompt, strlen(prompt), 0);
    yyjson_val *schema_catalog = yyjson_obj_get(yyjson_doc_get_root(prompt_doc), "catalog");
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *body = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, body);
    yyjson_mut_obj_add_str(doc, body, "model", model_name);
    yyjson_mut_val *messages = yyjson_mut_obj_add_arr(doc, body, "messages");
    const char
        *output = ollama || json_schema
                    ? "Return the JSON proposal directly in assistant content."
                    : "Call submit_plan exactly once with the complete "
                      "proposal. Do "
                      "not call any other tool or provide shell commands.";
    size_t system_size = strlen(instructions()) + strlen(output) + 2;
    char *system = xmalloc(system_size);
    snprintf(system, system_size, "%s %s", instructions(), output);
    add_message(doc, messages, "system", system);
    free(system);
    yyjson_mut_val *schema = model_schema(doc, allows_duplicate(command), schema_catalog, command);
    if (ollama) {
        ollama_body(doc, body, messages, schema);
    }
    add_message(doc, messages, "user", prompt);
    if (!ollama) {
        openai_body(doc, body, schema, json_schema);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(prompt_doc);
    return json;
}

char *editor_command_model_prepare(const char *packet_json, char *error, size_t error_size)
{
    initialize_settings();
    bool gpt_oss = !strncmp(model_name, "gpt-oss", 7);
    char *quick = gpt_oss ? editor_command_quick_edit(packet_json) : NULL;
    if (quick) {
        return immediate_response(quick);
    }
    char *policy = gpt_oss ? policy_for_packet(packet_json) : NULL;
    if (policy) {
        return policy;
    }
    yyjson_doc *packet_doc = NULL;
    yyjson_val *packet = packet_root(packet_json, &packet_doc, error, error_size);
    if (!packet) {
        return NULL;
    }
    char endpoint[MODEL_ENDPOINT_SIZE];
    if (!append_endpoint(model_host, endpoint, sizeof endpoint)) {
        set_error(error, error_size, "Invalid model host");
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    bool ollama = ollama_api_endpoint(endpoint);
    char *prompt = prompt_for_packet(packet, ollama || model_json_schema, error, error_size);
    if (!prompt) {
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    char *body = request_body(packet, prompt, ollama);
    yyjson_doc_free(packet_doc);
    free(prompt);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "url", endpoint);
    yyjson_mut_obj_add_str(doc, envelope, "body", body);
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    free(body);
    return result;
}

static yyjson_val *catalog_object(yyjson_val *catalog, const char *id)
{
    yyjson_val *objects = yyjson_obj_get(catalog, "objects");
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        if (!strcmp(command_string(object, "id"), id)) {
            return object;
        }
    }
    return NULL;
}

static yyjson_val *catalog_field(yyjson_val *object, const char *id)
{
    yyjson_val *fields = yyjson_obj_get(object, "fields");
    size_t i, count;
    yyjson_val *field;
    yyjson_arr_foreach(fields, i, count, field)
    {
        if (!strcmp(command_string(field, "id"), id)) {
            return field;
        }
    }
    return NULL;
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool explicit_hex(const char *request, unsigned *value, int *digits)
{
    int matches = 0;
    for (const char *at = request; *at; at++) {
        if (*at != '#') {
            continue;
        }
        unsigned parsed = 0;
        int count = 0;
        while (count < 8 && hex_digit(at[count + 1]) >= 0) {
            parsed = (parsed << 4) | (unsigned)hex_digit(at[++count]);
        }
        if ((count != 6 && count != 8) || hex_digit(at[count + 1]) >= 0) {
            continue;
        }
        *value = parsed;
        *digits = count;
        matches++;
    }
    return matches == 1;
}

static bool named_color(yyjson_val *field, const char *request, unsigned *value)
{
    if (text_has_phrase(request, "alpha") || text_has_phrase(request, "opacity") ||
        text_has_phrase(request, "transparent") ||
        text_has_phrase(request, "translucent")) {
        return false;
    }
    static const struct
    {
        const char *name;
        unsigned rgb;
    } colors[] = { { "black", 0x000000 }, { "white", 0xffffff },
                   { "red", 0xff0000 },   { "green", 0x00ff00 },
                   { "blue", 0x0000ff },  { "yellow", 0xffff00 },
                   { "cyan", 0x00ffff },  { "magenta", 0xff00ff },
                   { "gray", 0x808080 },  { "grey", 0x808080 } };
    int matches = 0;
    unsigned rgb = 0;
    for (size_t i = 0; i < sizeof colors / sizeof colors[0]; i++) {
        if (text_has_phrase(request, colors[i].name)) {
            rgb = colors[i].rgb;
            matches++;
        }
    }
    yyjson_val *old = yyjson_obj_get(field, "value");
    if (matches != 1 || !yyjson_is_num(old)) {
        return false;
    }
    *value = (rgb << 8) | ((unsigned)yyjson_get_uint(old) & 255u);
    return true;
}

static yyjson_val *requested_choice(yyjson_val *field, const char *request)
{
    yyjson_val *choices = yyjson_obj_get(field, "choices");
    yyjson_val *best = NULL;
    int best_score = 0;
    size_t i, count;
    yyjson_val *choice;
    yyjson_arr_foreach(choices, i, count, choice)
    {
        const char *label = command_string(choice, "label");
        if (!*label) {
            continue;
        }
        const char *mention = text_find_phrase(request, label);
        if (mention) {
            const char *clause = request;
            for (const char *at = request; at < mention; at++) {
                if (*at == ';' || *at == '.' || *at == ',') {
                    clause = at + 1;
                }
            }
            const char *leave = text_find_phrase(clause, "leave");
            const char *keep = text_find_phrase(clause, "keep");
            if ((leave && leave < mention) || (keep && keep < mention)) {
                continue;
            }
        }
        char phrase[128];
        snprintf(phrase, sizeof phrase, "not %s", label);
        if (text_has_phrase(request, phrase)) {
            continue;
        }
        snprintf(phrase, sizeof phrase, "except %s", label);
        if (text_has_phrase(request, phrase)) {
            continue;
        }
        snprintf(phrase, sizeof phrase, "instead of %s", label);
        if (text_has_phrase(request, phrase)) {
            continue;
        }
        snprintf(phrase, sizeof phrase, "rather than %s", label);
        if (text_has_phrase(request, phrase)) {
            continue;
        }
        snprintf(phrase, sizeof phrase, "from %s", label);
        if (text_has_phrase(request, phrase)) {
            continue;
        }
        bool mentioned = text_has_phrase(request, label);
        bool collider = text_has_phrase(request, "collider") ||
                        text_has_phrase(request, "collision") ||
                        text_has_phrase(request, "hitbox") ||
                        text_has_phrase(request, "hit area") ||
                        text_has_phrase(request, "hit region") ||
                        text_has_phrase(request, "collision boundary");
        bool round = !strcasecmp(label, "Circle") && collider &&
                     (text_has_phrase(request, "circular") ||
                      text_has_phrase(request, "round"));
        bool rectangular = !strcasecmp(label, "Box") && collider &&
                           (text_has_phrase(request, "rectangular") ||
                            text_has_phrase(request, "rectangle"));
        if (!mentioned && !round && !rectangular) {
            continue;
        }
        snprintf(phrase, sizeof phrase, "to %s", label);
        int score = text_has_phrase(request, phrase) ? 3
                  : round || rectangular             ? 2
                                                     : 1;
        snprintf(phrase, sizeof phrase, "as %s", label);
        if (text_has_phrase(request, phrase)) {
            score = 3;
        }
        snprintf(phrase, sizeof phrase, "%s instead", label);
        if (text_has_phrase(request, phrase) && score < 2) {
            score = 2;
        }
        if (score > best_score) {
            best = choice;
            best_score = score;
        } else if (score == best_score) {
            best = NULL;
        }
    }
    return best;
}

static bool unlisted_replacement(yyjson_val *field, const char *request)
{
    if (strcmp(command_string(field, "type"), "choice") ||
        !text_has_phrase(request, "replace")) {
        return false;
    }
    const char *with = text_find_phrase(request, "with");
    if (!with) {
        return false;
    }
    with += 4;
    yyjson_val *choices = yyjson_obj_get(field, "choices");
    size_t i, count;
    yyjson_val *choice;
    yyjson_arr_foreach(choices, i, count, choice)
    {
        if (text_has_phrase(with, command_string(choice, "label"))) {
            return false;
        }
    }
    return true;
}

static bool specified_number(const char *request)
{
    static const char *const words[] = { "one",    "two",   "three", "four",
                                         "five",   "six",   "seven", "eight",
                                         "nine",   "ten",   "half",  "quarter",
                                         "double", "twice", "triple" };
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        if (text_has_phrase(request, words[i])) {
            return true;
        }
    }
    for (const char *at = request; *at; at++) {
        if (*at == '#') {
            while (hex_digit(at[1]) >= 0) {
                at++;
            }
            continue;
        }
        if (isdigit((unsigned char)*at)) {
            return true;
        }
    }
    return false;
}

static bool specified_color(const char *request)
{
    static const char *const names[] = { "black", "white",  "red",    "green",
                                         "blue",  "yellow", "cyan",   "magenta",
                                         "gray",  "grey",   "orange", "purple",
                                         "pink",  "teal",   "lime",   "amber",
                                         "coral", "ivory" };
    if (strchr(request, '#')) {
        return true;
    }
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (text_has_phrase(request, names[i])) {
            return true;
        }
    }
    return false;
}

static bool extra_dimension(yyjson_val *field, const char *request)
{
    const char *name = command_string(field, "name");
    bool width = text_has_phrase(request, "width") ||
                 text_has_phrase(request, "wide") ||
                 text_has_phrase(request, "widen") ||
                 text_has_phrase(request, "span") ||
                 text_has_phrase(request, "across");
    bool height = text_has_phrase(request, "height") ||
                  text_has_phrase(request, "high") ||
                  text_has_phrase(request, "tall");
    return (!strcmp(name, "height") && width && !height) ||
           (!strcmp(name, "width") && height && !width);
}

static bool extra_shape(yyjson_val *field, const char *request)
{
    if (strcmp(command_string(field, "name"), "shape")) {
        return false;
    }
    bool collision = text_has_phrase(request, "collider") ||
                     text_has_phrase(request, "collision") ||
                     text_has_phrase(request, "hitbox") ||
                     text_has_phrase(request, "hit area") ||
                     text_has_phrase(request, "hit region");
    bool appearance = text_has_phrase(request, "appearance") ||
                      text_has_phrase(request, "sprite") ||
                      text_has_phrase(request, "visual");
    return collision && !appearance &&
           !strcmp(command_string(field, "component"), "GameAppearance");
}

static bool missing_set_value(yyjson_val *field, const char *request)
{
    const char *type = command_string(field, "type");
    return (!strcmp(type, "number") && !specified_number(request)) ||
           (!strcmp(type, "color") && !specified_color(request));
}

static bool normalize_operation(
    yyjson_mut_doc *doc,
    yyjson_mut_val *operation,
    yyjson_val *catalog,
    const char *request
)
{
    const char *op = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "op"));
    if (!op) {
        op = "";
    }
    if (!strcmp(op, "connect")) {
        const char *destination = yyjson_mut_get_str(
            yyjson_mut_obj_get(operation, "destination")
        );
        const char *pin = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "pin"));
        const char *to = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "to"));
        const char *input = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "input"));
        if (!destination && to) {
            yyjson_mut_obj_add_str(doc, operation, "destination", to);
            yyjson_mut_obj_remove_key(operation, "to");
        }
        if (!pin && input) {
            yyjson_mut_obj_add_str(doc, operation, "pin", input);
            yyjson_mut_obj_remove_key(operation, "input");
        }
    }
    if (!strcmp(op, "set") || !strcmp(op, "assert")) {
        const char *target = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "target"));
        const char *field_id = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "field"));
        yyjson_val *object = catalog_object(catalog, target ? target : "");
        if (object && !strcmp(command_string(object, "kind"), "entity") &&
            !request_instance(request)) {
            const char *prototype_name = command_string(object, "prototype");
            yyjson_val *objects = yyjson_obj_get(catalog, "objects");
            size_t i, count;
            yyjson_val *candidate;
            yyjson_arr_foreach(objects, i, count, candidate)
            {
                if (!strcmp(command_string(candidate, "kind"), "prototype") &&
                    !strcmp(command_string(candidate, "name"), prototype_name)) {
                    object = candidate;
                    yyjson_mut_obj_remove_key(operation, "target");
                    yyjson_mut_obj_add_strcpy(doc, operation, "target", command_string(object, "id"));
                    break;
                }
            }
        }
        yyjson_val *field = object ? catalog_field(object, field_id ? field_id : "")
                                   : NULL;
        yyjson_mut_val *value = yyjson_mut_obj_get(operation, "value");
        yyjson_val *choice = field ? requested_choice(field, request) : NULL;
        if (choice) {
            yyjson_mut_obj_remove_key(operation, "value");
            yyjson_mut_obj_add_val(
                doc,
                operation,
                "value",
                yyjson_val_mut_copy(doc, yyjson_obj_get(choice, "value"))
            );
            value = yyjson_mut_obj_get(operation, "value");
        }
        unsigned literal;
        int digits;
        if (field && !strcmp(command_string(field, "type"), "color") &&
            explicit_hex(request, &literal, &digits)) {
            unsigned alpha = (unsigned)yyjson_get_uint(yyjson_obj_get(field, "value")) &
                             255u;
            yyjson_mut_obj_remove_key(operation, "value");
            yyjson_mut_obj_add_uint(doc, operation, "value", digits == 8 ? literal : (literal << 8) | alpha);
            yyjson_mut_obj_remove_key(operation, "alpha");
            return true;
        }
        if (field && !strcmp(command_string(field, "type"), "color") &&
            named_color(field, request, &literal)) {
            yyjson_mut_obj_remove_key(operation, "value");
            yyjson_mut_obj_add_uint(doc, operation, "value", literal);
            yyjson_mut_obj_remove_key(operation, "alpha");
            return true;
        }
        if (field && !strcmp(command_string(field, "type"), "color") &&
            yyjson_mut_is_str(value)) {
            const char *text = yyjson_mut_get_str(value);
            size_t length = strlen(text);
            if ((length != 7 && length != 9) || text[0] != '#') {
                return false;
            }
            unsigned packed = 0;
            for (size_t i = 0; i < length - 1; i++) {
                int digit = hex_digit(text[i + 1]);
                if (digit < 0) {
                    return false;
                }
                packed = (packed << 4) | (unsigned)digit;
            }
            yyjson_mut_val *alpha_value = yyjson_mut_obj_get(operation, "alpha");
            if (length == 9 && yyjson_mut_is_num(alpha_value) &&
                yyjson_mut_get_uint(alpha_value) != (packed & 255u)) {
                return false;
            }
            unsigned alpha = yyjson_mut_is_num(alpha_value)
                               ? (unsigned)yyjson_mut_get_uint(alpha_value)
                               : (unsigned)(yyjson_get_uint(yyjson_obj_get(field, "value")) &
                                            255u);
            if (alpha > 255) {
                return false;
            }
            yyjson_mut_obj_remove_key(operation, "value");
            yyjson_mut_obj_add_uint(
                doc,
                operation,
                "value",
                length == 9 ? packed : ((uint64_t)packed << 8) | alpha
            );
            yyjson_mut_obj_remove_key(operation, "alpha");
        }
    }
    return true;
}

static char *trimmed_json(const char *text)
{
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    if (!strncmp(text, "```json", 7)) {
        text += 7;
        while (*text && isspace((unsigned char)*text)) {
            text++;
        }
        size_t length = strlen(text);
        while (length && isspace((unsigned char)text[length - 1])) {
            length--;
        }
        if (length >= 3 && !strncmp(text + length - 3, "```", 3)) {
            length -= 3;
        }
        char *result = xmalloc(length + 1);
        memcpy(result, text, length);
        result[length] = 0;
        return result;
    }
    return xstrdup(text);
}

static char *model_error(yyjson_val *root, char *error, size_t error_size)
{
    yyjson_val *error_value = yyjson_obj_get(root, "error");
    const char *message = yyjson_get_str(error_value);
    if (!message && yyjson_is_obj(error_value)) {
        message = yyjson_get_str(yyjson_obj_get(error_value, "message"));
    }
    if (!message || !*message) {
        message = command_string(root, "message");
    }
    set_error(error, error_size, *message ? message : "Model host returned an invalid response");
    return NULL;
}

static char *provider_content(yyjson_val *message)
{
    yyjson_val *calls = yyjson_obj_get(message, "tool_calls");
    if (yyjson_arr_size(calls)) {
        if (yyjson_arr_size(calls) != 1) {
            return NULL;
        }
        yyjson_val *function = yyjson_obj_get(yyjson_arr_get(calls, 0), "function");
        if (strcmp(command_string(function, "name"), "submit_plan")) {
            return NULL;
        }
        yyjson_val *arguments = yyjson_obj_get(function, "arguments");
        if (yyjson_is_str(arguments)) {
            return xstrdup(yyjson_get_str(arguments));
        }
        return yyjson_is_obj(arguments) ? yyjson_val_write(arguments, 0, NULL) : NULL;
    }
    const char *content = yyjson_get_str(yyjson_obj_get(message, "content"));
    if (content && *content) {
        return xstrdup(content);
    }
    return NULL;
}

char *editor_command_model_result(const char *packet_json, const char *response, char *error, size_t error_size)
{
    yyjson_doc *local_doc = response ? yyjson_read(response, strlen(response), 0)
                                     : NULL;
    yyjson_val *local = yyjson_doc_get_root(local_doc);
    bool native = (!strcmp(command_string(local, "provider"), "quick_edit") &&
                   !strcmp(command_string(local, "status"), "proposed")) ||
                  (!strcmp(command_string(local, "provider"), "native") &&
                   (!strcmp(command_string(local, "status"), "needs_clarification") ||
                    !strcmp(command_string(local, "status"), "unsupported")));
    yyjson_doc_free(local_doc);
    if (native) {
        return xstrdup(response);
    }
    if (!response) {
        set_error(error, error_size, "Model host returned no response");
        return NULL;
    }
    yyjson_doc *packet_doc = NULL;
    yyjson_val *packet = packet_root(packet_json, &packet_doc, error, error_size);
    if (!packet) {
        return NULL;
    }
    yyjson_doc *provider_doc = yyjson_read(response, strlen(response), 0);
    yyjson_val *provider = provider_doc ? yyjson_doc_get_root(provider_doc) : NULL;
    yyjson_val *message = yyjson_obj_get(provider, "message");
    if (!message) {
        yyjson_val *choices = yyjson_obj_get(provider, "choices");
        yyjson_val *choice = yyjson_is_arr(choices) ? yyjson_arr_get(choices, 0) : NULL;
        message = yyjson_obj_get(choice, "message");
    }
    char *content = provider_content(message);
    if (!content || !*content) {
        if (getenv("CHIBI_COMMAND_DEBUG_PROPOSAL")) {
            fprintf(stderr, "Model response without content: %s\n", response);
        }
        char *result = model_error(provider, error, error_size);
        free(content);
        yyjson_doc_free(provider_doc);
        yyjson_doc_free(packet_doc);
        return result;
    }
    char *json = trimmed_json(content);
    yyjson_doc *proposal_doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *proposal = proposal_doc ? yyjson_doc_get_root(proposal_doc) : NULL;
    if (!yyjson_is_obj(proposal)) {
        if (getenv("CHIBI_COMMAND_DEBUG_PROPOSAL")) {
            fprintf(stderr, "Invalid model content: %s\n", content);
        }
        set_error(error, error_size, "Model returned invalid command JSON");
        free(content);
        free(json);
        yyjson_doc_free(proposal_doc);
        yyjson_doc_free(provider_doc);
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    yyjson_val *catalog = yyjson_obj_get(packet, "catalog");
    char *request = resolved_request(packet);
    const char *decision = command_string(proposal, "decision");
    const char *unsupported = command_string(proposal, "unsupported");
    yyjson_mut_doc *result_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(result_doc);
    yyjson_mut_doc_set_root(result_doc, result);
    yyjson_mut_obj_add_bool(result_doc, result, "ok", true);
    yyjson_mut_obj_add_str(result_doc, result, "provider", model_name);
    if (yyjson_obj_get(provider, "total_duration")) {
        yyjson_mut_val *metrics = yyjson_mut_obj_add_obj(result_doc, result, "provider_metrics");
        yyjson_mut_obj_add_uint(
            result_doc,
            metrics,
            "total_ms",
            yyjson_get_uint(yyjson_obj_get(provider, "total_duration")) / 1000000
        );
        yyjson_mut_obj_add_uint(
            result_doc,
            metrics,
            "load_ms",
            yyjson_get_uint(yyjson_obj_get(provider, "load_duration")) / 1000000
        );
        yyjson_mut_obj_add_uint(
            result_doc,
            metrics,
            "prompt_ms",
            yyjson_get_uint(yyjson_obj_get(provider, "prompt_eval_duration")) / 1000000
        );
        yyjson_mut_obj_add_uint(
            result_doc,
            metrics,
            "output_ms",
            yyjson_get_uint(yyjson_obj_get(provider, "eval_duration")) / 1000000
        );
        yyjson_mut_obj_add_uint(
            result_doc,
            metrics,
            "prompt_tokens",
            yyjson_get_uint(yyjson_obj_get(provider, "prompt_eval_count"))
        );
        yyjson_mut_obj_add_uint(
            result_doc,
            metrics,
            "cached_prompt_tokens",
            yyjson_get_uint(yyjson_obj_get(provider, "prompt_eval_cached_count"))
        );
        yyjson_mut_obj_add_uint(
            result_doc,
            metrics,
            "output_tokens",
            yyjson_get_uint(yyjson_obj_get(provider, "eval_count"))
        );
    }
    yyjson_mut_val *operations = yyjson_mut_obj_add_arr(result_doc, result, "operations");
    yyjson_val *proposed_operations = yyjson_obj_get(proposal, "operations");
    bool valid = !strcmp(decision, "act") || !strcmp(decision, "ask") ||
                 !strcmp(decision, "unsupported");
    if (!valid) {
        set_error(error, error_size, "Model returned an unknown decision");
    } else if (!strcmp(decision, "unsupported") || *unsupported) {
        yyjson_mut_obj_add_str(result_doc, result, "status", "unsupported");
        yyjson_mut_obj_add_str(result_doc, result, "message", *unsupported ? unsupported : "Unsupported editor request");
    } else if (!strcmp(decision, "ask") || *command_string(proposal, "question")) {
        const char *question = command_string(proposal, "question");
        yyjson_mut_obj_add_str(result_doc, result, "status", "needs_clarification");
        yyjson_mut_obj_add_str(result_doc, result, "message", *question ? question : "Choose an editor object");
        yyjson_val *proposed_choices = yyjson_obj_get(proposal, "choices");
        yyjson_mut_val *answer_choices = yyjson_mut_obj_add_arr(result_doc, result, "choices");
        bool invalid_choice = false;
        for (size_t i = 0; i < yyjson_arr_size(proposed_choices); i++) {
            const char *id = yyjson_get_str(yyjson_arr_get(proposed_choices, i));
            yyjson_val *object = id ? catalog_object(catalog, id) : NULL;
            if (!object) {
                invalid_choice = true;
                break;
            }
            yyjson_mut_val *choice = yyjson_mut_arr_add_obj(result_doc, answer_choices);
            char label[256];
            snprintf(
                label,
                sizeof label,
                "%s · %s",
                command_string(object, "name"),
                command_string(object, "kind")
            );
            yyjson_mut_obj_add_str(result_doc, choice, "id", id);
            yyjson_mut_obj_add_strcpy(result_doc, choice, "label", label);
        }
        if (invalid_choice) {
            yyjson_mut_obj_remove_key(result, "choices");
            answer_choices = yyjson_mut_obj_add_arr(result_doc, result, "choices");
        }
        yyjson_mut_obj_add_str(
            result_doc,
            result,
            "binding",
            yyjson_mut_arr_size(answer_choices) ? "target" : "answer"
        );
    } else if (!yyjson_is_arr(proposed_operations) || !yyjson_arr_size(proposed_operations)) {
        set_error(error, error_size, "Model returned no complete editor action");
        valid = false;
    } else {
        bool clarify = false;
        bool filtered = false;
        bool unsupported_choice = false;
        for (size_t i = 0; i < yyjson_arr_size(proposed_operations); i++) {
            yyjson_mut_val *operation = yyjson_val_mut_copy(
                result_doc,
                yyjson_arr_get(proposed_operations, i)
            );
            const char *verb = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "op"));
            if (verb && !strcmp(verb, "select") && !text_has_phrase(request, "select") &&
                !text_has_phrase(request, "highlight") &&
                !text_has_phrase(request, "focus")) {
                continue;
            }
            if (verb && !strcmp(verb, "open") && !text_has_phrase(request, "open") &&
                !text_has_phrase(request, "show") &&
                !text_has_phrase(request, "navigate")) {
                continue;
            }
            if (verb && !strcmp(verb, "assert") && !text_has_phrase(request, "assert") &&
                !text_has_phrase(request, "check") &&
                !text_has_phrase(request, "verify")) {
                yyjson_mut_obj_remove_key(operation, "op");
                yyjson_mut_obj_add_str(result_doc, operation, "op", "set");
            }
            if (!normalize_operation(result_doc, operation, catalog, request)) {
                set_error(error, error_size, "Model returned an invalid color value");
                valid = false;
                break;
            }
            const char *op = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "op"));
            const char *target_id = yyjson_mut_get_str(
                yyjson_mut_obj_get(operation, "target")
            );
            const char *field_id = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "field"));
            yyjson_val *object = target_id ? catalog_object(catalog, target_id) : NULL;
            yyjson_val *field = object && field_id ? catalog_field(object, field_id)
                                                   : NULL;
            if (op && !strcmp(op, "set") && field) {
                if (unlisted_replacement(field, request)) {
                    unsupported_choice = true;
                    break;
                }
                if (extra_dimension(field, request) || extra_shape(field, request)) {
                    filtered = true;
                    continue;
                }
                if (missing_set_value(field, request)) {
                    clarify = true;
                    break;
                }
            }
            bool duplicate = false;
            for (size_t j = 0; j < yyjson_mut_arr_size(operations); j++) {
                yyjson_mut_val *previous = yyjson_mut_arr_get(operations, j);
                const char *op = yyjson_mut_get_str(yyjson_mut_obj_get(operation, "op"));
                const char *prior_op = yyjson_mut_get_str(
                    yyjson_mut_obj_get(previous, "op")
                );
                const char *target = yyjson_mut_get_str(
                    yyjson_mut_obj_get(operation, "target")
                );
                const char *prior_target = yyjson_mut_get_str(
                    yyjson_mut_obj_get(previous, "target")
                );
                const char *field = yyjson_mut_get_str(
                    yyjson_mut_obj_get(operation, "field")
                );
                const char *prior_field = yyjson_mut_get_str(
                    yyjson_mut_obj_get(previous, "field")
                );
                if (!op || !prior_op || strcmp(op, "set") || strcmp(prior_op, "set") ||
                    !target || !prior_target || strcmp(target, prior_target) ||
                    !field || !prior_field || strcmp(field, prior_field)) {
                    continue;
                }
                if (!yyjson_mut_equals(
                        yyjson_mut_obj_get(operation, "value"),
                        yyjson_mut_obj_get(previous, "value")
                    )) {
                    set_error(error, error_size, "Model proposed conflicting values for one field");
                    valid = false;
                    break;
                }
                duplicate = true;
                break;
            }
            if (!valid) {
                break;
            }
            if (!duplicate) {
                yyjson_mut_arr_append(operations, operation);
            }
        }
        if (valid && unsupported_choice) {
            yyjson_mut_obj_remove_key(result, "operations");
            yyjson_mut_obj_add_arr(result_doc, result, "operations");
            yyjson_mut_obj_add_str(result_doc, result, "status", "unsupported");
            yyjson_mut_obj_add_str(result_doc, result, "message", "That property value is not available");
        } else if (valid && (clarify || filtered || !yyjson_mut_arr_size(operations))) {
            yyjson_mut_obj_remove_key(result, "operations");
            yyjson_mut_obj_add_arr(result_doc, result, "operations");
            yyjson_mut_obj_add_str(result_doc, result, "status", "needs_clarification");
            yyjson_mut_obj_add_str(
                result_doc,
                result,
                "message",
                filtered ? "Which fields should change?" : "What value or amount should I use?"
            );
            yyjson_mut_obj_add_str(result_doc, result, "binding", "answer");
        } else if (valid) {
            yyjson_mut_obj_add_str(result_doc, result, "status", "proposed");
            yyjson_mut_obj_add_str(result_doc, result, "message", "Proposed");
            yyjson_mut_obj_add_str(result_doc, result, "revision", command_string(catalog, "revision"));
            yyjson_mut_val *context = yyjson_mut_obj_add_obj(result_doc, result, "context");
            yyjson_mut_obj_add_str(result_doc, context, "document", command_string(catalog, "document"));
            yyjson_mut_obj_add_str(
                result_doc,
                context,
                "operation",
                yyjson_mut_get_str(yyjson_mut_obj_get(
                    yyjson_mut_arr_get(operations, yyjson_mut_arr_size(operations) - 1),
                    "op"
                ))
            );
            yyjson_mut_val *targets = yyjson_mut_obj_add_arr(result_doc, context, "targets");
            for (size_t i = 0; i < yyjson_mut_arr_size(operations); i++) {
                const char *target = yyjson_mut_get_str(
                    yyjson_mut_obj_get(yyjson_mut_arr_get(operations, i), "target")
                );
                if (!target) {
                    continue;
                }
                if (*target && catalog_object(catalog, target)) {
                    bool found = false;
                    for (size_t j = 0; j < yyjson_mut_arr_size(targets); j++) {
                        if (!strcmp(target, yyjson_mut_get_str(yyjson_mut_arr_get(targets, j)))) {
                            found = true;
                        }
                    }
                    if (!found) {
                        yyjson_mut_arr_add_str(result_doc, targets, target);
                    }
                }
            }
        }
    }
    char *output = valid ? yyjson_mut_write(result_doc, 0, NULL) : NULL;
    if (!output && valid) {
        set_error(error, error_size, "Could not encode the command result");
    }
    yyjson_mut_doc_free(result_doc);
    free(content);
    free(json);
    yyjson_doc_free(proposal_doc);
    yyjson_doc_free(provider_doc);
    yyjson_doc_free(packet_doc);
    free(request);
    return output;
}
