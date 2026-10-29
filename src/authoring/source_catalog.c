#include "authoring/source_catalog.h"

#include "assets/input_map.h"
#include "assets/ui_document.h"
#include "authoring/reference_index.h"
#include "authoring/source_asset.h"
#include "authoring/source_catalog_snapshot.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/scalar.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum
{
    MAX_SOURCES = 256,
    MAX_FILES = 1024,
    MAX_ENTRIES = 4096,
    MAX_FILE_BYTES = 512 * 1024,
    MAX_TOTAL_BYTES = 8 * 1024 * 1024,
    MAX_DEPTH = 16,
    MAX_PATH = 4096,
    MAX_VALUES = 65536,
    MAX_SPECIALIZATIONS = 256,
    /* SCENE_DEPTH_MAX: a scene nested deeper does not load. */
    MAX_ENTITY_DEPTH = 256
};
typedef enum
{
    DECLARATION_EVENT,
    DECLARATION_PROTOTYPE,
    DECLARATION_SOURCE,
    DECLARATION_SCENE,
    DECLARATION_UI,
    DECLARATION_RELATIONSHIP,
} declaration_kind;
struct source_catalog
{
    arena storage;
    const char *root;
    script_catalog view;
    source_catalog_source *sources;
    source_catalog_definition *definitions;
    source_catalog_reference *references;
    source_catalog_diagnostic *diagnostics;
    script_catalog_capability *capabilities;
    script_catalog_relationship *relationships;
    script_catalog_template *templates;
    script_catalog_specialization *specializations;
    script_specialization_request *requests;
    const char **request_paths;
    input_action *input;
    int native_capabilities;
    size_t bytes;
    int files, entries;
    int values;
    const source_catalog_edit *edits;
    bool *edit_used;
    int edit_count;
    unsigned char *default_key;
    bool defaults_known;
};
static const char *own(source_catalog *c, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(&c->storage, size);
    memcpy(copy, text, size);
    return copy;
}
static void error(source_catalog *c, const char *path, script_range range, const char *message)
{
    for (int i = 0; i < acount(c->diagnostics); i++) {
        const source_catalog_diagnostic *previous = &c->diagnostics[i];
        bool same_path = previous->path == path ||
                         (previous->path && path && !strcmp(previous->path, path));
        if (same_path && previous->range.start == range.start &&
            previous->range.end == range.end && !strcmp(previous->message, message)) {
            return;
        }
    }
    source_catalog_diagnostic diagnostic = { .path = own(c, path), .range = range };
    snprintf(diagnostic.message, sizeof diagnostic.message, "%s", message);
    apush(c->diagnostics, diagnostic);
}
static void update_view(source_catalog *c)
{
    c->view.capabilities = c->capabilities;
    c->view.capability_count = acount(c->capabilities);
    c->view.relationships = c->relationships;
    c->view.relationship_count = acount(c->relationships);
    c->view.templates = c->templates;
    c->view.template_count = acount(c->templates);
    c->view.specializations = c->specializations;
    c->view.specialization_count = acount(c->specializations);
}
static void capability(source_catalog *c, const char *path, script_catalog_capability value)
{
    for (int i = 0; i < acount(c->capabilities); i++) {
        if (c->capabilities[i].id == value.id) {
            error(c, path, (script_range){ 0 }, "Duplicate project capability identity");
            return;
        }
    }
    apush(c->capabilities, value);
    update_view(c);
}
/* Only authored exports enter here. Native aliases and event declarations keep
 * their existing insertion rules; an export cannot hide an exact callable of
 * the same kind already present in this catalog. */
static void exported_callable(source_catalog *c, const char *path, script_range range, script_catalog_capability value)
{
    for (int i = 0; i < acount(c->capabilities); i++) {
        const script_catalog_capability *existing = &c->capabilities[i];
        const char *name = existing->qualified_name ? existing->qualified_name
                                                    : existing->name;
        if (existing->id != value.id && existing->kind == value.kind &&
            !strcmp(name, value.qualified_name)) {
            char message[192];
            snprintf(
                message,
                sizeof message,
                "Duplicate exported callable name '%s'",
                value.qualified_name
            );
            error(c, path, range, message);
            return;
        }
    }
    capability(c, path, value);
}

static char *read_source(source_catalog *c, const char *path)
{
    if (++c->files > MAX_FILES) {
        error(c, path, (script_range){ 0 }, "Project declaration file limit exceeded");
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        error(c, path, (script_range){ 0 }, "Cannot read project declaration");
        return NULL;
    }
    long length = -1;
    if (!fseek(file, 0, SEEK_END)) {
        length = ftell(file);
    }
    if (length < 0 || length > MAX_FILE_BYTES ||
        c->bytes + (size_t)length > MAX_TOTAL_BYTES || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        error(c, path, (script_range){ 0 }, "Project declaration byte limit exceeded");
        return NULL;
    }
    char *text = xmalloc((size_t)length + 1);
    bool complete = fread(text, 1, (size_t)length, file) == (size_t)length;
    complete = complete && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!complete || memchr(text, 0, (size_t)length)) {
        free(text);
        error(c, path, (script_range){ 0 }, "Project declaration changed during read or contains NUL");
        return NULL;
    }
    text[length] = 0;
    c->bytes += (size_t)length;
    return text;
}
static script_type event_type(source_catalog *c, const av_value *variable)
{
    const char *name = av_get_str(av_obj_get(variable, "type_name"));
    const char *storage = av_get_str(av_obj_get(variable, "type"));
    int index = name ? script_catalog_find_type(&c->view, name) : -1;
    script_type type = index >= 0 ? c->view.types[index].type
                                  : script_builtin_type(
                                        name      ? name
                                        : storage ? storage
                                                  : ""
                                    );
    if (!name && (type.flags & SCRIPT_TYPE_ERROR)) {
        for (int i = PIN_BOOL; i <= PIN_ENUM; i++) {
            if (storage && !strcmp(storage, pin_type_name(i))) {
                type = (script_type){ .storage = (pin_type)i };
                break;
            }
        }
    }
    const char *container = av_get_str(av_obj_get(variable, "container"));
    if (container && !strcmp(container, "array")) {
        type.container = PIN_CONTAINER_ARRAY;
    }
    if (container && !strcmp(container, "set")) {
        type.container = PIN_CONTAINER_SET;
    }
    if (container && !strcmp(container, "map")) {
        type.container = PIN_CONTAINER_MAP;
    }
    return type;
}
static const script_catalog_capability *find_event(const source_catalog *c, const char *name, int count)
{
    for (int i = 0; i < count; i++) {
        const script_catalog_capability *value = &c->view.capabilities[i];
        if (value->kind == SCRIPT_CAPABILITY_EVENT && !strcmp(value->name, name)) {
            return value;
        }
    }
    return NULL;
}
static void named_event(source_catalog *c, const char *path, const char *name)
{
    if (!name || !*name || find_event(c, name, c->view.capability_count)) {
        return;
    }
    capability(
        c,
        path,
        (script_catalog_capability){
            .id = script_catalog_id("event", name),
            .name = own(c, name),
            .qualified_name = own(c, name),
            .kind = SCRIPT_CAPABILITY_EVENT,
            .completion_outcome = -1,
        }
    );
}
static script_range value_range(const char *text, const av_value *value)
{
    av_source origin = av_get_source(value);
    script_range range = { .start = -1, .end = -1 };
    if (origin.start == SIZE_MAX || origin.end == SIZE_MAX) {
        return range;
    }
    assert(origin.start < origin.end && origin.end <= strlen(text));
    range = (script_range){ .start = (int)origin.start,
                            .end = (int)origin.end,
                            .line = 1,
                            .column = 1 };
    for (size_t i = 0; i < origin.start; i++) {
        if (text[i] == '\n') {
            range.line++;
            range.column = 1;
        } else {
            range.column++;
        }
    }
    return range;
}
static script_range text_range(const char *text, int start, int end)
{
    script_range range = { .start = start, .end = end, .line = 1, .column = 1 };
    for (int i = 0; i < start; i++) {
        if (text[i] == '\n') {
            range.line++;
            range.column = 1;
        } else {
            range.column++;
        }
    }
    return range;
}

static bool project_value(source_catalog *, const char *path);
static void ui_references(source_catalog *c, const char *path, const char *text)
{
    ui_document_event_range *ranges = NULL;
    ui_document_event_ranges(text, &ranges);
    const char *owned_path = own(c, path), *owned_text = own(c, text);
    for (int i = 0; i < acount(ranges); i++) {
        if (!project_value(c, path)) {
            break;
        }
        ui_document_event_range at = ranges[i];
        int length = at.end - at.start;
        char *name = xmalloc((size_t)length + 1);
        memcpy(name, text + at.start, (size_t)length);
        name[length] = 0;
        apush(
            c->references,
            ((source_catalog_reference){ .kind = SOURCE_REFERENCE_EVENT,
                                         .name = own(c, name),
                                         .path = owned_path,
                                         .text = owned_text,
                                         .range = text_range(text, at.start, at.end),
                                         .text_kind = SOURCE_TEXT_RML })
        );
        free(name);
    }
    afree(ranges);
}
static void value_reference(
    source_catalog *c,
    const char *path,
    const char *text,
    const av_value *value,
    source_catalog_reference_kind kind
)
{
    const char *name = av_get_str(value);
    if (!name || !*name) {
        return;
    }
    apush(
        c->references,
        ((source_catalog_reference){ .kind = kind,
                                     .name = own(c, name),
                                     .path = path,
                                     .text = text,
                                     .range = value_range(text, value) })
    );
}
static bool project_value(source_catalog *c, const char *path)
{
    if (++c->values <= MAX_VALUES) {
        return true;
    }
    error(c, path, (script_range){ 0 }, "Project declaration value limit exceeded");
    return false;
}
static void project_references(
    source_catalog *c,
    const char *path,
    const char *text,
    const av_value *value,
    bool scene
);
/* Scene entities and their children, as deep as scenes may nest them. */
static void entity_references(
    source_catalog *c,
    const char *path,
    const char *text,
    const av_value *entities,
    int depth
)
{
    for (size_t i = 0; i < av_arr_size(entities) && c->values <= MAX_VALUES &&
                       depth <= MAX_ENTITY_DEPTH;
         i++) {
        const av_value *entity = av_arr_get(entities, i);
        project_references(c, path, text, entity, false);
        entity_references(c, path, text, av_obj_get(entity, "children"), depth + 1);
    }
}
static void project_references(
    source_catalog *c,
    const char *path,
    const char *text,
    const av_value *value,
    bool scene
)
{
    if (!project_value(c, path)) {
        error(c, path, (script_range){ 0 }, "Project reference value limit exceeded");
        return;
    }
    if (scene) {
        entity_references(c, path, text, av_obj_get(value, "entities"), 1);
        return;
    }
    value_reference(c, path, text, av_obj_get(value, "prototype"), SOURCE_REFERENCE_PROTOTYPE);
    const av_value *pairs = av_obj_get(value, "links");
    for (size_t i = 0; i < av_obj_size(pairs); i++) {
        value_reference(c, path, text, av_obj_key_at(pairs, i), SOURCE_REFERENCE_RELATIONSHIP);
    }
    const av_value *behaviors = av_obj_get(value, "behaviors");
    for (size_t i = 0; i < av_arr_size(behaviors); i++) {
        if (!project_value(c, path)) {
            return;
        }
        value_reference(c, path, text, av_arr_get(behaviors, i), SOURCE_REFERENCE_PROGRAM);
    }
    const av_value *components = av_obj_get(value, "components");
    const av_value *binding = av_obj_get(av_obj_get(components, "GlobalBinding"), "name");
    const char *binding_name = av_get_str(binding);
    if (binding_name && binding_name[0]) {
        size_t size = strlen(binding_name) + sizeof "SceneGlobals.";
        char *field = arena_alloc(&c->storage, size);
        snprintf(field, size, "SceneGlobals.%s", binding_name);
        apush(
            c->definitions,
            ((source_catalog_definition){
                .id = script_catalog_id("field", field),
                .name = own(c, binding_name),
                .path = path,
                .text = text,
                .range = value_range(text, binding),
                .field = true,
                .owner = script_catalog_id("type", "SceneGlobals"),
            })
        );
    }
    for (size_t i = 0; i < av_obj_size(components); i++) {
        if (!project_value(c, path)) {
            return;
        }
        const char *key = av_get_str(av_obj_key_at(components, i));
        int type = key ? script_catalog_find_type(&c->view, key) : -1;
        if (type < 0 || !c->view.types[type].component) {
            continue;
        }
        const script_catalog_type *component = &c->view.types[type];
        const av_value *fields = av_obj_value_at(components, i);
        for (int j = 0; j < component->field_count; j++) {
            const char *field = component->fields[j].name;
            reference_asset_kind kind = reference_named_field_kind(component->name, field);
            if (kind == REFERENCE_ASSET_PROTOTYPE || kind == REFERENCE_ASSET_SYSTEM) {
                const av_value *named = av_obj_get(fields, field);
                if (!av_get_str(named)) {
                    continue;
                }
                if (!project_value(c, path)) {
                    return;
                }
                value_reference(
                    c,
                    path,
                    text,
                    named,
                    kind == REFERENCE_ASSET_SYSTEM ? SOURCE_REFERENCE_SYSTEM : SOURCE_REFERENCE_PROTOTYPE
                );
            }
        }
    }
}
static void declarative(source_catalog *c, const char *path, const char *text, bool event)
{
    char message[192];
    size_t length = strlen(path);
    bool source = !event && length > 6 && !strcmp(path + length - 6, ".chibi");
    av_doc *document = source
                         ? authoring_prototype_source_read(text, NULL, message, sizeof message)
                         : asset_text_read(text, message, sizeof message);
    if (!document) {
        error(c, path, (script_range){ 0 }, message);
        return;
    }
    const av_value *root = av_doc_root(document);
    const av_value *name_value = av_obj_get(root, "name");
    const char *name = av_get_str(name_value);
    if (!name || !*name) {
        error(c, path, (script_range){ 0 }, "Project declaration requires a name");
        av_doc_free(document);
        return;
    }
    script_catalog_capability value = {
        .id = script_catalog_id(event ? "event" : "prototype", name),
        .name = own(c, name),
        .qualified_name = own(c, name),
        .kind = event ? SCRIPT_CAPABILITY_EVENT : SCRIPT_CAPABILITY_PROTOTYPE,
    };
    if (event) {
        const av_value *variables = av_obj_get(root, "variables");
        size_t count = av_arr_size(variables);
        if (count > MAX_FILES) {
            error(c, path, (script_range){ 0 }, "Event parameter limit exceeded");
            av_doc_free(document);
            return;
        }
        script_catalog_parameter *parameters = arena_alloc(&c->storage, count * sizeof *parameters);
        for (size_t i = 0; i < count; i++) {
            const av_value *variable = av_arr_get(variables, i);
            parameters[i] = (script_catalog_parameter){
                .name = own(c, av_get_str(av_obj_get(variable, "name"))),
                .type = event_type(c, variable),
            };
            if (!parameters[i].name || (parameters[i].type.flags & SCRIPT_TYPE_ERROR)) {
                error(c, path, (script_range){ 0 }, "Invalid event parameter declaration");
            }
        }
        value.parameters = parameters;
        value.parameter_count = (int)count;
    }
    capability(c, path, value);
    int first_reference = acount(c->references);
    const char *owned_path = own(c, path), *owned_text = own(c, text);
    apush(
        c->definitions,
        ((source_catalog_definition){ .id = value.id,
                                      .kind = value.kind,
                                      .name = value.name,
                                      .path = owned_path,
                                      .text = owned_text,
                                      .range = value_range(text, name_value),
                                      .text_kind = source ? SOURCE_TEXT_CHIBI
                                                          : SOURCE_TEXT_KDL })
    );
    if (!event) {
        project_references(c, owned_path, owned_text, root, 0);
        entity_references(c, owned_path, owned_text, av_obj_get(root, "children"), 1);
        const av_value *includes = av_obj_get(root, "includes");
        for (size_t i = 0; i < av_arr_size(includes); i++) {
            value_reference(c, owned_path, owned_text, av_arr_get(includes, i), SOURCE_REFERENCE_PROTOTYPE);
        }
    }
    if (source) {
        for (int i = first_reference; i < acount(c->references); i++) {
            c->references[i].text_kind = SOURCE_TEXT_CHIBI;
        }
    }
    av_doc_free(document);
}
static char *edited_source(source_catalog *c, const char *path, const char *text)
{
    size_t size = strnlen(text, MAX_FILE_BYTES + 1);
    if (++c->files > MAX_FILES || size > MAX_FILE_BYTES ||
        c->bytes + size > MAX_TOTAL_BYTES) {
        error(c, path, (script_range){ 0 }, "Project declaration byte or file limit exceeded");
        return NULL;
    }
    c->bytes += size;
    return xstrdup(text);
}
static void load_file(source_catalog *c, const char *path, declaration_kind kind)
{
    char *text = NULL;
    const char *identity = NULL;
    bool edited = false;
    {
        for (int i = 0; i < c->edit_count; i++) {
            if (c->edits[i].path && !strcmp(path, c->edits[i].path)) {
                c->edit_used[i] = true;
                edited = true;
                identity = c->edits[i].identity;
                if (c->edits[i].text) {
                    text = edited_source(c, path, c->edits[i].text);
                }
                break;
            }
        }
    }
    if (!edited) {
        text = read_source(c, path);
    }
    if (!text) {
        return;
    }
    if (kind == DECLARATION_RELATIONSHIP) {
        char message[192];
        ecs_relationship_desc *desc = authoring_relationship_source_read(text, message, sizeof message);
        if (!desc) {
            error(c, path, (script_range){ 0 }, message);
        } else {
            bool duplicate = false;
            for (int i = 0; i < acount(c->relationships); i++) {
                duplicate |= c->relationships[i].id == desc->id ||
                             !strcmp(c->relationships[i].name, desc->name);
            }
            if (duplicate || !strcmp(desc->name, "ChildOf")) {
                error(c, path, (script_range){ 0 }, "Duplicate relationship name or identity");
            } else {
                apush(
                    c->relationships,
                    ((script_catalog_relationship){
                        .id = desc->id,
                        .name = own(c, desc->name),
                        .exclusive = !!(desc->traits & ECS_REL_EXCLUSIVE),
                        .traversable = !!(desc->traits & ECS_REL_TRAVERSABLE) })
                );
                script_syntax *syntax = script_parse(text);
                TSNode root = ts_tree_root_node(syntax->tree), declaration = { 0 };
                for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
                    TSNode node = ts_node_named_child(root, i);
                    if (!strcmp(ts_node_type(node), "relationship_declaration")) {
                        declaration = node;
                        break;
                    }
                }
                TSNode name = ts_node_child_by_field_name(declaration, "name", 4);
                apush(
                    c->definitions,
                    ((source_catalog_definition){ .id = desc->id,
                                                  .name = own(c, desc->name),
                                                  .path = own(c, path),
                                                  .text = own(c, text),
                                                  .range = script_node_range(name),
                                                  .text_kind = SOURCE_TEXT_CHIBI,
                                                  .relationship = true })
                );
                script_syntax_free(syntax);
                update_view(c);
            }
            authoring_relationship_source_free(desc);
        }
        free(text);
        return;
    }
    if (kind == DECLARATION_UI) {
        const char *base = strrchr(path, '/');
        if (!base || strcmp(base + 1, UI_DOCUMENT_PAUSE_MENU ".rml")) {
            ui_document_event *events = NULL;
            ui_document_events(text, &events);
            for (int i = 0; i < acount(events); i++) {
                named_event(c, path, events[i].name);
            }
            ui_document_events_free(events);
            ui_references(c, path, text);
        }
        free(text);
        return;
    }
    if (kind == DECLARATION_SCENE) {
        char message[192];
        av_doc *document = asset_text_read(text, message, sizeof message);
        if (document) {
            project_references(c, own(c, path), own(c, text), av_doc_root(document), true);
            av_doc_free(document);
        } else {
            error(c, path, (script_range){ 0 }, message);
        }
        free(text);
        return;
    }
    if (kind != DECLARATION_SOURCE) {
        declarative(c, path, text, kind == DECLARATION_EVENT);
        free(text);
        return;
    }
    if (acount(c->sources) == MAX_SOURCES) {
        error(c, path, (script_range){ 0 }, "Executable source limit exceeded");
        free(text);
        return;
    }
    size_t root_length = strlen(c->root);
    while (root_length && c->root[root_length - 1] == '/') {
        root_length--;
    }
    assert(!strncmp(c->root, path, root_length) && path[root_length] == '/');
    source_catalog_source source = {
        .path = own(c, path),
        .identity = own(c, identity ? identity : path + root_length + 1),
        .text = text,
        .syntax = script_parse(text)
    };
    apush(c->sources, source);
}
static void scan(source_catalog *c, const char *path, int depth, declaration_kind kind)
{
    if (depth > MAX_DEPTH) {
        error(c, path, (script_range){ 0 }, "Project declaration directory depth exceeded");
        return;
    }
    struct stat directory_info;
    if (lstat(path, &directory_info)) {
        if (errno != ENOENT) {
            error(c, path, (script_range){ 0 }, "Cannot stat project declaration directory");
        }
        return;
    }
    if (!S_ISDIR(directory_info.st_mode)) {
        error(c, path, (script_range){ 0 }, "Project declaration directory must not be a symlink");
        return;
    }
    DIR *directory = opendir(path);
    if (!directory) {
        if (errno != ENOENT) {
            error(c, path, (script_range){ 0 }, "Cannot scan project declaration directory");
        }
        return;
    }
    const char *suffix = kind == DECLARATION_SOURCE || kind == DECLARATION_RELATIONSHIP
                           ? ".chibi"
                       : kind == DECLARATION_UI ? ".rml"
                                                : ".kdl";
    struct dirent *entry;
    while (c->entries < MAX_ENTRIES && (entry = readdir(directory))) {
        c->entries++;
        if (entry->d_name[0] == '.') {
            continue;
        }
        char child[MAX_PATH];
        int length = snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
        if (length < 0 || length >= (int)sizeof child) {
            error(c, path, (script_range){ 0 }, "Project declaration path limit exceeded");
            continue;
        }
        struct stat info;
        if (lstat(child, &info)) {
            error(c, child, (script_range){ 0 }, "Cannot stat project declaration");
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            scan(c, child, depth + 1, kind);
            continue;
        }
        size_t size = strlen(child), end = strlen(suffix);
        if (S_ISREG(info.st_mode) &&
            ((size >= end && !strcmp(child + size - end, suffix)) ||
             (kind == DECLARATION_PROTOTYPE && size > 6 &&
              !strcmp(child + size - 6, ".chibi")))) {
            load_file(c, child, kind);
        }
    }
    if (c->entries >= MAX_ENTRIES) {
        error(c, path, (script_range){ 0 }, "Project declaration directory entry limit exceeded");
    }
    closedir(directory);
}
static void *grow(source_catalog *c, const void *items, int count, int extra, size_t size)
{
    void *grown = arena_alloc(&c->storage, (size_t)(count + extra) * size);
    memcpy(grown, items, (size_t)count * size);
    return grown;
}
static bool library_type_source(const script_syntax *syntax)
{
    /* Invalid syntax may recover a library declaration during analysis. */
    if (syntax->diagnostic_count) {
        return true;
    }
    TSNode root = ts_tree_root_node(syntax->tree);
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode program = ts_node_named_child(root, i);
        if (ts_node_is_extra(program)) {
            continue;
        }
        const char *kind = ts_node_type(program);
        return strcmp(kind, "behavior") && strcmp(kind, "system");
    }
    return true;
}
/* Publish library type identities before callable signatures. A second pass
 * resolves payloads that name a type from a later library in source order. */
static void declared_types(source_catalog *c)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < acount(c->sources); i++) {
            source_catalog_source *source = &c->sources[i];
            if (!library_type_source(source->syntax)) {
                continue;
            }
            script_analysis *analysis = script_declarations_document(
                source->syntax,
                &c->view,
                source->identity
            );
            if (analysis->kind != SCRIPT_PROGRAM_LIBRARY || !analysis->type_count) {
                script_analysis_free(analysis);
                continue;
            }
            script_catalog input = { .types = analysis->types,
                                     .type_count = analysis->type_count },
                           owned;
            if (!script_catalog_snapshot(&c->storage, &input, &owned)) {
                error(c, source->path, (script_range){ 0 }, "Cannot retain declared type payload");
                script_analysis_free(analysis);
                continue;
            }
            for (int j = 0; j < owned.type_count; j++) {
                script_catalog_type type = owned.types[j];
                if (!type.internal) {
                    const char *local = type.source_name ? type.source_name
                                                         : type.name;
                    size_t bytes = strlen(analysis->name) + strlen(local) + 2;
                    char *qualified = arena_alloc(&c->storage, bytes);
                    snprintf(qualified, bytes, "%s.%s", analysis->name, local);
                    type.name = qualified;
                    type.source_name = local;
                }
                int existing = -1;
                bool duplicate = false;
                for (int k = 0; k < c->view.type_count; k++) {
                    if (c->view.types[k].id == type.id) {
                        existing = k;
                    } else if (
                        !type.internal && !c->view.types[k].internal &&
                        !strcmp(c->view.types[k].name, type.name)
                    ) {
                        duplicate = true;
                    }
                }
                if (duplicate) {
                    if (pass) {
                        char message[192];
                        snprintf(message, sizeof message, "Duplicate exported type name '%s'", type.name);
                        error(c, source->path, (script_range){ 0 }, message);
                    }
                    continue;
                }
                if (existing >= 0) {
                    ((script_catalog_type *)c->view.types)[existing] = type;
                    continue;
                }
                script_catalog_type *types = grow(c, c->view.types, c->view.type_count, 1, sizeof *types);
                types[c->view.type_count++] = type;
                c->view.types = types;
            }
            script_analysis_free(analysis);
        }
    }
}
/* An edit of the input map replaces the file, and one without text removes
 * it. */
static char *input_text(source_catalog *c, const char *path)
{
    for (int i = 0; i < c->edit_count; i++) {
        if (c->edits[i].path && !strcmp(c->edits[i].path, path)) {
            c->edit_used[i] = true;
            return c->edits[i].text ? edited_source(c, path, c->edits[i].text) : NULL;
        }
    }
    struct stat info;
    if (lstat(path, &info) && errno == ENOENT) {
        return NULL;
    }
    return read_source(c, path);
}
/* Each action's definition is its exact node name in the map, which
 * input_map_read accepted. */
static void input_definitions(source_catalog *c, const char *path, const char *text)
{
    av_doc *document = asset_text_read(text, NULL, 0);
    assert(document);
    const av_value *root = av_doc_root(document);
    for (size_t i = 0; i < av_obj_size(root); i++) {
        const char *name = own(c, av_get_str(av_obj_key_at(root, i)));
        av_source at = av_get_source(av_obj_value_at(root, i));
        assert(at.name_start < at.name_end && at.name_end != SIZE_MAX);
        apush(
            c->definitions,
            ((source_catalog_definition){
                .id = script_catalog_id("event", name),
                .kind = SCRIPT_CAPABILITY_EVENT,
                .name = name,
                .path = path,
                .text = text,
                .range = text_range(text, (int)at.name_start, (int)at.name_end) })
        );
    }
    av_doc_free(document);
}
static bool has_global(const source_catalog *c, const char *name)
{
    for (int i = 0; i < c->view.global_count; i++) {
        if (!strcmp(c->view.globals[i].name, name)) {
            return true;
        }
    }
    return false;
}
/* Named entity bindings are project declarations. Discover them from authored
 * KDL, without expanding a scene or depending on the running world. */
static void global_declarations(source_catalog *c)
{
    script_catalog_field *fields = NULL;
    for (int i = 0; i < acount(c->definitions); i++) {
        const source_catalog_definition *definition = &c->definitions[i];
        if (!definition->field) {
            continue;
        }
        bool found = false;
        for (int j = 0; j < acount(fields); j++) {
            found |= fields[j].id == definition->id;
        }
        if (!found) {
            apush(
                fields,
                ((script_catalog_field){
                    .id = definition->id,
                    .name = definition->name,
                    .type = { .storage = PIN_HANDLE, .nominal = c->view.entity_type },
                    .read = script_catalog_id("capability", "Get Global Variable"),
                    .read_key = definition->name,
                })
            );
        }
    }
    int count = acount(fields);
    if (!count) {
        afree(fields);
        return;
    }
    const source_catalog_definition *definition = NULL;
    for (int i = 0; !definition && i < acount(c->definitions); i++) {
        if (c->definitions[i].field) {
            definition = &c->definitions[i];
        }
    }
    assert(definition);
    const char *message = NULL;
    if (has_global(c, "globals")) {
        message = "Scene bindings need the global name globals; rename the "
                  "global that uses it";
    } else if (script_catalog_find_type(&c->view, "SceneGlobals") >= 0) {
        message = "Scene bindings need the type name SceneGlobals; rename the "
                  "type that uses it";
    } else if (!script_catalog_capability_by_id(&c->view, fields[0].read)) {
        message = "Scene bindings need the native global read capability";
    }
    if (message) {
        error(c, definition->path, definition->range, message);
        int kept = 0;
        for (int i = 0; i < acount(c->definitions); i++) {
            if (!c->definitions[i].field) {
                c->definitions[kept++] = c->definitions[i];
            }
        }
        asetlen(c->definitions, kept);
        afree(fields);
        return;
    }
    script_catalog_field *owned = arena_alloc(&c->storage, (size_t)count * sizeof *owned);
    memcpy(owned, fields, (size_t)count * sizeof *owned);
    afree(fields);
    uint64_t id = script_catalog_id("type", "SceneGlobals");
    script_type scope = { .storage = PIN_STR, .nominal = id, .flags = SCRIPT_TYPE_NAMESPACE };
    script_catalog_type *types = grow(c, c->view.types, c->view.type_count, 1, sizeof *types);
    types[c->view.type_count++] = (script_catalog_type){
        .id = id,
        .name = "SceneGlobals",
        .type = scope,
        .fields = owned,
        .field_count = count,
    };
    c->view.types = types;
    script_catalog_global *globals = grow(c, c->view.globals, c->view.global_count, 1, sizeof *globals);
    globals[c->view.global_count++] = (script_catalog_global){
        .id = script_catalog_id("global", "globals"),
        .name = "globals",
        .type = scope,
    };
    c->view.globals = globals;
}
/* Each action is the event of its name, so an action named like a host
 * event (Tick, Begin, ...) would dispatch that event when pressed. Project
 * events may share an action's name: both raise the same event. */
static bool actions_unclaimed(source_catalog *c, const char *path)
{
    bool unclaimed = true;
    for (int i = 0; i < acount(c->input); i++) {
        const script_catalog_capability *event = find_event(c, c->input[i].name, c->native_capabilities);
        if (!event) {
            continue;
        }
        char message[sizeof c->diagnostics->message];
        snprintf(
            message,
            sizeof message,
            "Input action %s takes the name of the event %s; rename the action",
            c->input[i].name,
            event->qualified_name ? event->qualified_name : event->name
        );
        error(c, path, (script_range){ 0 }, message);
        unclaimed = false;
    }
    return unclaimed;
}
/* The input map's actions are events, and `input.<Action>` reads one: an
 * InputMap field per action holds its name, whose InputAction type reads
 * held and pressed through the native input capabilities. */
static void input_declarations(source_catalog *c)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s/" INPUT_MAP_FILE, c->root);
    char *text = input_text(c, path);
    char message[192];
    if (!text || !input_map_read(text, &c->input, message, sizeof message)) {
        if (text) {
            error(c, path, (script_range){ 0 }, message);
        }
        free(text);
        return;
    }
    const char *owned_text = own(c, text);
    free(text);
    if (!acount(c->input)) {
        return;
    }
    const char *getters[] = { INPUT_ACTION_HELD, INPUT_ACTION_PRESSED };
    script_catalog_field *reads = arena_alloc(&c->storage, 2 * sizeof *reads);
    for (int i = 0; i < 2; i++) {
        uint64_t id = script_catalog_id("capability", getters[i]);
        if (!script_catalog_capability_by_id(&c->view, id)) {
            error(c, path, (script_range){ 0 }, "Input actions need the native input capabilities");
            return;
        }
        reads[i] = (script_catalog_field){
            .id = script_catalog_id("field", i ? "InputAction.pressed" : "InputAction.held"),
            .name = i ? "pressed" : "held",
            .type = { .storage = PIN_BOOL },
            .read = id,
        };
    }
    if (script_catalog_find_type(&c->view, INPUT_ACTION_TYPE) >= 0 ||
        script_catalog_find_type(&c->view, INPUT_MAP_TYPE) >= 0) {
        error(
            c,
            path,
            (script_range){ 0 },
            "Input actions need the type names " INPUT_ACTION_TYPE
            " and " INPUT_MAP_TYPE "; rename the type that uses one"
        );
        return;
    }
    if (has_global(c, "input")) {
        error(c, path, (script_range){ 0 }, "Input actions need the global name input; rename the global that uses it");
        return;
    }
    if (!actions_unclaimed(c, path)) {
        return;
    }
    uint64_t action_type = script_catalog_id("type", INPUT_ACTION_TYPE);
    uint64_t map_type = script_catalog_id("type", INPUT_MAP_TYPE);
    int count = acount(c->input);
    script_catalog_field *actions = arena_alloc(&c->storage, (size_t)count * sizeof *actions);
    for (int i = 0; i < count; i++) {
        const char *name = own(c, c->input[i].name);
        size_t size = strlen(name) + sizeof "InputMap.";
        char *field = arena_alloc(&c->storage, size);
        snprintf(field, size, "InputMap.%s", name);
        actions[i] = (script_catalog_field){
            .id = script_catalog_id("field", field),
            .name = name,
            .type = { .storage = PIN_STR, .nominal = action_type },
            .constant = true,
        };
        named_event(c, path, name);
    }
    script_catalog_type *types = grow(c, c->view.types, c->view.type_count, 2, sizeof *types);
    types[c->view.type_count] = (script_catalog_type){
        .id = action_type,
        .name = INPUT_ACTION_TYPE,
        .type = { .storage = PIN_STR, .nominal = action_type },
        .fields = reads,
        .field_count = 2,
    };
    types[c->view.type_count + 1] = (script_catalog_type){
        .id = map_type,
        .name = INPUT_MAP_TYPE,
        .type = { .storage = PIN_STR, .nominal = map_type },
        .fields = actions,
        .field_count = count,
    };
    c->view.types = types;
    c->view.type_count += 2;
    script_catalog_global *globals = grow(c, c->view.globals, c->view.global_count, 1, sizeof *globals);
    globals[c->view.global_count++] = (script_catalog_global){
        .id = script_catalog_id("global", "input"),
        .name = "input",
        .type = { .storage = PIN_STR, .nominal = map_type },
    };
    c->view.globals = globals;
    input_definitions(c, own(c, path), owned_text);
}
static script_catalog_parameter *parameters(
    source_catalog *c,
    const char *path,
    const script_analysis *a,
    const int *symbols,
    int count
)
{
    script_catalog_parameter *values = arena_alloc(&c->storage, (size_t)count * sizeof *values);
    for (int i = 0; i < count; i++) {
        const script_symbol *symbol = &a->symbols[symbols[i]];
        values[i] = (script_catalog_parameter){
            .name = own(c, symbol->name),
            .unit = own(c, symbol->unit),
            .type = symbol->type,
            .reference_kind = script_reference_kind_for_type(symbol->type)
        };
        if (symbol->initializer >= 0) {
            const script_expression *expression = &a->expressions[symbol->initializer];
            bool zero = expression->kind == SCRIPT_EXPR_NIL ||
                        ((expression->kind == SCRIPT_EXPR_STRUCT ||
                          expression->kind == SCRIPT_EXPR_ARRAY) &&
                         !expression->argument_count);
            if (expression->kind == SCRIPT_EXPR_CONSTANT || zero) {
                pin_value value = zero ? (pin_value){ .type = symbol->type.storage,
                                                      .container = symbol->type.container }
                                       : expression->constant;
                const script_catalog_type
                    *type = script_catalog_type_by_id(&c->view, symbol->type.nominal);
                if (type) {
                    value.type_name = type->name;
                }
                values[i].has_default = !pin_value_capture_to_arena(
                    &c->storage,
                    &values[i].default_value,
                    &value
                );
                if (!values[i].has_default) {
                    error(c, path, symbol->declaration, "Cannot retain declaration default");
                }
            } else {
                error(c, path, symbol->declaration, "Project signature default requires a constant value");
            }
        }
    }
    return values;
}
static int compare_sources(const void *left, const void *right)
{
    return strcmp(
        ((const source_catalog_source *)left)->path,
        ((const source_catalog_source *)right)->path
    );
}
static void callable_ports(source_catalog *c, const script_analysis *a, int function, script_catalog_capability *value)
{
    const char **entries = NULL, **outcomes = NULL;
    apush(entries, own(c, "entry"));
    for (int i = 0; i < a->function_count; i++) {
        const script_function *child = &a->functions[i];
        if (child->kind == SCRIPT_FUNCTION_MACRO_ENTRY && child->parent == function &&
            child->entry && strcmp(child->entry, "entry")) {
            apush(entries, own(c, child->entry));
        }
    }
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *symbol = &a->symbols[i];
        if (symbol->kind == SCRIPT_LABEL && symbol->outcome &&
            symbol->scope == a->functions[function].scope) {
            apush(outcomes, own(c, symbol->name));
        }
    }
    if (!acount(outcomes)) {
        apush(outcomes, own(c, "Then"));
    }
    value->entry_count = acount(entries);
    value->outcome_count = acount(outcomes);
    const char **retained = arena_alloc(&c->storage, (size_t)value->entry_count * sizeof *retained);
    memcpy(retained, entries, (size_t)value->entry_count * sizeof *retained);
    value->entries = retained;
    retained = arena_alloc(&c->storage, (size_t)value->outcome_count * sizeof *retained);
    memcpy(retained, outcomes, (size_t)value->outcome_count * sizeof *retained);
    value->outcomes = retained;
    value->completion_outcome = -1;
    for (int i = 0; i < value->outcome_count; i++) {
        if (!strcmp(value->outcomes[i], "Then")) {
            value->completion_outcome = i;
        }
    }
    afree(entries);
    afree(outcomes);
}

static bool same_compile_value(script_compile_value a, script_compile_value b)
{
    if (a.is_type != b.is_type) {
        return false;
    }
    if (a.is_type) {
        return script_type_equal(a.type, b.type);
    }
    if (a.value.type != b.value.type || a.value.container != b.value.container) {
        return false;
    }
    if (strcmp(
            a.value.type_name ? a.value.type_name : "",
            b.value.type_name ? b.value.type_name : ""
        )) {
        return false;
    }
    if (a.value.type == PIN_FLOW) {
        return true; /* An executable specialization has no compile-time result. */
    }
    if (a.value.container == PIN_CONTAINER_SINGLE && a.value.type == PIN_F32) {
        return !memcmp(&a.value.f, &b.value.f, sizeof a.value.f);
    }
    if (a.value.container == PIN_CONTAINER_SINGLE && a.value.type == PIN_F64) {
        return !memcmp(&a.value.d, &b.value.d, sizeof a.value.d);
    }
    if (a.value.container == PIN_CONTAINER_SINGLE &&
        ((a.value.type >= PIN_BOOL && a.value.type <= PIN_F64) ||
         a.value.type == PIN_ENUM || a.value.type == PIN_HANDLE)) {
        return behavior_scalar_bits(a.value) == behavior_scalar_bits(b.value);
    }
    return pin_value_equal(&a.value, &b.value);
}

static bool same_compile_bindings(
    const script_compile_binding *a,
    const script_compile_binding *b,
    int count
)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(a[i].name, b[i].name) ||
            !same_compile_value(a[i].value, b[i].value)) {
            return false;
        }
    }
    return true;
}

static const script_compile_binding *own_compile_bindings(
    source_catalog *c,
    const char *path,
    const script_compile_binding *bindings,
    int count
)
{
    script_compile_binding *owned = arena_alloc(&c->storage, (size_t)count * sizeof *owned);
    for (int i = 0; i < count; i++) {
        owned[i] = bindings[i];
        owned[i].name = (char *)own(c, bindings[i].name);
        if (!bindings[i].value.is_type && pin_value_capture_to_arena(
                                              &c->storage,
                                              &owned[i].value.value,
                                              &bindings[i].value.value
                                          )) {
            error(c, path, (script_range){ 0 }, "Cannot retain imported specialization argument");
            return NULL;
        }
    }
    return owned;
}

static bool export_templates(source_catalog *c, const source_catalog_source *source, const script_analysis *analysis)
{
    if (analysis->kind != SCRIPT_PROGRAM_LIBRARY) {
        return false;
    }
    bool changed = false;
    for (int i = 0; i < analysis->template_count; i++) {
        const script_catalog_template *input = &analysis->templates[i];
        int existing = -1;
        for (int j = 0; j < acount(c->templates); j++) {
            if (c->templates[j].id == input->id) {
                existing = j;
                break;
            }
        }
        if (existing >= 0) {
            const script_catalog_template *previous = &c->templates[existing];
            bool equal = previous->kind == input->kind &&
                         previous->generic == input->generic &&
                         previous->parameter_count == input->parameter_count;
            for (int j = 0; equal && j < input->parameter_count; j++) {
                const script_template_parameter *a = &previous->parameters[j],
                                                *b = &input->parameters[j];
                equal = !strcmp(a->name, b->name) && (!a->binder == !b->binder) &&
                        (!a->binder || !strcmp(a->binder, b->binder)) &&
                        script_type_equal(a->type, b->type) &&
                        a->comptime == b->comptime && a->type_value == b->type_value &&
                        a->binds_type == b->binds_type;
            }
            if (equal) {
                continue;
            }
        }
        script_catalog_template value = *input;
        size_t length = strlen(analysis->name) + strlen(input->name) + 2;
        char *qualified = arena_alloc(&c->storage, length);
        snprintf(qualified, length, "%s.%s", analysis->name, input->name);
        value.name = qualified;
        value.owner = own(c, source->identity);
        script_template_parameter *parameters = arena_alloc(
            &c->storage,
            (size_t)value.parameter_count * sizeof *parameters
        );
        for (int j = 0; j < value.parameter_count; j++) {
            parameters[j] = input->parameters[j];
            parameters[j].name = own(c, parameters[j].name);
            parameters[j].binder = own(c, parameters[j].binder);
        }
        value.parameters = parameters;
        for (int j = 0; j < acount(c->templates); j++) {
            if (j != existing && !strcmp(c->templates[j].name, value.name)) {
                TSNode declaration = ts_node_named_descendant_for_byte_range(
                    ts_tree_root_node(source->syntax->tree),
                    (uint32_t)value.start,
                    (uint32_t)value.end
                );
                TSNode name = ts_node_child_by_field_name(declaration, "name", 4);
                char message[192];
                snprintf(
                    message,
                    sizeof message,
                    "Duplicate exported %s name '%s'",
                    value.kind == SCRIPT_TEMPLATE_RECORD ? "type" : "callable",
                    value.name
                );
                error(c, source->path, script_node_range(name), message);
                return changed;
            }
        }
        if (existing < 0) {
            apush(c->templates, value);
            TSNode declaration = ts_node_named_descendant_for_byte_range(
                ts_tree_root_node(source->syntax->tree),
                (uint32_t)value.start,
                (uint32_t)value.end
            );
            TSNode name = ts_node_child_by_field_name(declaration, "name", 4);
            if (!ts_node_is_null(name)) {
                apush(
                    c->definitions,
                    ((source_catalog_definition){
                        .id = value.id,
                        .kind = SCRIPT_CAPABILITY_FUNCTION,
                        .name = own(c, input->name),
                        .path = source->path,
                        .text = source->text,
                        .range = script_node_range(name),
                        .text_kind = SOURCE_TEXT_CHIBI,
                    })
                );
            }
        } else {
            c->templates[existing] = value;
        }
        changed = true;
    }
    update_view(c);
    return changed;
}

static bool collect_specialization_requests(
    source_catalog *c,
    const source_catalog_source *source,
    const script_analysis *analysis
)
{
    bool changed = false;
    for (int i = 0; i < analysis->request_count; i++) {
        const script_specialization_request *input = &analysis->requests[i];
        bool known = false;
        for (int j = 0; j < acount(c->requests); j++) {
            const script_specialization_request *existing = &c->requests[j];
            if (existing->id != input->id) {
                continue;
            }
            known = true;
            if (existing->template_id != input->template_id ||
                existing->evaluate != input->evaluate ||
                strcmp(existing->owner, input->owner) ||
                existing->binding_count != input->binding_count ||
                !same_compile_bindings(
                    existing->bindings,
                    input->bindings,
                    input->binding_count
                )) {
                error(c, source->path, input->origin, "Imported specialization identity collides with different arguments");
            }
            break;
        }
        if (known) {
            continue;
        }
        if (acount(c->requests) >= MAX_SPECIALIZATIONS) {
            error(c, source->path, input->origin, "Project polymorphic specialization budget exceeded");
            return changed;
        }
        script_specialization_request request = *input;
        request.owner = own(c, input->owner);
        request.bindings = own_compile_bindings(
            c,
            source->path,
            input->bindings,
            input->binding_count
        );
        if (request.binding_count && !request.bindings) {
            return changed;
        }
        apush(c->requests, request);
        apush(c->request_paths, own(c, source->path));
        changed = true;
    }
    return changed;
}

static bool export_specialization_types(
    source_catalog *c,
    const source_catalog_source *source,
    const script_analysis *analysis
)
{
    if (analysis->kind != SCRIPT_PROGRAM_LIBRARY) {
        return false;
    }
    if (!analysis->type_count) {
        return false;
    }
    script_catalog input = { .types = analysis->types,
                             .type_count = analysis->type_count },
                   owned;
    bool valid = script_catalog_snapshot(&c->storage, &input, &owned);
    if (!valid) {
        error(c, source->path, (script_range){ 0 }, "Cannot retain specialized record layout");
        return false;
    }
    bool changed = false;
    for (int i = 0; i < owned.type_count; i++) {
        script_catalog_type value = owned.types[i];
        if (!value.internal) {
            const char *local = value.source_name ? value.source_name : value.name;
            size_t length = strlen(analysis->name) + strlen(local) + 2;
            char *qualified = arena_alloc(&c->storage, length);
            snprintf(qualified, length, "%s.%s", analysis->name, local);
            value.name = qualified;
            value.source_name = local;
        }
        int existing = -1;
        for (int j = 0; j < c->view.type_count; j++) {
            if (c->view.types[j].id == value.id) {
                existing = j;
                break;
            }
        }
        if (existing >= 0) {
            script_catalog left = { .types = &c->view.types[existing],
                                    .type_count = 1 },
                           right = { .types = &value, .type_count = 1 };
            unsigned char *left_defaults = NULL, *right_defaults = NULL;
            bool equal = source_catalog_metadata_equal(&left, &right) &&
                         source_catalog_default_key(&left, &left_defaults) &&
                         source_catalog_default_key(&right, &right_defaults) &&
                         acount(left_defaults) == acount(right_defaults) &&
                         (!acount(left_defaults) ||
                          !memcmp(left_defaults, right_defaults, (size_t)acount(left_defaults)));
            afree(left_defaults);
            afree(right_defaults);
            if (equal) {
                continue;
            }
            ((script_catalog_type *)c->view.types)[existing] = value;
        } else {
            script_catalog_type *types = grow(c, c->view.types, c->view.type_count, 1, sizeof *types);
            types[c->view.type_count++] = value;
            c->view.types = types;
        }
        changed = true;
    }
    return changed;
}

static bool export_specializations(
    source_catalog *c,
    const source_catalog_source *source,
    const script_analysis *analysis
)
{
    if (analysis->kind != SCRIPT_PROGRAM_LIBRARY) {
        return false;
    }
    bool changed = false;
    for (int i = 0; i < analysis->specialization_count; i++) {
        const script_catalog_specialization *input = &analysis->specializations[i];
        int index = -1;
        for (int j = 0; j < acount(c->specializations); j++) {
            if (c->specializations[j].id == input->id) {
                index = j;
                break;
            }
        }
        if (index >= 0) {
            const script_catalog_specialization *existing = &c->specializations[index];
            if (existing->template_id != input->template_id ||
                existing->binding_count != input->binding_count ||
                !same_compile_bindings(
                    existing->bindings,
                    input->bindings,
                    input->binding_count
                )) {
                error(c, source->path, (script_range){ 0 }, "Specialization identity collides with another binding tuple");
                continue;
            }
            if (existing->executable == input->executable &&
                same_compile_value(existing->result, input->result)) {
                continue;
            }
        } else if (acount(c->specializations) >= MAX_SPECIALIZATIONS) {
            error(c, source->path, (script_range){ 0 }, "Project polymorphic specialization budget exceeded");
            return changed;
        }
        script_catalog_specialization value = *input;
        value.bindings = own_compile_bindings(
            c,
            source->path,
            input->bindings,
            input->binding_count
        );
        if (value.binding_count && !value.bindings) {
            continue;
        }
        if (!value.result.is_type && pin_value_capture_to_arena(
                                         &c->storage,
                                         &value.result.value,
                                         &input->result.value
                                     )) {
            error(c, source->path, (script_range){ 0 }, "Cannot retain compile-time specialization result");
            continue;
        }
        if (index < 0) {
            apush(c->specializations, value);
        } else {
            c->specializations[index] = value;
        }
        changed = true;
    }
    update_view(c);
    return changed;
}

static bool specialized_function(const script_analysis *analysis, const script_function *function, uint64_t id)
{
    for (int i = 0; i < analysis->specialization_count; i++) {
        if (analysis->specializations[i].id == id &&
            analysis->specializations[i].executable) {
            return true;
        }
    }
    for (int i = 0; i < analysis->template_count; i++) {
        const script_catalog_template *definition = &analysis->templates[i];
        if (definition->generic && definition->id != id &&
            definition->start == function->range.start &&
            definition->end == function->range.end) {
            return true;
        }
    }
    return false;
}

static bool export_functions(
    source_catalog *c,
    const source_catalog_source *source,
    const script_analysis *analysis,
    bool update
)
{
    if (analysis->kind != SCRIPT_PROGRAM_LIBRARY) {
        return false;
    }
    bool changed = false;
    for (int i = 0; i < analysis->function_count; i++) {
        const script_function *function = &analysis->functions[i];
        if (function->kind != SCRIPT_FUNCTION_AUTHORED ||
            function->symbol < 0 || function->parent >= 0) {
            continue;
        }
        const script_symbol *symbol = &analysis->symbols[function->symbol];
        bool specialized = specialized_function(analysis, function, symbol->id);
        bool requested = false, published = false;
        for (int j = 0; j < acount(c->requests); j++) {
            requested |= c->requests[j].id == symbol->id;
        }
        for (int j = 0; j < analysis->specialization_count; j++) {
            published |= analysis->specializations[j].id == symbol->id &&
                         analysis->specializations[j].executable;
        }
        if (specialized && requested && !published) {
            continue;
        }
        char suffix[40] = { 0 };
        if (specialized) {
            snprintf(
                suffix,
                sizeof suffix,
                "$%016llx",
                (unsigned long long)symbol->id
            );
        }
        size_t length = strlen(analysis->name) + strlen(symbol->name) +
                        strlen(suffix) + 2;
        char *qualified = arena_alloc(&c->storage, length);
        snprintf(qualified, length, "%s.%s%s", analysis->name, symbol->name, suffix);
        script_catalog_capability value = {
            .id = symbol->id,
            .name = qualified + strlen(analysis->name) + 1,
            .qualified_name = qualified,
            .kind = symbol->kind == SCRIPT_MACRO ? SCRIPT_CAPABILITY_ACTION
                                                 : SCRIPT_CAPABILITY_FUNCTION,
            .parameters = parameters(
                c,
                source->path,
                analysis,
                function->parameters,
                function->parameter_count
            ),
            .parameter_count = function->parameter_count,
            .results = parameters(
                c,
                source->path,
                analysis,
                function->results,
                function->result_count
            ),
            .result_count = function->result_count,
            .pure = function->pure,
            .effects = function->effects,
        };
        callable_ports(c, analysis, i, &value);
        int existing = -1;
        for (int j = 0; j < acount(c->capabilities); j++) {
            if (c->capabilities[j].id == value.id) {
                existing = j;
                break;
            }
        }
        if (existing >= 0 && update) {
            if (existing < c->native_capabilities) {
                error(c, source->path, symbol->declaration, "Specialized authored callable collides with native identity");
                continue;
            }
            const script_catalog_capability *previous = &c->capabilities[existing];
            bool contract_changed = script_catalog_capability_abi(&c->view, previous) !=
                                    script_catalog_capability_abi(&c->view, &value);
            changed |= contract_changed || previous->effects != value.effects ||
                       previous->pure != value.pure;
            c->capabilities[existing] = value;
        } else {
            int before = acount(c->capabilities);
            exported_callable(c, source->path, symbol->declaration, value);
            changed |= acount(c->capabilities) != before;
        }
    }
    update_view(c);
    return changed;
}

static int compare_requests(const void *left, const void *right)
{
    const script_specialization_request *a = left, *b = right;
    return (a->id > b->id) - (a->id < b->id);
}

static void sort_requests(source_catalog *c)
{
    for (int i = 1; i < acount(c->requests); i++) {
        for (int j = i; j > 0 && c->requests[j].id < c->requests[j - 1].id; j--) {
            script_specialization_request request = c->requests[j];
            c->requests[j] = c->requests[j - 1];
            c->requests[j - 1] = request;
            const char *path = c->request_paths[j];
            c->request_paths[j] = c->request_paths[j - 1];
            c->request_paths[j - 1] = path;
        }
    }
}

static script_analysis *analyze_requested(source_catalog *c, const source_catalog_source *source)
{
    script_specialization_request *requests = NULL;
    for (int i = 0; i < acount(c->requests); i++) {
        if (!strcmp(c->requests[i].owner, source->identity)) {
            apush(requests, c->requests[i]);
        }
    }
    asort(requests, compare_requests);
    script_analysis *analysis = script_analyze_document_requested(
        source->syntax,
        &c->view,
        source->identity,
        requests,
        acount(requests)
    );
    afree(requests);
    return analysis;
}

static void library_specializations(source_catalog *c)
{
    if (!acount(c->templates)) {
        return;
    }
    for (int pass = 0; pass <= MAX_SPECIALIZATIONS && !acount(c->diagnostics); pass++) {
        bool changed = false;
        for (int i = 0; i < acount(c->sources); i++) {
            source_catalog_source *source = &c->sources[i];
            script_analysis *analysis = analyze_requested(c, source);
            changed |= export_templates(c, source, analysis);
            changed |= collect_specialization_requests(c, source, analysis);
            changed |= export_specialization_types(c, source, analysis);
            changed |= export_specializations(c, source, analysis);
            changed |= export_functions(c, source, analysis, true);
            script_analysis_free(analysis);
            if (acount(c->diagnostics)) {
                break;
            }
        }
        if (!changed) {
            for (int i = 0; i < acount(c->requests); i++) {
                bool resolved = false;
                for (int j = 0; j < acount(c->specializations); j++) {
                    resolved |= c->requests[i].id == c->specializations[j].id;
                }
                if (!resolved) {
                    bool owner_diagnosed = false;
                    for (int owner = 0; owner < acount(c->sources); owner++) {
                        const source_catalog_source *source = &c->sources[owner];
                        if (strcmp(source->identity, c->requests[i].owner)) {
                            continue;
                        }
                        script_analysis *analysis = analyze_requested(c, source);
                        owner_diagnosed = analysis->diagnostic_count > 0;
                        for (int j = 0; j < analysis->diagnostic_count; j++) {
                            bool duplicate = false;
                            for (int k = 0; k < acount(c->diagnostics); k++) {
                                const source_catalog_diagnostic *existing = &c->diagnostics[k];
                                duplicate |= !strcmp(existing->path, source->path) &&
                                             existing->range.start ==
                                                 analysis->diagnostics[j].range.start &&
                                             !strcmp(
                                                 existing->message,
                                                 analysis->diagnostics[j].message
                                             );
                            }
                            if (!duplicate) {
                                error(
                                    c,
                                    source->path,
                                    analysis->diagnostics[j].range,
                                    analysis->diagnostics[j].message
                                );
                            }
                        }
                        script_analysis_free(analysis);
                        break;
                    }
                    if (!owner_diagnosed) {
                        error(c, c->request_paths[i], c->requests[i].origin, "Imported template has no concrete specialization");
                    }
                }
            }
            return;
        }
    }
    if (!acount(c->diagnostics)) {
        error(c, c->root, (script_range){ 0 }, "Imported specialization fixed-point budget exceeded");
    }
}

static void discover_templates(source_catalog *c)
{
    for (int pass = 0; pass <= acount(c->sources); pass++) {
        bool changed = false;
        for (int i = 0; i < acount(c->sources); i++) {
            source_catalog_source *source = &c->sources[i];
            script_analysis *analysis = script_declarations_document(
                source->syntax,
                &c->view,
                source->identity
            );
            changed |= export_templates(c, source, analysis);
            script_analysis_free(analysis);
        }
        if (!changed) {
            return;
        }
    }
    error(c, c->root, (script_range){ 0 }, "Template signature discovery budget exceeded");
}
bool source_catalog_identity_valid(const char *key)
{
    if (!key || !key[0] || strlen(key) >= MAX_PATH || key[0] == '/' ||
        strchr(key, '\\')) {
        return false;
    }
    const char *start = key;
    while (true) {
        const char *end = strchr(start, '/');
        size_t size = end ? (size_t)(end - start) : strlen(start);
        if (!size || (size == 1 && start[0] == '.') ||
            (size == 2 && !strncmp(start, "..", 2))) {
            return false;
        }
        if (!end) {
            return true;
        }
        start = end + 1;
    }
}
bool source_catalog_source_path(const char *root, const char *path)
{
    size_t length = strlen(root);
    while (length && root[length - 1] == '/') {
        length--;
    }
    if (strncmp(root, path, length) || path[length] != '/') {
        return false;
    }
    const char *relative = path + length + 1;
    if (strlen(path) >= MAX_PATH) {
        return false;
    }
    int depth = 0;
    for (const char *p = relative; *p; p++) {
        depth += *p == '/';
    }
    if (depth > MAX_DEPTH || strstr(relative, "../") || strstr(relative, "/..") ||
        strstr(relative, "./") || strstr(relative, "//")) {
        return false;
    }
    return (!strncmp(relative, "behaviors/", 10) ||
            !strncmp(relative, "libraries/", 10) ||
            !strncmp(relative, "systems/", 8)) &&
           strlen(relative) > strlen(CODE_ASSET_EXTENSION) &&
           !strcmp(relative + strlen(relative) - strlen(CODE_ASSET_EXTENSION), CODE_ASSET_EXTENSION);
}
bool source_catalog_edit_path(const char *root, const char *path)
{
    if (source_catalog_source_path(root, path)) {
        return true;
    }
    size_t length = strlen(root);
    while (length && root[length - 1] == '/') {
        length--;
    }
    if (strncmp(root, path, length) || path[length] != '/' || strlen(path) >= MAX_PATH) {
        return false;
    }
    const char *relative = path + length + 1;
    if (!source_catalog_identity_valid(relative)) {
        return false;
    }
    int depth = 0;
    for (const char *p = relative; *p; p++) {
        depth += *p == '/';
    }
    if (depth > MAX_DEPTH) {
        return false;
    }
    if (!strcmp(relative, INPUT_MAP_FILE)) {
        return true;
    }
    const char *folders[] = { "events/", "prototypes/", "scenes/", "ui/", "relationships/" };
    for (int i = 0; i < 5; i++) {
        const char *extension = i == 3 ? ".rml"
                              : i == 4 ? ".chibi"
                                       : DECLARATIVE_ASSET_EXTENSION;
        size_t size = strlen(relative), suffix = strlen(extension);
        if (!strncmp(relative, folders[i], strlen(folders[i])) &&
            ((size > suffix && !strcmp(relative + size - suffix, extension)) ||
             (i == 1 && size > 6 && !strcmp(relative + size - 6, ".chibi")))) {
            return true;
        }
    }
    return false;
}
static declaration_kind edit_kind(const char *root, const char *path)
{
    assert(source_catalog_edit_path(root, path));
    if (source_catalog_source_path(root, path)) {
        return DECLARATION_SOURCE;
    }
    size_t length = strlen(root);
    while (length && root[length - 1] == '/') {
        length--;
    }
    const char *relative = path + length + 1;
    return !strncmp(relative, "events/", 7)         ? DECLARATION_EVENT
         : !strncmp(relative, "prototypes/", 11)    ? DECLARATION_PROTOTYPE
         : !strncmp(relative, "relationships/", 14) ? DECLARATION_RELATIONSHIP
         : !strncmp(relative, "ui/", 3)             ? DECLARATION_UI
                                                    : DECLARATION_SCENE;
}
/* Signatures must all exist before bodies can establish imported effects.
 * Effects only grow; N callables require at most N dependency propagation
 * passes. Invalid or unresolved libraries export a conservative contract. */
static bool library_effect_pass(source_catalog *c, bool unresolved)
{
    const unsigned unknown = SCRIPT_EFFECT_READ | SCRIPT_EFFECT_WRITE |
                             SCRIPT_EFFECT_STRUCTURAL | SCRIPT_EFFECT_HOST |
                             SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_RANDOM |
                             SCRIPT_EFFECT_INVOCATION_READ;
    bool changed = false;
    for (int i = 0; i < acount(c->sources); i++) {
        source_catalog_source *source = &c->sources[i];
        if (!source->declarations ||
            source->declarations->kind != SCRIPT_PROGRAM_LIBRARY) {
            continue;
        }
        script_analysis *analysis = analyze_requested(c, source);
        for (int j = 0; j < analysis->function_count; j++) {
            const script_function *function = &analysis->functions[j];
            if (function->kind != SCRIPT_FUNCTION_AUTHORED ||
                function->symbol < 0 || function->parent >= 0) {
                continue;
            }
            uint64_t id = analysis->symbols[function->symbol].id;
            for (int k = c->native_capabilities; k < acount(c->capabilities); k++) {
                script_catalog_capability *cap = &c->capabilities[k];
                if (cap->id != id) {
                    continue;
                }
                unsigned effects = cap->effects | function->effects;
                if (unresolved && (!analysis->valid || function->body < 0)) {
                    effects |= unknown;
                }
                changed |= effects != cap->effects;
                cap->effects = effects;
                cap->pure = analysis->valid && function->pure &&
                            !(effects & ~(SCRIPT_EFFECT_READ |
                                          SCRIPT_EFFECT_INVOCATION_READ));
                break;
            }
        }
        script_analysis_free(analysis);
    }
    return changed;
}
static void library_effects(source_catalog *c)
{
    int count = acount(c->capabilities) - c->native_capabilities;
    for (int pass = 0; pass <= count; pass++) {
        if (!library_effect_pass(c, false)) {
            break;
        }
    }
    /* Resolve provisional signature-only assumptions before consumers see the
     * catalog, then propagate any conservative fallback through import chains. */
    for (int pass = 0; pass <= count; pass++) {
        if (!library_effect_pass(c, true)) {
            break;
        }
    }
}
static source_catalog *build(
    const char *root,
    const script_catalog *native,
    const source_catalog_edit *edits,
    int edit_count,
    bool replace
)
{
    assert(root && native && edit_count >= 0 && (!edit_count || edits));
    source_catalog *c = xcalloc(1, sizeof *c);
    size_t root_length = strlen(root);
    while (root_length && root[root_length - 1] == '/') {
        root_length--;
    }
    char *normalized = arena_alloc(&c->storage, root_length + 1);
    memcpy(normalized, root, root_length);
    normalized[root_length] = 0;
    root = c->root = normalized;
    if (!script_catalog_snapshot(&c->storage, native, &c->view)) {
        error(c, root, (script_range){ 0 }, "Cannot retain native declaration defaults");
        return c;
    }
    c->native_capabilities = c->view.capability_count;
    if (edit_count > MAX_SOURCES) {
        error(c, root, (script_range){ 0 }, "Executable edit count limit exceeded");
        return c;
    }
    c->edits = edits;
    c->edit_count = edit_count;
    c->edit_used = xcalloc((size_t)edit_count, sizeof *c->edit_used);
    for (int i = 0; i < edit_count; i++) {
        if (!edits[i].path || !source_catalog_edit_path(root, edits[i].path)) {
            error(c, root, (script_range){ 0 }, "Edit path is outside the authored folders");
            c->edit_used[i] = true;
        }
        if (edits[i].identity &&
            (!source_catalog_identity_valid(edits[i].identity) || !edits[i].path ||
             !source_catalog_source_path(root, edits[i].path))) {
            error(c, root, (script_range){ 0 }, "Invalid executable document identity");
            c->edit_used[i] = true;
        }
        for (int j = 0; j < i; j++) {
            if (edits[i].path && edits[j].path &&
                !strcmp(edits[i].path, edits[j].path)) {
                error(c, edits[i].path, (script_range){ 0 }, "Duplicate executable edit path");
                c->edit_used[i] = true;
            }
        }
    }
    for (int i = 0; i < c->view.capability_count; i++) {
        apush(c->capabilities, c->view.capabilities[i]);
    }
    for (int i = 0; i < c->view.relationship_count; i++) {
        apush(c->relationships, c->view.relationships[i]);
    }
    for (int i = 0; i < c->view.template_count; i++) {
        apush(c->templates, c->view.templates[i]);
    }
    for (int i = 0; i < c->view.specialization_count; i++) {
        apush(c->specializations, c->view.specializations[i]);
    }
    update_view(c);
    const char *folders[] = { "events",    "prototypes",   "libraries",
                              "behaviors", "systems",      "scenes",
                              "ui",        "relationships" };
    const declaration_kind kinds[] = {
        DECLARATION_EVENT,  DECLARATION_PROTOTYPE,   DECLARATION_SOURCE,
        DECLARATION_SOURCE, DECLARATION_SOURCE,      DECLARATION_SCENE,
        DECLARATION_UI,     DECLARATION_RELATIONSHIP
    };
    for (int i = 0; i < 8; i++) {
        if (replace && kinds[i] == DECLARATION_SOURCE) {
            continue;
        }
        char path[MAX_PATH];
        int length = snprintf(path, sizeof path, "%s/%s", root, folders[i]);
        if (length < 0 || length >= (int)sizeof path) {
            error(c, root, (script_range){ 0 }, "Content root path limit exceeded");
            continue;
        }
        scan(c, path, 0, kinds[i]);
    }
    input_declarations(c);
    for (int i = 0; i < edit_count; i++) {
        if (!c->edit_used[i] && edits[i].text) {
            load_file(c, edits[i].path, edit_kind(root, edits[i].path));
        }
    }
    global_declarations(c);
    free(c->edit_used);
    c->edit_used = NULL;
    c->edits = NULL;
    c->edit_count = 0;
    asort(c->sources, compare_sources);
    for (int i = 0; i < acount(c->sources); i++) {
        for (int j = 0; j < i; j++) {
            if (!strcmp(c->sources[i].identity, c->sources[j].identity)) {
                error(c, c->sources[i].path, (script_range){ 0 }, "Duplicate executable document identity");
            }
        }
    }
    discover_templates(c);
    declared_types(c);
    for (int i = 0; i < acount(c->sources); i++) {
        source_catalog_source *source = &c->sources[i];
        script_analysis
            *a = script_declarations_document(source->syntax, &c->view, source->identity);
        source->declarations = a;
        for (int j = 0; !a->request_count && j < a->diagnostic_count; j++) {
            error(c, source->path, a->diagnostics[j].range, a->diagnostics[j].message);
        }
        if (!a->valid || a->kind != SCRIPT_PROGRAM_LIBRARY) {
            continue;
        }
        export_templates(c, source, a);
        export_functions(c, source, a, false);
    }
    library_specializations(c);
    sort_requests(c);
    library_effects(c);
    for (int i = 0; i < acount(c->references); i++) {
        source_catalog_reference *reference = &c->references[i];
        if (reference->kind == SOURCE_REFERENCE_RELATIONSHIP) {
            for (int j = 0; j < c->view.relationship_count; j++) {
                if (!strcmp(c->view.relationships[j].name, reference->name)) {
                    reference->id = c->view.relationships[j].id;
                    break;
                }
            }
            if (!reference->id) {
                error(c, reference->path, reference->range, "Unknown authored relationship");
            }
            continue;
        }
        if (reference->kind == SOURCE_REFERENCE_PROGRAM ||
            reference->kind == SOURCE_REFERENCE_SYSTEM) {
            for (int j = 0; j < acount(c->sources); j++) {
                const script_analysis *analysis = c->sources[j].declarations;
                script_program_kind expected = reference->kind == SOURCE_REFERENCE_SYSTEM
                                                 ? SCRIPT_PROGRAM_SYSTEM
                                                 : SCRIPT_PROGRAM_BEHAVIOR;
                if (analysis && analysis->kind == expected && analysis->name &&
                    !strcmp(analysis->name, reference->name)) {
                    reference->id = script_catalog_id("program", analysis->identity);
                    reference->owner = (uint64_t)analysis->kind;
                    break;
                }
            }
            if (!reference->id) {
                char message[192];
                snprintf(
                    message,
                    sizeof message,
                    "Unknown behavior or system '%s'",
                    reference->name
                );
                error(c, reference->path, reference->range, message);
            }
            continue;
        }
        script_capability_kind kind = reference->kind == SOURCE_REFERENCE_EVENT
                                        ? SCRIPT_CAPABILITY_EVENT
                                        : SCRIPT_CAPABILITY_PROTOTYPE;
        for (int j = 0; j < c->view.capability_count; j++) {
            const script_catalog_capability *capability = &c->view.capabilities[j];
            if (capability->kind == kind && !strcmp(capability->name, reference->name)) {
                reference->id = capability->id;
                break;
            }
        }
        if (!reference->id) {
            char message[192];
            snprintf(
                message,
                sizeof message,
                "Unknown %s '%s'",
                kind == SCRIPT_CAPABILITY_EVENT ? "event" : "prototype",
                reference->name
            );
            error(c, reference->path, reference->range, message);
        }
    }
    c->defaults_known = source_catalog_default_key(&c->view, &c->default_key);
    return c;
}
source_catalog *source_catalog_new(const char *root, const script_catalog *native)
{
    return build(root, native, NULL, 0, false);
}
source_catalog *source_catalog_edited(
    const char *root,
    const script_catalog *native,
    const source_catalog_edit *edits,
    int count
)
{
    return build(root, native, edits, count, false);
}
source_catalog *source_catalog_replaced(
    const char *root,
    const script_catalog *native,
    const source_catalog_edit *sources,
    int count
)
{
    return build(root, native, sources, count, true);
}
const script_catalog *source_catalog_view(const source_catalog *c)
{
    return &c->view;
}
const script_specialization_request *source_catalog_requests(const source_catalog *c, int *count)
{
    *count = acount(c->requests);
    return c->requests;
}
static bool authored_template(const source_catalog *c, const script_catalog_template *definition)
{
    if (!definition->owner) {
        return false;
    }
    for (int i = 0; i < acount(c->sources); i++) {
        if (!strcmp(c->sources[i].identity, definition->owner)) {
            return true;
        }
    }
    return false;
}

static script_catalog compatibility_view(
    const source_catalog *c,
    script_catalog_capability **capabilities,
    script_catalog_template **templates
)
{
    script_catalog view = c->view;
    *capabilities = xmalloc((size_t)view.capability_count * sizeof **capabilities);
    *templates = xmalloc((size_t)view.template_count * sizeof **templates);
    if (view.capability_count) {
        memcpy(*capabilities, view.capabilities, (size_t)view.capability_count * sizeof **capabilities);
    }
    if (view.template_count) {
        memcpy(*templates, view.templates, (size_t)view.template_count * sizeof **templates);
    }
    view.capabilities = *capabilities;
    view.templates = *templates;
    /* Source text and the collection's transitive executable dependency graph
     * invalidate authored bodies. Their inferred effects and source locations
     * must not invalidate unrelated source generations globally. Signatures,
     * native contracts, constructed types and compile-time results stay exact. */
    for (int i = c->native_capabilities; i < view.capability_count; i++) {
        script_catalog_capability *capability = &(*capabilities)[i];
        if (capability->kind == SCRIPT_CAPABILITY_FUNCTION ||
            capability->kind == SCRIPT_CAPABILITY_ACTION) {
            capability->effects = 0;
            capability->pure = false;
        }
    }
    for (int i = 0; i < view.template_count; i++) {
        script_catalog_template *definition = &(*templates)[i];
        if (authored_template(c, definition)) {
            definition->source_generation = 0;
            definition->start = definition->end = 0;
        }
    }
    return view;
}

bool source_catalog_same(const source_catalog *a, const source_catalog *b)
{
    int bytes = acount(a->default_key);
    if (!a->defaults_known || !b->defaults_known || bytes != acount(b->default_key) ||
        (bytes && memcmp(a->default_key, b->default_key, (size_t)bytes))) {
        return false;
    }
    script_catalog_capability *left_capabilities, *right_capabilities;
    script_catalog_template *left_templates, *right_templates;
    script_catalog left = compatibility_view(a, &left_capabilities, &left_templates),
                   right = compatibility_view(b, &right_capabilities, &right_templates);
    bool same = source_catalog_metadata_equal(&left, &right);
    free(left_capabilities);
    free(right_capabilities);
    free(left_templates);
    free(right_templates);
    return same;
}
const input_action *source_catalog_input(const source_catalog *c)
{
    return c->input;
}
int source_catalog_source_count(const source_catalog *c)
{
    return acount(c->sources);
}
const source_catalog_source *source_catalog_source_at(const source_catalog *c, int index)
{
    assert(index >= 0 && index < acount(c->sources));
    return &c->sources[index];
}
int source_catalog_definition_count(const source_catalog *c)
{
    return acount(c->definitions);
}
const source_catalog_definition *source_catalog_definitions(const source_catalog *c)
{
    return c->definitions;
}
int source_catalog_reference_count(const source_catalog *c)
{
    return acount(c->references);
}
const source_catalog_reference *source_catalog_references(const source_catalog *c)
{
    return c->references;
}
int source_catalog_diagnostic_count(const source_catalog *c)
{
    return acount(c->diagnostics);
}
const source_catalog_diagnostic *source_catalog_diagnostic_at(const source_catalog *c, int index)
{
    assert(index >= 0 && index < acount(c->diagnostics));
    return &c->diagnostics[index];
}
void source_catalog_free(source_catalog *c)
{
    if (!c) {
        return;
    }
    for (int i = 0; i < acount(c->sources); i++) {
        free((void *)c->sources[i].text);
        script_syntax_free(c->sources[i].syntax);
        script_analysis_free(c->sources[i].declarations);
    }
    afree(c->sources);
    afree(c->definitions);
    afree(c->references);
    afree(c->diagnostics);
    afree(c->capabilities);
    afree(c->relationships);
    afree(c->templates);
    afree(c->specializations);
    afree(c->requests);
    afree(c->request_paths);
    afree(c->default_key);
    input_map_free(c->input);
    arena_free(&c->storage);
    free(c);
}
