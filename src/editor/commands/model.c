#include "editor/commands/model.h"

#include "editor/commands/internal.h"
#include "editor/commands/laya.h"
#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define MODEL_HOST_SIZE 256
#define MODEL_NAME_SIZE 128
#define MODEL_ENDPOINT_SIZE 512
#define MODEL_ERROR_SIZE 256
#define MODEL_DEFAULT_MAX_TOKENS 800

static char model_host[MODEL_HOST_SIZE];
static char model_name[MODEL_NAME_SIZE];
static bool model_initialized;

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
        getenv("CHIBI_MODEL_URL") ? getenv("CHIBI_MODEL_URL") : "http://127.0.0.1:8768/v1/decide"
    );
    snprintf(model_name, sizeof model_name, "%s", getenv("CHIBI_MODEL") ? getenv("CHIBI_MODEL") : "laya");
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
    if (strpbrk(host, "\r\n") || strpbrk(model, "\r\n")) {
        set_error(error, error_size, "Host and model must be single lines");
        return false;
    }
    if (strncmp(host, "https://", 8) == 0) {
        set_error(error, error_size, "Use an HTTP model host");
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

static yyjson_mut_val *schema_operation(yyjson_mut_doc *doc)
{
    static const char *const verbs[] = { "set",     "assert", "duplicate",
                                         "connect", "open",   "select",
                                         "frame",   "panel",  "runtime",
                                         "save",    "undo",   "redo" };
    yyjson_mut_val *operation = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, operation, "type", "object");
    yyjson_mut_obj_add_bool(doc, operation, "additionalProperties", false);
    yyjson_mut_val *required = yyjson_mut_obj_add_arr(doc, operation, "required");
    const char *required_names[] = { "op",    "target", "field",
                                     "value", "alpha",  "destination",
                                     "pin",   "result", "result_target" };
    for (int i = 0; i < (int)(sizeof required_names / sizeof required_names[0]); i++) {
        yyjson_mut_arr_add_str(doc, required, required_names[i]);
    }
    yyjson_mut_val *properties = yyjson_mut_obj_add_obj(doc, operation, "properties");
    yyjson_mut_obj_add_val(
        doc,
        properties,
        "op",
        schema_string(doc, verbs, (int)(sizeof verbs / sizeof verbs[0]))
    );
    const char *string_names[] = { "target", "field",  "destination",
                                   "pin",    "result", "result_target" };
    for (int i = 0; i < (int)(sizeof string_names / sizeof string_names[0]); i++) {
        yyjson_mut_obj_add_val(doc, properties, string_names[i], schema_string(doc, NULL, 0));
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, properties, "value");
    yyjson_mut_val *value_types = yyjson_mut_obj_add_arr(doc, value, "anyOf");
    const char *value_type_names[] = { "string", "boolean", "integer", "number", "null" };
    for (int i = 0; i < (int)(sizeof value_type_names / sizeof value_type_names[0]);
         i++) {
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

static yyjson_mut_val *model_schema(yyjson_mut_doc *doc)
{
    static const char *const decisions[] = { "act", "ask", "unsupported" };
    yyjson_mut_val *schema = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, schema, "type", "object");
    yyjson_mut_obj_add_bool(doc, schema, "additionalProperties", false);
    yyjson_mut_val *required = yyjson_mut_obj_add_arr(doc, schema, "required");
    const char *required_names[] = { "decision", "operations", "question", "choices", "unsupported" };
    for (int i = 0; i < (int)(sizeof required_names / sizeof required_names[0]); i++) {
        yyjson_mut_arr_add_str(doc, required, required_names[i]);
    }
    yyjson_mut_val *properties = yyjson_mut_obj_add_obj(doc, schema, "properties");
    yyjson_mut_obj_add_val(doc, properties, "decision", schema_string(doc, decisions, 3));
    yyjson_mut_val *operations = yyjson_mut_obj_add_obj(doc, properties, "operations");
    yyjson_mut_obj_add_str(doc, operations, "type", "array");
    yyjson_mut_obj_add_int(doc, operations, "maxItems", 64);
    yyjson_mut_obj_add_val(doc, operations, "items", schema_operation(doc));
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

static bool text_has_phrase(const char *text, const char *phrase)
{
    size_t length = strlen(phrase);
    if (!length) {
        return false;
    }
    for (const char *start = text; *start; start++) {
        size_t i = 0;
        while (i < length && start[i] &&
               tolower((unsigned char)start[i]) == tolower((unsigned char)phrase[i])) {
            i++;
        }
        if (i == length && (start == text || !isalnum((unsigned char)start[-1])) &&
            !isalnum((unsigned char)start[i])) {
            return true;
        }
    }
    return false;
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
    const char *const keys[] = { "revision",     "document", "playing",
                                 "capabilities", "panels",   "graph_hash" };
    for (int i = 0; i < (int)(sizeof keys / sizeof keys[0]); i++) {
        yyjson_val *value = yyjson_obj_get(catalog, keys[i]);
        if (value) {
            yyjson_mut_obj_add_val(doc, root, keys[i], yyjson_val_mut_copy(doc, value));
        }
    }
    yyjson_mut_val *objects = yyjson_mut_obj_add_arr(doc, root, "objects");
    yyjson_val *source_objects = yyjson_obj_get(catalog, "objects");
    bool instance = text_has_phrase(request, "instance") ||
                    text_has_phrase(request, "this entity") ||
                    text_has_phrase(request, "this object");
    bool focused = false;
    size_t i, count;
    yyjson_val *source;
    yyjson_arr_foreach(source_objects, i, count, source)
    {
        if (!text_has_phrase(request, command_string(source, "name"))) {
            continue;
        }
        focused = true;
        bool entity = !strcmp(command_string(source, "kind"), "entity");
        model_add_object(doc, objects, source, !entity || instance, NULL);
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
        if (!yyjson_mut_arr_size(objects)) {
            yyjson_mut_doc_free(doc);
            return NULL;
        }
    }
    return doc;
}

static char *prompt_for_packet(yyjson_val *packet, bool ollama, char *error, size_t error_size)
{
    yyjson_val *catalog = yyjson_obj_get(packet, "catalog");
    const char *command = command_string(packet, "command");
    if (!yyjson_is_obj(catalog) || !*command) {
        set_error(error, error_size, "Command packet is missing its catalog or command");
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_doc *focused_catalog = ollama ? model_catalog(catalog, command) : NULL;
    yyjson_mut_obj_add_val(
        doc,
        root,
        "catalog",
        focused_catalog
            ? yyjson_mut_val_mut_copy(doc, yyjson_mut_doc_get_root(focused_catalog))
            : yyjson_val_mut_copy(doc, catalog)
    );
    yyjson_mut_obj_add_str(doc, root, "request", command);
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
           "follow-ups and "
           "reply values for a previous clarification. Numeric values are JSON "
           "numbers; "
           "booleans are JSON booleans. Colors may be #RRGGBB with optional "
           "alpha 0-255. "
           "A color without alpha preserves the observed alpha. Use exact "
           "listed choice "
           "values. A set uses a final absolute value calculated from the "
           "observed value. "
           "Only entity objects can be selected, framed, or duplicated. To "
           "edit a copy, "
           "duplicate with result, then use the original target and that "
           "result_target. "
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
           "essential information is missing. A clear request supported by the "
           "catalog "
           "is act, not ask or unsupported. Do not explain successful "
           "operations. "
           "When a request names a scene entity, edit its prototype by default "
           "so the "
           "change applies to all instances; only edit the scene instance if "
           "the user "
           "explicitly asks for that instance. For an unqualified color "
           "request, use "
           "Appearance Color rather than border, glow, or material colors. "
           "Return all five proposal fields: "
           "decision, operations, question, choices, and unsupported.";
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

char *editor_command_model_prepare(const char *packet_json, char *error, size_t error_size)
{
    initialize_settings();
    if (!strcmp(model_name, "laya")) {
        return editor_command_laya_prepare(packet_json, model_host, error, error_size);
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
    char *prompt = prompt_for_packet(packet, ollama, error, error_size);
    yyjson_doc_free(packet_doc);
    if (!prompt) {
        return NULL;
    }
    yyjson_mut_doc *body_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *body = yyjson_mut_obj(body_doc);
    yyjson_mut_doc_set_root(body_doc, body);
    yyjson_mut_obj_add_str(body_doc, body, "model", model_name);
    yyjson_mut_val *messages = yyjson_mut_obj_add_arr(body_doc, body, "messages");
    const char *output_instruction =
        ollama ? "Return the JSON proposal directly in assistant content." : "Call submit_plan exactly once with the complete proposal. Do not call any other tool or provide shell commands.";
    size_t system_size = strlen(instructions()) + strlen(output_instruction) + 2;
    char *system_content = xmalloc(system_size);
    snprintf(system_content, system_size, "%s %s", instructions(), output_instruction);
    add_message(body_doc, messages, "system", system_content);
    free(system_content);
    if (ollama) {
        add_message(body_doc, messages, "user", "{\"request\":\"Change Scout's color to blue\",\"catalog\":{\"revision\":\"example\",\"document\":\"scene-example\",\"capabilities\":[\"set\"],\"panels\":[],\"objects\":[{\"id\":\"entity:example:1\",\"name\":\"Scout\",\"kind\":\"entity\",\"prototype\":\"DemoShip\"},{\"id\":\"/example/content/prototypes/DemoShip.json\",\"name\":\"DemoShip\",\"kind\":\"prototype\",\"fields\":[{\"id\":\"GAME.GameAppearance.color\",\"name\":\"color\",\"label\":\"Appearance Color\",\"type\":\"color\",\"value\":4294967295}]}]}}");
        add_message(body_doc, messages, "assistant", "{\"decision\":\"act\",\"operations\":[{\"op\":\"set\",\"target\":\"/example/content/prototypes/DemoShip.json\",\"field\":\"GAME.GameAppearance.color\",\"value\":\"#0000FF\",\"alpha\":null,\"destination\":\"\",\"pin\":\"\",\"result\":\"\",\"result_target\":\"\"}],\"question\":\"\",\"choices\":[],\"unsupported\":\"\"}");
    }
    add_message(body_doc, messages, "user", prompt);
    if (ollama) {
        yyjson_mut_obj_add_str(body_doc, body, "think", "low");
        yyjson_mut_obj_add_str(body_doc, body, "keep_alive", "30m");
        yyjson_mut_obj_add_val(body_doc, body, "format", model_schema(body_doc));
        yyjson_mut_val *options = yyjson_mut_obj_add_obj(body_doc, body, "options");
        yyjson_mut_obj_add_int(body_doc, options, "temperature", 0);
        yyjson_mut_obj_add_int(body_doc, options, "num_predict", max_tokens());
        yyjson_mut_obj_add_bool(body_doc, body, "stream", false);
    } else {
        yyjson_mut_obj_add_int(body_doc, body, "temperature", 0);
        yyjson_mut_obj_add_int(body_doc, body, "max_tokens", max_tokens());
        yyjson_mut_obj_add_str(body_doc, body, "reasoning_effort", "low");
        yyjson_mut_obj_add_bool(body_doc, body, "stream", false);
        yyjson_mut_val *tools = yyjson_mut_obj_add_arr(body_doc, body, "tools");
        yyjson_mut_val *tool = yyjson_mut_arr_add_obj(body_doc, tools);
        yyjson_mut_obj_add_str(body_doc, tool, "type", "function");
        yyjson_mut_val *function = yyjson_mut_obj_add_obj(body_doc, tool, "function");
        yyjson_mut_obj_add_str(body_doc, function, "name", "submit_plan");
        yyjson_mut_obj_add_str(body_doc, function, "description", "Submit one complete editor proposal");
        yyjson_mut_obj_add_bool(body_doc, function, "strict", false);
        yyjson_mut_obj_add_val(body_doc, function, "parameters", model_schema(body_doc));
        yyjson_mut_val *choice = yyjson_mut_obj_add_obj(body_doc, body, "tool_choice");
        yyjson_mut_obj_add_str(body_doc, choice, "type", "function");
        yyjson_mut_val *choice_function = yyjson_mut_obj_add_obj(body_doc, choice, "function");
        yyjson_mut_obj_add_str(body_doc, choice_function, "name", "submit_plan");
        yyjson_mut_obj_add_bool(body_doc, body, "parallel_tool_calls", false);
    }
    char *body_json = yyjson_mut_write(body_doc, 0, NULL);
    yyjson_mut_doc_free(body_doc);
    free(prompt);
    if (!body_json) {
        set_error(error, error_size, "Could not encode the model request");
        return NULL;
    }
    yyjson_mut_doc *envelope_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *envelope = yyjson_mut_obj(envelope_doc);
    yyjson_mut_doc_set_root(envelope_doc, envelope);
    yyjson_mut_obj_add_str(envelope_doc, envelope, "url", endpoint);
    yyjson_mut_obj_add_strcpy(envelope_doc, envelope, "body", body_json);
    char *result = yyjson_mut_write(envelope_doc, 0, NULL);
    yyjson_mut_doc_free(envelope_doc);
    free(body_json);
    if (!result) {
        set_error(error, error_size, "Could not encode the model request");
    }
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

static bool normalize_operation(yyjson_mut_doc *doc, yyjson_mut_val *operation, yyjson_val *catalog)
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
        yyjson_val *field = object ? catalog_field(object, field_id ? field_id : "")
                                   : NULL;
        yyjson_mut_val *value = yyjson_mut_obj_get(operation, "value");
        if (field && !strcmp(command_string(field, "type"), "color") &&
            yyjson_mut_is_str(value)) {
            const char *text = yyjson_mut_get_str(value);
            if (strlen(text) != 7 || text[0] != '#') {
                return false;
            }
            unsigned rgb = 0;
            for (int i = 0; i < 6; i++) {
                int digit = hex_digit(text[i + 1]);
                if (digit < 0) {
                    return false;
                }
                rgb = (rgb << 4) | (unsigned)digit;
            }
            yyjson_mut_val *alpha_value = yyjson_mut_obj_get(operation, "alpha");
            unsigned alpha = yyjson_mut_is_num(alpha_value)
                               ? (unsigned)yyjson_mut_get_uint(alpha_value)
                               : (unsigned)(yyjson_get_uint(yyjson_obj_get(field, "value")) &
                                            255u);
            if (alpha > 255) {
                return false;
            }
            yyjson_mut_obj_remove_key(operation, "value");
            yyjson_mut_obj_add_uint(doc, operation, "value", ((uint64_t)rgb << 8) | alpha);
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
    initialize_settings();
    if (!strcmp(model_name, "laya")) {
        return editor_command_laya_result(packet_json, response, error, error_size);
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
        set_error(error, error_size, "Model returned invalid command JSON");
        free(content);
        free(json);
        yyjson_doc_free(proposal_doc);
        yyjson_doc_free(provider_doc);
        yyjson_doc_free(packet_doc);
        return NULL;
    }
    yyjson_val *catalog = yyjson_obj_get(packet, "catalog");
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
        for (size_t i = 0; i < yyjson_arr_size(proposed_choices); i++) {
            const char *id = yyjson_get_str(yyjson_arr_get(proposed_choices, i));
            yyjson_val *object = id ? catalog_object(catalog, id) : NULL;
            if (!object) {
                set_error(error, error_size, "Model returned an unknown clarification choice");
                valid = false;
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
        yyjson_mut_obj_add_str(
            result_doc,
            result,
            "binding",
            yyjson_arr_size(proposed_choices) ? "target" : "answer"
        );
    } else if (!yyjson_is_arr(proposed_operations) || !yyjson_arr_size(proposed_operations)) {
        set_error(error, error_size, "Model returned no complete editor action");
        valid = false;
    } else {
        for (size_t i = 0; i < yyjson_arr_size(proposed_operations); i++) {
            yyjson_mut_val *operation = yyjson_val_mut_copy(
                result_doc,
                yyjson_arr_get(proposed_operations, i)
            );
            if (!normalize_operation(result_doc, operation, catalog)) {
                set_error(error, error_size, "Model returned an invalid color value");
                valid = false;
                break;
            }
            yyjson_mut_arr_append(operations, operation);
        }
        if (valid) {
            bool review = !getenv("CHIBI_COMMAND_REVIEW") ||
                          strcmp(getenv("CHIBI_COMMAND_REVIEW"), "0");
            yyjson_mut_obj_add_str(result_doc, result, "status", review ? "needs_review" : "ready");
            yyjson_mut_obj_add_str(result_doc, result, "message", review ? "Review changes" : "Ready");
            yyjson_mut_obj_add_str(result_doc, result, "revision", command_string(catalog, "revision"));
            yyjson_mut_val *context = yyjson_mut_obj_add_obj(result_doc, result, "context");
            yyjson_mut_obj_add_str(result_doc, context, "document", command_string(catalog, "document"));
            yyjson_mut_obj_add_str(
                result_doc,
                context,
                "operation",
                command_string(yyjson_arr_get(proposed_operations, yyjson_arr_size(proposed_operations) - 1), "op")
            );
            yyjson_mut_val *targets = yyjson_mut_obj_add_arr(result_doc, context, "targets");
            for (size_t i = 0; i < yyjson_arr_size(proposed_operations); i++) {
                const char *target = command_string(yyjson_arr_get(proposed_operations, i), "target");
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
    return output;
}
