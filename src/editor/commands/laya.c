#include "editor/commands/laya.h"

#include "editor/commands/internal.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <yyjson.h>

#define LAYA_CHOICES 6
#define LAYA_CONFIDENCE 0.8

static char laya_token[257];

bool editor_command_laya_set_token(const char *token)
{
    if (!token || strlen(token) >= sizeof laya_token || strchr(token, '\r') ||
        strchr(token, '\n')) {
        return false;
    }
    snprintf(laya_token, sizeof laya_token, "%s", token);
    return true;
}

typedef struct
{
    yyjson_val *item;
    char label[112];
} laya_option;

typedef struct
{
    const char *binding, *question;
    const char *goal;
    yyjson_val *target;
    laya_option options[LAYA_CHOICES];
    int count;
    bool overflow, unresolved;
} laya_menu;

static bool word(const char *text, const char *name)
{
    if (!text || !name || !*name) {
        return false;
    }
    size_t size = strlen(name);
    for (const char *at = text; *at; at++) {
        if (strncasecmp(at, name, size)) {
            continue;
        }
        bool before = at == text || !isalnum((unsigned char)at[-1]);
        bool after = !isalnum((unsigned char)at[size]);
        if (before && after) {
            return true;
        }
    }
    return false;
}

static bool contains(const char *text, const char *part)
{
    if (!text || !part || !*part) {
        return false;
    }
    size_t size = strlen(part);
    for (const char *at = text; *at; at++) {
        if (!strncasecmp(at, part, size)) {
            return true;
        }
    }
    return false;
}

static bool name_matches(const char *goal, const char *name)
{
    if (word(goal, name)) {
        return true;
    }
    size_t first = 0;
    while (isalnum((unsigned char)name[first]) &&
           (first == 0 || !isupper((unsigned char)name[first]))) {
        first++;
    }
    if (first >= 4 && name[first]) {
        char part[128];
        if (first < sizeof part) {
            memcpy(part, name, first);
            part[first] = 0;
            if (word(goal, part)) {
                return true;
            }
        }
    }
    char compact_goal[1025], compact_name[128];
    size_t used = 0;
    for (const unsigned char *at = (const unsigned char *)goal;
         *at && used + 1 < sizeof compact_goal;
         at++) {
        if (isalnum(*at)) {
            compact_goal[used++] = (char)tolower(*at);
        }
    }
    compact_goal[used] = 0;
    used = 0;
    for (const unsigned char *at = (const unsigned char *)name;
         *at && used + 1 < sizeof compact_name;
         at++) {
        if (isalnum(*at)) {
            compact_name[used++] = (char)tolower(*at);
        }
    }
    compact_name[used] = 0;
    return used >= 8 && strstr(compact_goal, compact_name) != NULL;
}

static void fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
}

static yyjson_val *catalog_prototype(yyjson_val *objects, const char *name)
{
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        if (!strcmp(command_string(object, "kind"), "prototype") &&
            !strcmp(command_string(object, "name"), name)) {
            return object;
        }
    }
    return NULL;
}

static void current_value(yyjson_val *field, char *text, size_t size)
{
    yyjson_val *value = yyjson_obj_get(field, "value");
    const char *type = command_string(field, "type");
    if (!strcmp(type, "color")) {
        snprintf(text, size, "#%08X", (unsigned)yyjson_get_uint(value));
    } else if (!strcmp(type, "choice")) {
        yyjson_val *choices = yyjson_obj_get(field, "choices");
        size_t i, count;
        yyjson_val *choice;
        yyjson_arr_foreach(choices, i, count, choice)
        {
            if (yyjson_equals(value, yyjson_obj_get(choice, "value"))) {
                snprintf(text, size, "%s", command_string(choice, "label"));
                return;
            }
        }
        snprintf(text, size, "%.6g", yyjson_get_num(value));
    } else if (yyjson_is_num(value)) {
        snprintf(text, size, "%.6g", yyjson_get_num(value));
    } else if (yyjson_is_bool(value)) {
        snprintf(text, size, "%s", yyjson_get_bool(value) ? "true" : "false");
    } else {
        snprintf(text, size, "%.24s", yyjson_get_str(value) ? yyjson_get_str(value) : "");
    }
}

static void add(laya_menu *menu, yyjson_val *item, const char *kind, const char *used_by)
{
    const char *id = command_string(item, "id");
    if (!*id) {
        return;
    }
    for (int i = 0; i < menu->count; i++) {
        if (!strcmp(command_string(menu->options[i].item, "id"), id)) {
            return;
        }
    }
    if (menu->count == LAYA_CHOICES) {
        menu->overflow = true;
        return;
    }
    laya_option *option = &menu->options[menu->count++];
    option->item = item;
    if (kind) {
        const char *name = command_string(item, "name");
        const char *effect = "properties";
        if ((word(menu->goal, "particle") || word(menu->goal, "particles")) &&
            (word(menu->goal, "size") || word(menu->goal, "larger") ||
             word(menu->goal, "smaller"))) {
            effect = "particle size";
        } else if (word(menu->goal, "color") || word(menu->goal, "colour")) {
            effect = "color";
        } else if (word(menu->goal, "hitbox") || word(menu->goal, "collider")) {
            effect = "hitbox";
        }
        snprintf(
            option->label,
            sizeof option->label,
            "Change %s %s %s%s%s",
            name,
            kind,
            effect,
            used_by && *used_by       ? ", used by "
            : !strcmp(kind, "entity") ? ", this scene only"
                                      : "",
            used_by && *used_by ? used_by : ""
        );
    } else {
        char value[32];
        current_value(item, value, sizeof value);
        snprintf(option->label, sizeof option->label, "%s, current %s", command_string(item, "label"), value);
    }
}

static bool particle_object(yyjson_val *object)
{
    yyjson_val *components = yyjson_obj_get(object, "components");
    size_t i, count;
    yyjson_val *component;
    yyjson_arr_foreach(components, i, count, component)
    {
        const char *name = yyjson_get_str(component);
        if (name && contains(name, "particle")) {
            return true;
        }
    }
    return false;
}

static yyjson_val *owning_object(yyjson_val *objects, yyjson_val *object, bool instance)
{
    if (instance || strcmp(command_string(object, "kind"), "entity")) {
        return object;
    }
    yyjson_val *prototype = catalog_prototype(objects, command_string(object, "prototype"));
    return prototype ? prototype : object;
}

static bool color_value(const char *text, yyjson_val *field, unsigned *out);

static bool field_matches(const char *goal, yyjson_val *field)
{
    const char *id = command_string(field, "id");
    const char *name = command_string(field, "name");
    const char *type = command_string(field, "type");
    if (yyjson_obj_get(field, "editable") &&
        !yyjson_get_bool(yyjson_obj_get(field, "editable"))) {
        return false;
    }
    unsigned requested_color;
    bool color = word(goal, "color") || word(goal, "colour") ||
                 (!(word(goal, "size") || word(goal, "sizes") ||
                    word(goal, "larger") || word(goal, "smaller")) &&
                  color_value(goal, field, &requested_color));
    if (color) {
        if (strcmp(type, "color")) {
            return false;
        }
        if (word(goal, "border")) {
            return contains(id, "border");
        }
        if (word(goal, "glow")) {
            return contains(id, "glow");
        }
        if (word(goal, "material")) {
            return contains(id, "Material");
        }
        if (word(goal, "particle") || word(goal, "particles")) {
            return contains(id, word(goal, "end") ? "GameParticleEmitter.end_color" : "GameParticleEmitter.color");
        }
        return contains(id, "GameAppearance.color");
    }
    if (word(goal, "hitbox") || word(goal, "collider")) {
        return contains(id, "GameCollider.") &&
               (word(goal, "shape") || word(goal, "circle") || word(goal, "circular") ||
                        word(goal, "round") || word(goal, "rectangle")
                    ? !strcmp(name, "shape")
                    : word(goal, name));
    }
    if (word(goal, "shape")) {
        return !strcmp(name, "shape");
    }
    if (word(goal, "size") || word(goal, "sizes") || word(goal, "larger") ||
        word(goal, "smaller")) {
        if (word(goal, "particle") || word(goal, "particles")) {
            return contains(id, "GameParticleEmitter.size.start.x");
        }
        return word(goal, name) || !strcmp(name, "size") || !strcmp(name, "width") ||
               !strcmp(name, "height") || !strcmp(name, "radius");
    }
    return word(goal, name);
}

static const char *build_menu(yyjson_val *packet, laya_menu *menu)
{
    const char *goal = command_string(packet, "command");
    menu->goal = goal;
    yyjson_val *catalog = yyjson_obj_get(packet, "catalog");
    yyjson_val *objects = yyjson_obj_get(catalog, "objects");
    yyjson_val *bindings = yyjson_obj_get(packet, "bindings");
    const char *target_id = command_string(bindings, "target");
    if (!*goal || !yyjson_is_arr(objects)) {
        return "Command or editor catalog is missing";
    }
    if (word(goal, "if") || word(goal, "when") || word(goal, "unless") ||
        word(goal, "while") || word(goal, "and") || word(goal, "also") ||
        word(goal, "or") || word(goal, "but")) {
        return "Command needs multiple or conditional edits";
    }
    if (*target_id) {
        menu->target = command_find(objects, target_id);
        if (!menu->target) {
            return "Selected editor target is no longer available";
        }
        menu->binding = "field";
        menu->question = "Which property best fulfills the user's request?";
        yyjson_val *fields = yyjson_obj_get(menu->target, "fields");
        size_t i, count;
        yyjson_val *field;
        yyjson_arr_foreach(fields, i, count, field)
        {
            if (field_matches(goal, field)) {
                add(menu, field, NULL, NULL);
            }
        }
        return menu->overflow ? "Too many matching properties; name a component"
             : menu->count    ? NULL
                              : "No matching editable property";
    }
    menu->binding = "target";
    menu->question = "Which editor object should receive the requested change?";
    bool instance = word(goal, "instance") || contains(goal, "only this") ||
                    contains(goal, "just this");
    bool particles = word(goal, "particle") || word(goal, "particles");
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        const char *kind = command_string(object, "kind");
        if (strcmp(kind, "prototype") && strcmp(kind, "entity") &&
            strcmp(kind, "node")) {
            continue;
        }
        if (name_matches(goal, command_string(object, "name"))) {
            yyjson_val *owner = owning_object(objects, object, instance);
            add(menu,
                owner,
                command_string(owner, "kind"),
                owner != object ? command_string(object, "name") : NULL);
        }
    }
    if (!menu->count) {
        yyjson_val *targets = yyjson_obj_get(yyjson_obj_get(packet, "context"), "targets");
        yyjson_val *id;
        yyjson_arr_foreach(targets, i, count, id)
        {
            yyjson_val *found = command_find(objects, yyjson_get_str(id));
            if (found && (!particles || particle_object(found))) {
                yyjson_val *owner = owning_object(objects, found, instance);
                add(menu,
                    owner,
                    command_string(owner, "kind"),
                    owner != found ? command_string(found, "name") : NULL);
            }
        }
    }
    if (!menu->count) {
        yyjson_arr_foreach(objects, i, count, object)
        {
            if (yyjson_get_bool(yyjson_obj_get(object, "selected")) &&
                (!particles || particle_object(object))) {
                yyjson_val *owner = owning_object(objects, object, instance);
                add(menu,
                    owner,
                    command_string(owner, "kind"),
                    owner != object ? command_string(object, "name") : NULL);
            }
        }
    }
    if (!menu->count && particles) {
        menu->unresolved = true;
        yyjson_arr_foreach(objects, i, count, object)
        {
            if (!particle_object(object)) {
                continue;
            }
            yyjson_val *owner = owning_object(objects, object, instance);
            add(menu,
                owner,
                command_string(owner, "kind"),
                owner != object ? command_string(object, "name") : NULL);
        }
    }
    return menu->overflow ? "Too many matching objects; name one target"
         : menu->count    ? NULL
                          : "Name or select an editor object";
}

static const char *target_source(yyjson_val *packet, yyjson_val *target)
{
    if (!target || strcmp(command_string(target, "kind"), "prototype")) {
        return NULL;
    }
    yyjson_val *objects = yyjson_obj_get(yyjson_obj_get(packet, "catalog"), "objects");
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        if (!strcmp(command_string(object, "kind"), "entity") &&
            !strcmp(command_string(object, "prototype"), command_string(target, "name")) &&
            name_matches(command_string(packet, "command"), command_string(object, "name"))) {
            return command_string(object, "name");
        }
    }
    return NULL;
}

static char *local_response(const char *key, const char *message)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_val *payload = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, payload, key, message);
    yyjson_mut_doc_set_root(doc, payload);
    char *body = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "local_response", body);
    free(body);
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

#ifndef __EMSCRIPTEN__
static char *api_key()
{
    if (*laya_token) {
        return xstrdup(laya_token);
    }
    const char *value = getenv("LAYA_API_KEY");
    if (value && *value) {
        return xstrdup(value);
    }
    const char *path = getenv("LAYA_API_KEY_FILE");
    FILE *file = fopen(path && *path ? path : ".chibi/laya-api-key", "r");
    if (!file) {
        return NULL;
    }
    char buffer[257];
    size_t size = fread(buffer, 1, sizeof buffer - 1, file);
    fclose(file);
    buffer[size] = 0;
    while (size && isspace((unsigned char)buffer[size - 1])) {
        buffer[--size] = 0;
    }
    return size ? xstrdup(buffer) : NULL;
}

static bool local_endpoint(const char *url)
{
    const char *host = url + 7;
    const char *names[] = { "127.0.0.1", "localhost" };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t size = strlen(names[i]);
        if (!strncasecmp(host, names[i], size) &&
            (!host[size] || host[size] == ':' || host[size] == '/')) {
            return true;
        }
    }
    return false;
}
#endif

char *editor_command_laya_prepare(const char *packet_json, const char *host, char *error, size_t error_size)
{
    if (!packet_json || !host || !*host) {
        fail(error, error_size, "Invalid Laya command request");
        return NULL;
    }
    char endpoint[512];
    int size = snprintf(endpoint, sizeof endpoint, "%s%s", strncmp(host, "http://", 7) ? "http://" : "", host);
    if (size < 0 || (size_t)size >= sizeof endpoint) {
        fail(error, error_size, "Laya host is too long");
        return NULL;
    }
    if (!strchr(endpoint + 7, '/')) {
        if ((size_t)size + strlen("/v1/decide") >= sizeof endpoint) {
            fail(error, error_size, "Laya host is too long");
            return NULL;
        }
        strcat(endpoint, "/v1/decide");
    }
    yyjson_doc *read = yyjson_read(packet_json, strlen(packet_json), 0);
    yyjson_val *packet = read ? yyjson_doc_get_root(read) : NULL;
    laya_menu menu = { 0 };
    const char *problem = build_menu(packet, &menu);
    if (problem) {
        yyjson_doc_free(read);
        return local_response("local_error", problem);
    }
    if (menu.unresolved && menu.count > 1 &&
        !*command_string(yyjson_obj_get(packet, "bindings"), "target")) {
        yyjson_doc_free(read);
        return local_response("local_clarify", "Which particle emitter should change?");
    }
    const char *bound = command_string(yyjson_obj_get(packet, "bindings"), menu.binding);
    if (*bound || menu.count == 1) {
        const char *choice = *bound ? bound
                                    : command_string(menu.options[0].item, "id");
        bool offered = false;
        for (int i = 0; i < menu.count; i++) {
            offered |= !strcmp(command_string(menu.options[i].item, "id"), choice);
        }
        char *result = local_response(
            offered ? "local_choice" : "local_error",
            offered ? choice : "Choice is no longer available"
        );
        yyjson_doc_free(read);
        return result;
    }
    yyjson_mut_doc *body_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *body = yyjson_mut_obj(body_doc);
    yyjson_mut_doc_set_root(body_doc, body);
    yyjson_mut_val *state = yyjson_mut_obj_add_obj(body_doc, body, "state");
    yyjson_mut_obj_add_strcpy(body_doc, state, "goal", command_string(packet, "command"));
    char observation[256];
    const char *kind = command_string(menu.target, "kind");
    const char *source = target_source(packet, menu.target);
    const char *scope = !strcmp(kind, "prototype") ? " prototype"
                      : !strcmp(kind, "entity")    ? " scene instance"
                                                   : "";
    char relationship[96] = { 0 };
    if (source) {
        snprintf(relationship, sizeof relationship, ", used by %s", source);
    } else if (!strcmp(kind, "entity")) {
        snprintf(relationship, sizeof relationship, ", this scene only");
    }
    snprintf(
        observation,
        sizeof observation,
        "Current target: %s%s%s. Choices reflect current editor values.",
        menu.target ? command_string(menu.target, "name") : "not selected",
        scope,
        relationship
    );
    yyjson_mut_obj_add_strcpy(body_doc, state, "observation", observation);
    yyjson_mut_val *questions = yyjson_mut_obj_add_obj(body_doc, body, "questions");
    yyjson_mut_val *action = yyjson_mut_obj_add_obj(body_doc, questions, "action");
    yyjson_mut_obj_add_str(body_doc, action, "type", "choice");
    yyjson_mut_obj_add_str(body_doc, action, "instructions", menu.question);
    yyjson_mut_val *criteria = yyjson_mut_obj_add_obj(body_doc, action, "criteria");
    for (int i = 0; i < menu.count; i++) {
        char number[4];
        snprintf(number, sizeof number, "%d", i);
        yyjson_mut_obj_add(
            criteria,
            yyjson_mut_strcpy(body_doc, number),
            yyjson_mut_strcpy(body_doc, menu.options[i].label)
        );
    }
    char *body_json = yyjson_mut_write(body_doc, 0, NULL);
    yyjson_mut_doc_free(body_doc);
    yyjson_mut_doc *envelope = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(envelope);
    yyjson_mut_doc_set_root(envelope, root);
    yyjson_mut_obj_add_strcpy(envelope, root, "url", endpoint);
    yyjson_mut_obj_add_strcpy(envelope, root, "body", body_json);
#ifndef __EMSCRIPTEN__
    char *key = api_key();
    if (key) {
        if (!local_endpoint(endpoint)) {
            yyjson_mut_doc_free(envelope);
            yyjson_doc_free(read);
            free(body_json);
            free(key);
            return local_response("local_error", "Use a local tunnel for authenticated Laya");
        }
        yyjson_mut_obj_add_strcpy(envelope, root, "auth_token", key);
        free(key);
    }
#endif
    char *result = yyjson_mut_write(envelope, 0, NULL);
    yyjson_mut_doc_free(envelope);
    yyjson_doc_free(read);
    free(body_json);
    (void)error;
    (void)error_size;
    return result;
}

static char *decision(const char *status, const char *message, const char *binding, const char *value)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_str(doc, root, "status", status);
    yyjson_mut_obj_add_str(doc, root, "message", message);
    yyjson_mut_obj_add_str(doc, root, "provider", "laya");
    if (binding) {
        yyjson_mut_obj_add_str(doc, root, "binding", binding);
    }
    if (value) {
        yyjson_mut_obj_add_str(doc, root, "value", value);
    }
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static char *clarify(laya_menu *menu, const char *message)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_str(doc, root, "status", "needs_clarification");
    yyjson_mut_obj_add_str(doc, root, "message", message);
    yyjson_mut_obj_add_str(doc, root, "provider", "laya");
    yyjson_mut_obj_add_str(doc, root, "binding", menu->binding);
    yyjson_mut_val *choices = yyjson_mut_obj_add_arr(doc, root, "choices");
    for (int i = 0; i < menu->count; i++) {
        yyjson_mut_val *choice = yyjson_mut_arr_add_obj(doc, choices);
        yyjson_mut_obj_add_strcpy(
            doc,
            choice,
            "id",
            command_string(menu->options[i].item, "id")
        );
        yyjson_mut_obj_add_strcpy(doc, choice, "label", menu->options[i].label);
    }
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

static int hex_digit(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    digit = (char)tolower((unsigned char)digit);
    return digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : -1;
}

static bool color_value(const char *text, yyjson_val *field, unsigned *out)
{
    static const struct
    {
        const char *name;
        unsigned rgb;
    } colors[] = { { "black", 0x000000 },   { "white", 0xffffff },
                   { "red", 0xff0000 },     { "green", 0x00ff00 },
                   { "blue", 0x0000ff },    { "yellow", 0xffff00 },
                   { "orange", 0xff8000 },  { "purple", 0x8000ff },
                   { "magenta", 0xff00ff }, { "cyan", 0x00ffff },
                   { "gray", 0x808080 },    { "grey", 0x808080 } };
    unsigned rgb = 0;
    bool found = false;
    for (size_t i = 0; i < sizeof colors / sizeof colors[0]; i++) {
        if (word(text, colors[i].name)) {
            rgb = colors[i].rgb;
            found = true;
            break;
        }
    }
    if (!found) {
        for (const char *at = text; at && *at; at++) {
            if (*at != '#') {
                continue;
            }
            unsigned value = 0;
            int digits = 0;
            while (digits < 6 && hex_digit(at[digits + 1]) >= 0) {
                value = (value << 4) | (unsigned)hex_digit(at[++digits]);
            }
            if (digits == 6 && hex_digit(at[7]) < 0) {
                rgb = value;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        return false;
    }
    *out = (rgb << 8) | (yyjson_get_uint(yyjson_obj_get(field, "value")) & 255u);
    return true;
}

static bool number_value(const char *text, yyjson_val *field, double *out)
{
    const char *amount = NULL;
    bool relative = false;
    for (const char *at = text; at && *at; at++) {
        if (!strncasecmp(at, " by ", 4)) {
            amount = at + 4;
            relative = true;
            break;
        }
        if (!strncasecmp(at, " to ", 4)) {
            amount = at + 4;
            break;
        }
    }
    if (!amount && (word(text, "increase") || word(text, "decrease") ||
                    word(text, "larger") || word(text, "smaller"))) {
        double current = yyjson_get_num(yyjson_obj_get(field, "value"));
        double difference = fabs(current) * 0.25;
        if (yyjson_get_bool(yyjson_obj_get(field, "integer"))) {
            difference = fmax(1, round(difference));
        } else if (!difference) {
            difference = 1;
        }
        *out = current + (word(text, "decrease") || word(text, "smaller")
                              ? -difference
                              : difference);
        return isfinite(*out);
    }
    if (!amount) {
        const char *last = strrchr(text, ' ');
        amount = last ? last + 1 : text;
    }
    if (!isdigit((unsigned char)*amount) && *amount != '-' && *amount != '.') {
        return false;
    }
    char *end;
    double value = strtod(amount, &end);
    if (end == amount || !isfinite(value)) {
        return false;
    }
    bool percent = *end == '%';
    if (percent && !relative) {
        return false;
    }
    if (relative) {
        double current = yyjson_get_num(yyjson_obj_get(field, "value"));
        if (percent) {
            value = fabs(current) * value / 100.0;
        }
        value = current +
                (word(text, "decrease") || word(text, "smaller") ? -value : value);
    }
    *out = value;
    return isfinite(value);
}

static yyjson_mut_val *requested_value(
    yyjson_mut_doc *doc,
    yyjson_val *field,
    const char *goal,
    const char *answer
)
{
    const char *text = answer && *answer ? answer : goal;
    const char *type = command_string(field, "type");
    if (!strcmp(type, "color")) {
        unsigned color;
        return color_value(text, field, &color) ? yyjson_mut_uint(doc, color) : NULL;
    }
    if (!strcmp(type, "choice")) {
        yyjson_val *choices = yyjson_obj_get(field, "choices");
        size_t i, count;
        yyjson_val *choice;
        yyjson_arr_foreach(choices, i, count, choice)
        {
            const char *label = command_string(choice, "label");
            if (word(text, label) || ((word(text, "round") || word(text, "circular")) &&
                                      !strcasecmp(label, "circle"))) {
                return yyjson_val_mut_copy(doc, yyjson_obj_get(choice, "value"));
            }
        }
        return NULL;
    }
    if (!strcmp(type, "number")) {
        double value;
        if (!number_value(text, field, &value)) {
            return NULL;
        }
        return yyjson_get_bool(yyjson_obj_get(field, "integer")) &&
                       floor(value) == value && value >= (double)INT64_MIN &&
                       value < (double)INT64_MAX
                 ? yyjson_mut_sint(doc, (int64_t)value)
                 : yyjson_mut_real(doc, value);
    }
    if (!strcmp(type, "bool")) {
        if (word(text, "enable") || word(text, "on") || word(text, "show") ||
            word(text, "true")) {
            return yyjson_mut_bool(doc, true);
        }
        if (word(text, "disable") || word(text, "off") || word(text, "hide") ||
            word(text, "false")) {
            return yyjson_mut_bool(doc, false);
        }
    }
    if (!strcmp(type, "text") && answer && *answer) {
        return yyjson_mut_strcpy(doc, answer);
    }
    return NULL;
}

static void append_set(
    yyjson_mut_doc *doc,
    yyjson_mut_val *operations,
    yyjson_val *target,
    yyjson_val *field,
    yyjson_mut_val *value
)
{
    yyjson_mut_val *operation = yyjson_mut_arr_add_obj(doc, operations);
    yyjson_mut_obj_add_str(doc, operation, "op", "set");
    yyjson_mut_obj_add_strcpy(doc, operation, "target", command_string(target, "id"));
    yyjson_mut_obj_add_strcpy(doc, operation, "field", command_string(field, "id"));
    yyjson_mut_obj_add_val(doc, operation, "value", value);
}

static char *propose(yyjson_val *packet, laya_menu *menu, yyjson_val *field)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_val *catalog = yyjson_obj_get(packet, "catalog");
    const char *answer = command_string(yyjson_obj_get(packet, "bindings"), "value");
    yyjson_mut_val *value = requested_value(doc, field, command_string(packet, "command"), answer);
    if (!value) {
        yyjson_mut_obj_add_bool(doc, root, "ok", true);
        yyjson_mut_obj_add_str(doc, root, "status", "needs_clarification");
        yyjson_mut_obj_add_str(doc, root, "provider", "laya");
        yyjson_mut_obj_add_str(doc, root, "binding", "value");
        yyjson_mut_obj_add_str(doc, root, "message", "What value should this property have?");
        yyjson_val *options = yyjson_obj_get(field, "choices");
        if (options) {
            yyjson_mut_val *choices = yyjson_mut_obj_add_arr(doc, root, "choices");
            size_t i, count;
            yyjson_val *option;
            yyjson_arr_foreach(options, i, count, option)
            {
                const char *label = command_string(option, "label");
                yyjson_mut_val *choice = yyjson_mut_arr_add_obj(doc, choices);
                yyjson_mut_obj_add_strcpy(doc, choice, "id", label);
                yyjson_mut_obj_add_strcpy(doc, choice, "label", label);
            }
        }
    } else {
        bool review = !getenv("CHIBI_COMMAND_REVIEW") ||
                      strcmp(getenv("CHIBI_COMMAND_REVIEW"), "0");
        yyjson_mut_obj_add_bool(doc, root, "ok", true);
        yyjson_mut_obj_add_str(doc, root, "status", review ? "needs_review" : "ready");
        yyjson_mut_obj_add_str(doc, root, "message", review ? "Review changes" : "Ready");
        yyjson_mut_obj_add_str(doc, root, "provider", "laya");
        yyjson_mut_obj_add_strcpy(doc, root, "revision", command_string(catalog, "revision"));
        yyjson_mut_val *operations = yyjson_mut_obj_add_arr(doc, root, "operations");
        if (contains(command_string(field, "id"), "GameParticleEmitter.size.start.x")) {
            yyjson_val *fields = yyjson_obj_get(menu->target, "fields");
            size_t i, count;
            yyjson_val *part;
            yyjson_arr_foreach(fields, i, count, part)
            {
                const char *id = command_string(part, "id");
                if (contains(id, "GameParticleEmitter.size.start.x")) {
                    append_set(doc, operations, menu->target, part, value);
                } else if (contains(id, "GameParticleEmitter.size.start.y")) {
                    yyjson_mut_val *other = requested_value(
                        doc,
                        part,
                        command_string(packet, "command"),
                        answer
                    );
                    if (other) {
                        append_set(doc, operations, menu->target, part, other);
                    }
                }
            }
        } else {
            append_set(doc, operations, menu->target, field, value);
        }
        yyjson_mut_val *context = yyjson_mut_obj_add_obj(doc, root, "context");
        yyjson_mut_obj_add_strcpy(doc, context, "document", command_string(catalog, "document"));
        yyjson_mut_obj_add_str(doc, context, "operation", "set");
        yyjson_mut_val *targets = yyjson_mut_obj_add_arr(doc, context, "targets");
        yyjson_mut_arr_add_strcpy(doc, targets, command_string(menu->target, "id"));
    }
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return result;
}

char *editor_command_laya_result(const char *packet_json, const char *response, char *error, size_t error_size)
{
    yyjson_doc *packet_doc = yyjson_read(packet_json, strlen(packet_json), 0);
    yyjson_val *packet = packet_doc ? yyjson_doc_get_root(packet_doc) : NULL;
    laya_menu menu = { 0 };
    const char *problem = build_menu(packet, &menu);
    if (problem) {
        yyjson_doc_free(packet_doc);
        return decision("unsupported", problem, NULL, NULL);
    }
    yyjson_doc *response_doc = yyjson_read(response, strlen(response), 0);
    yyjson_val *root = response_doc ? yyjson_doc_get_root(response_doc) : NULL;
    const char *chosen = command_string(root, "local_choice");
    const char *local_error = command_string(root, "local_error");
    const char *local_clarify = command_string(root, "local_clarify");
    if (*local_error) {
        yyjson_doc_free(response_doc);
        yyjson_doc_free(packet_doc);
        return decision("unsupported", local_error, NULL, NULL);
    }
    if (*local_clarify) {
        char *result = clarify(&menu, local_clarify);
        yyjson_doc_free(response_doc);
        yyjson_doc_free(packet_doc);
        return result;
    }
    if (!*chosen) {
        yyjson_val *answer = yyjson_obj_get(yyjson_obj_get(root, "answers"), "action");
        const char *ordinal = command_string(answer, "choice");
        yyjson_val *probabilities = yyjson_obj_get(answer, "probabilities");
        double confidence = yyjson_get_num(yyjson_obj_get(answer, "confidence"));
        double sum = 0, best = -1, picked = -1;
        int index = -1;
        bool valid = yyjson_is_obj(probabilities) &&
                     yyjson_obj_size(probabilities) == (size_t)menu.count &&
                     yyjson_is_num(yyjson_obj_get(answer, "confidence")) &&
                     isfinite(confidence) && confidence >= 0 && confidence <= 1;
        for (int i = 0; i < menu.count && valid; i++) {
            char key[4];
            snprintf(key, sizeof key, "%d", i);
            yyjson_val *entry = yyjson_obj_get(probabilities, key);
            double probability = yyjson_get_num(entry);
            valid = yyjson_is_num(entry) && isfinite(probability) &&
                    probability >= 0 && probability <= 1;
            sum += probability;
            best = fmax(best, probability);
            if (!strcmp(key, ordinal)) {
                index = i;
                picked = probability;
            }
        }
        valid &= index >= 0 && fabs(sum - 1) < 0.02 && picked + 0.000001 >= best;
        if (!valid) {
            const char *message = command_string(root, "error");
            fail(error, error_size, *message ? message : "Laya returned an invalid decision");
            yyjson_doc_free(response_doc);
            yyjson_doc_free(packet_doc);
            return NULL;
        }
        if (fmin(confidence, picked) < LAYA_CONFIDENCE) {
            char *result = clarify(&menu, menu.question);
            yyjson_doc_free(response_doc);
            yyjson_doc_free(packet_doc);
            return result;
        }
        chosen = command_string(menu.options[index].item, "id");
    }
    yyjson_val *selected = NULL;
    for (int i = 0; i < menu.count; i++) {
        if (!strcmp(command_string(menu.options[i].item, "id"), chosen)) {
            selected = menu.options[i].item;
        }
    }
    char *result;
    if (!selected) {
        fail(error, error_size, "Laya selected an unavailable editor choice");
        result = NULL;
    } else if (!strcmp(menu.binding, "target") || strcmp(command_string(yyjson_obj_get(packet, "bindings"), "field"), chosen)) {
        result = decision("continue", "Inspecting editor property…", menu.binding, chosen);
    } else {
        result = propose(packet, &menu, selected);
    }
    yyjson_doc_free(response_doc);
    yyjson_doc_free(packet_doc);
    return result;
}
