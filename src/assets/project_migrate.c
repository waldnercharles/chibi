#include "assets/project_migrate.h"

#include "language/syntax.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    int file;
    TSNode library, name, body, scope;
} function;
typedef struct
{
    const char *const *sources;
    function *functions;
} corpus;

static bool is(TSNode node, const char *type)
{
    return !strcmp(ts_node_type(node), type);
}
/* A name is an identifier or a quoted string; compare them without quotes. */
static bool same_name(const char *a_source, TSNode a, const char *b_source, TSNode b)
{
    uint32_t start_a = ts_node_start_byte(a), length_a = ts_node_end_byte(a) - start_a;
    uint32_t start_b = ts_node_start_byte(b), length_b = ts_node_end_byte(b) - start_b;
    if (is(a, "string")) {
        start_a++, length_a -= 2;
    }
    if (is(b, "string")) {
        start_b++, length_b -= 2;
    }
    return length_a == length_b &&
           !memcmp(a_source + start_a, b_source + start_b, length_a);
}
static char *name_text(const char *source, TSNode node)
{
    uint32_t start = ts_node_start_byte(node), length = ts_node_end_byte(node) - start;
    if (is(node, "string")) {
        start++, length -= 2;
    }
    char *text = xmalloc(length + 1);
    memcpy(text, source + start, length);
    text[length] = '\0';
    return text;
}
static int name_char(const char **p)
{
    *p += strspn(*p, "_ -");
    return tolower((unsigned char)(**p ? *(*p)++ : 0));
}
/* Names resolve through the catalog, which ignores case and the separators
 * `_`, ` ` and `-` (script_name_matches). */
static bool names_match(const char *a, const char *b)
{
    int c, d;
    do {
        c = name_char(&a), d = name_char(&b);
    } while (c == d && c);
    return c == d;
}
static bool similar_name(const char *a_source, TSNode a, const char *b_source, TSNode b)
{
    char *x = name_text(a_source, a), *y = name_text(b_source, b);
    bool matches = names_match(x, y);
    free(x);
    free(y);
    return matches;
}
static bool is_name(TSNode node)
{
    return is(node, "identifier") || is(node, "string");
}
static void collect(TSNode node, int file, TSNode library, function **functions)
{
    if (is(node, "library")) {
        library = ts_node_child_by_field_name(node, "name", 4);
    }
    /* An action has no body and shadows functions. */
    if (is(node, "function_declaration") || is(node, "action_declaration") ||
        is(node, "macro_declaration")) {
        apush(
            *functions,
            ((function){ .file = file,
                         .library = library,
                         .name = ts_node_child_by_field_name(node, "name", 4),
                         .body = ts_node_child_by_field_name(node, "body", 4),
                         .scope = ts_node_parent(node) })
        );
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        collect(ts_node_named_child(node, i), file, library, functions);
    }
}
static bool contains(TSNode outer, TSNode node)
{
    return ts_node_start_byte(outer) <= ts_node_start_byte(node) &&
           ts_node_end_byte(node) <= ts_node_end_byte(outer);
}
static uint32_t span(TSNode node)
{
    return ts_node_end_byte(node) - ts_node_start_byte(node);
}
/* The function a call names: the innermost local function of that name whose
 * scope holds the call, or a library function as Library.Function in any
 * file. */
static const function *callee(const corpus *c, int file, TSNode target)
{
    const char *source = c->sources[file];
    TSNode library = { 0 }, name = target;
    if (is(target, "member_expression")) {
        library = ts_node_child_by_field_name(target, "object", 6);
        name = ts_node_child_by_field_name(target, "member", 6);
        if (!is_name(library)) {
            return NULL;
        }
    }
    bool local = ts_node_is_null(library);
    const function *found = NULL;
    for (int i = 0; is_name(name) && i < acount(c->functions); i++) {
        const function *f = &c->functions[i];
        const char *defined = c->sources[f->file];
        if (!local) {
            if (!ts_node_is_null(f->body) && !ts_node_is_null(f->library) &&
                similar_name(source, library, defined, f->library) &&
                similar_name(source, name, defined, f->name)) {
                return f;
            }
            continue;
        }
        if (f->file == file && contains(f->scope, target) &&
            same_name(source, name, defined, f->name) &&
            (!found || span(f->scope) < span(found->scope))) {
            found = f;
        }
    }
    return found;
}
static char *read_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    char *text = NULL;
    size_t size = 0;
    if (!fseek(file, 0, SEEK_END)) {
        long end = ftell(file);
        if (end >= 0 && !fseek(file, 0, SEEK_SET)) {
            size = (size_t)end;
            text = xmalloc(size + 1);
            if (fread(text, 1, size, file) != size) {
                free(text);
                text = NULL;
            }
        }
    }
    fclose(file);
    if (text) {
        text[size] = '\0';
    }
    return text;
}
#define STAGED_SUFFIX ".migrating"
static char *staged_path(const char *path)
{
    size_t size = strlen(path) + sizeof STAGED_SUFFIX;
    char *staged = xmalloc(size);
    snprintf(staged, size, "%s" STAGED_SUFFIX, path);
    return staged;
}
/* Writes the migrated source beside the original, which stays untouched. */
static int stage_text(const char *path, const char *text, char *error, size_t error_size)
{
    char *staged = staged_path(path);
    /* Never write through a symbolic or hard link left at the staging path. */
    unlink(staged);
    int fd = open(staged, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    /* The replacement keeps the source's permissions; a new file gets the
     * usual ones. */
    struct stat original = { .st_mode = 0644 };
    if (fd >= 0 && ((stat(path, &original) && errno != ENOENT) ||
                    fchmod(fd, original.st_mode & 07777))) {
        close(fd);
        fd = -1;
    }
    FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !file) {
        close(fd);
    }
    size_t size = strlen(text);
    bool written = file && fwrite(text, 1, size, file) == size;
    written = file && !fclose(file) && written;
    if (!written) {
        snprintf(error, error_size, "Cannot write migrated source %s", staged);
        remove(staged);
    }
    free(staged);
    return written ? 0 : -1;
}
static bool has_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name), suffix_length = strlen(suffix);
    return length > suffix_length && !strcmp(name + length - suffix_length, suffix);
}
static int collect_sources(const char *directory, const char *suffix, char ***paths, char *error, size_t error_size)
{
    DIR *dir = opendir(directory);
    if (!dir) {
        snprintf(error, error_size, "Cannot open %s: %s", directory, strerror(errno));
        return -1;
    }
    int status = 0;
    for (struct dirent *entry; !status && (entry = readdir(dir));) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        size_t size = strlen(directory) + strlen(entry->d_name) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s/%s", directory, entry->d_name);
        /* Like the source catalog, symlinks are not project sources. */
        struct stat info;
        if (lstat(path, &info)) {
            snprintf(error, error_size, "Cannot inspect %s", path);
            status = -1;
        } else if (S_ISDIR(info.st_mode)) {
            status = collect_sources(path, suffix, paths, error, error_size);
        } else if (S_ISREG(info.st_mode) && has_suffix(entry->d_name, suffix)) {
            apush(*paths, path);
            continue;
        }
        free(path);
    }
    closedir(dir);
    return status;
}
static bool collected(char **paths, const char *path)
{
    for (int i = 0; i < acount(paths); i++) {
        if (!strcmp(paths[i], path)) {
            return true;
        }
    }
    return false;
}
/* Whether two nodes are spelled alike. */
static bool spelled_alike(const char *text, TSNode a, TSNode b)
{
    if (ts_node_is_null(a) || ts_node_is_null(b)) {
        return false;
    }
    size_t length = ts_node_end_byte(a) - ts_node_start_byte(a);
    return length == ts_node_end_byte(b) - ts_node_start_byte(b) &&
           !strncmp(text + ts_node_start_byte(a), text + ts_node_start_byte(b), length);
}
/* Whether node itself binds a name spelled like name: a local or constant, a
 * handler parameter or alias, or a loop name or key. */
static bool binds_here(const char *text, TSNode node, TSNode name)
{
    if (is(node, "handler")) {
        return spelled_alike(text, ts_node_child_by_field_name(node, "alias", 5), name);
    }
    if (is(node, "for_statement")) {
        return spelled_alike(text, ts_node_child_by_field_name(node, "name", 4), name) ||
               spelled_alike(text, ts_node_child_by_field_name(node, "key", 3), name);
    }
    bool named = is(node, "local_declaration") ||
                 is(node, "constant_declaration") || is(node, "parameter");
    return named &&
           spelled_alike(text, ts_node_child_by_field_name(node, "name", 4), name);
}
static TSNode ref_call_field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? (TSNode){ 0 }
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static bool ref_qualifier_binding(const char *source, TSNode scope, TSNode name)
{
    if (ts_node_is_null(scope)) {
        return false;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(scope); i++) {
        TSNode node = ts_node_named_child(scope, i);
        if ((is(node, "parameter") || is(node, "local_declaration") ||
             is(node, "constant_declaration") || is(node, "state_declaration")) &&
            same_name(source, ref_call_field(node, "name"), source, name)) {
            return true;
        }
    }
    return false;
}
static bool ref_qualifier_shadowed(const char *source, TSNode target, TSNode name)
{
    for (TSNode scope = ts_node_parent(target); !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        if (binds_here(source, scope, name) ||
            ref_qualifier_binding(source, scope, name) ||
            ref_qualifier_binding(source, ref_call_field(scope, "parameters"), name)) {
            return true;
        }
    }
    return false;
}
/* Only scene/prototype component containers own these names. A record field
 * with the same spelling remains user data. */
static bool drop_retired_components(av_value *owner)
{
    static const char *const keys[] = { "components",
                                        "detached_components",
                                        "retained_components",
                                        "component_overrides" };
    bool changed = false;
    for (size_t k = 0; k < sizeof keys / sizeof *keys; k++) {
        av_value *components = av_obj_get(owner, keys[k]);
        if (!av_is_obj(components)) {
            continue;
        }
        bool removed = av_obj_remove_key(components, "Rhythm");
        removed |= av_obj_remove_key(components, "VisualMotion");
        if (removed && k && !av_obj_size(components)) {
            av_obj_remove_key(owner, keys[k]);
        }
        changed |= removed;
    }
    static const char *const groups[] = { "entities", "children" };
    for (size_t k = 0; k < sizeof groups / sizeof *groups; k++) {
        av_value *entities = av_obj_get(owner, groups[k]);
        for (size_t i = 0; i < av_arr_size(entities); i++) {
            changed |= drop_retired_components(av_arr_get(entities, i));
        }
    }
    return changed;
}
static bool freeze_beat_nodes(av_doc *document, av_value *graph, char *error, size_t size)
{
    bool changed = false;
    av_value *nodes = av_obj_get(graph, "nodes");
    for (size_t i = 0; i < av_arr_size(nodes); i++) {
        av_value *node = av_arr_get(nodes, i), *type = av_obj_get(node, "type");
        if (!av_is_str(type) || (strcmp(av_get_str(type), "Shader Beat Phase") &&
                                 strcmp(av_get_str(type), "Shader Beat Pulse"))) {
            continue;
        }
        av_set_str(type, "Shader Constant");
        av_value *outputs = av_obj_get(node, "outputs");
        if (!outputs) {
            outputs = av_obj_add_obj(document, node, "outputs");
        }
        if (!av_is_obj(outputs)) {
            snprintf(error, size, "Beat node outputs must be an object; repair the material before migrating");
            return false;
        }
        av_value *value = av_obj_get(outputs, "Value");
        if (!value) {
            value = av_obj_add_obj(document, outputs, "Value");
        }
        if (!av_is_obj(value)) {
            snprintf(error, size, "Beat node Value output must be an object; repair the material before migrating");
            return false;
        }
        av_obj_set(document, value, "type", av_str(document, "number"));
        av_obj_set(document, value, "default", av_real(document, 0));
        changed = true;
    }
    return changed;
}
static char *retire_asset_features(const char *text, char *error, size_t size)
{
    av_doc *document = asset_text_read(text, error, size);
    if (!document) {
        return NULL;
    }
    av_value *root = av_doc_root(document);
    bool changed = drop_retired_components(root);
    changed |= freeze_beat_nodes(document, root, error, size);
    av_value *graphs = av_obj_get(root, "graphs");
    for (size_t i = 0; !error[0] && i < av_arr_size(graphs); i++) {
        changed |= freeze_beat_nodes(document, av_arr_get(graphs, i), error, size);
    }
    char *migrated = changed && !error[0] ? asset_text_write(root) : NULL;
    av_doc_free(document);
    return migrated;
}
static void retirement_types(TSNode node, int file, function **types)
{
    if (is(node, "component") || is(node, "record_declaration") ||
        is(node, "union_declaration")) {
        apush(
            *types,
            ((function){ .file = file,
                         .name = ref_call_field(node, "name"),
                         .scope = ts_node_parent(node),
                         .body = node })
        );
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        retirement_types(ts_node_named_child(node, i), file, types);
    }
}
static bool retirement_same_package(const corpus *context, int file, TSNode use, const function *type)
{
    if (!is(type->scope, "source_file")) {
        return false;
    }
    TSNode root = use;
    for (TSNode parent = ts_node_parent(root); !ts_node_is_null(parent);
         parent = ts_node_parent(root)) {
        root = parent;
    }
    TSNode current = ref_call_field(ref_call_field(root, "package"), "name"),
           declared = ref_call_field(ref_call_field(type->scope, "package"), "name");
    return !ts_node_is_null(current) && !ts_node_is_null(declared) &&
           same_name(context->sources[file], current, context->sources[type->file], declared);
}
static const char *retired_type_use(const corpus *context, function *types, int file, TSNode name)
{
    if (ts_node_is_null(name) || !is_name(name)) {
        return NULL;
    }
    const char *text = context->sources[file];
    char *spelling = name_text(text, name);
    const char *retired = names_match(spelling, "Rhythm")       ? "Rhythm"
                        : names_match(spelling, "VisualMotion") ? "VisualMotion"
                                                                : NULL;
    free(spelling);
    for (int i = 0; retired && i < acount(types); i++) {
        const function *type = &types[i];
        if (same_name(text, name, context->sources[type->file], type->name) &&
            (is(type->body, "component") ||
             (type->file == file && contains(type->scope, name)) ||
             retirement_same_package(context, file, name, type))) {
            return NULL;
        }
    }
    return retired;
}
static bool native_component_call(const char *name)
{
    static const char *const names[] = {
        "get_component", "set_component",    "has_component",
        "add_component", "remove_component", "find_entities_with_component"
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        if (names_match(name, names[i])) {
            return true;
        }
    }
    return false;
}
static const char *retired_child_use(
    const corpus *context,
    function *types,
    int file,
    TSNode children,
    TSNode *use
)
{
    if (ts_node_is_null(children) || !is(children, "array_expression")) {
        return NULL;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(children); i++) {
        TSNode child = ts_node_named_child(children, i);
        if (!is(child, "struct_expression")) {
            continue;
        }
        for (uint32_t j = 0; j < ts_node_named_child_count(child); j++) {
            TSNode property = ts_node_named_child(child, j);
            if (!is(property, "struct_field")) {
                continue;
            }
            TSNode name = ref_call_field(property, "name"),
                   value = ref_call_field(property, "value");
            if (!is_name(name)) {
                continue;
            }
            char *key = name_text(context->sources[file], name);
            bool components = !strcmp(key, "components") ||
                              !strcmp(key, "retained_components");
            const char *removed = !strcmp(key, "children")
                                    ? retired_child_use(context, types, file, value, use)
                                    : NULL;
            free(key);
            if (removed) {
                return removed;
            }
            for (uint32_t k = 0; components && is(value, "struct_expression") &&
                                 k < ts_node_named_child_count(value);
                 k++) {
                TSNode component = ref_call_field(ts_node_named_child(value, k), "name");
                removed = retired_type_use(context, types, file, component);
                if (removed) {
                    *use = component;
                    return removed;
                }
            }
        }
    }
    return NULL;
}
static const char *retired_guard_use(const corpus *context, function *types, int file, TSNode node, TSNode *use)
{
    if (ts_node_is_null(node) || is(node, "member_expression") || is(node, "string")) {
        return NULL;
    }
    if (is(node, "named_argument") || is(node, "typed_argument") ||
        is(node, "struct_field")) {
        node = ref_call_field(node, "value");
        if (ts_node_is_null(node)) {
            return NULL;
        }
    }
    if (is(node, "identifier")) {
        const char *text = context->sources[file];
        const char *removed = retired_type_use(context, types, file, node);
        if (removed && !ref_qualifier_shadowed(text, node, node)) {
            bool bound = false;
            for (TSNode scope = ts_node_parent(node);
                 !bound && !ts_node_is_null(scope);
                 scope = ts_node_parent(scope)) {
                if (!is(scope, "query_list")) {
                    continue;
                }
                for (uint32_t i = 0; i < ts_node_named_child_count(scope); i++) {
                    TSNode entry = ts_node_named_child(scope, i);
                    if (ts_node_start_byte(entry) >= ts_node_start_byte(node)) {
                        break;
                    }
                    if ((is(entry, "query_binding") || is(entry, "query_generator")) &&
                        same_name(text, ref_call_field(entry, "name"), text, node)) {
                        bound = true;
                    }
                }
            }
            if (!bound) {
                *use = node;
                return removed;
            }
        }
    }
    if (is(node, "call_expression")) {
        node = ref_call_field(node, "arguments");
    }
    for (uint32_t i = 0; !ts_node_is_null(node) && i < ts_node_named_child_count(node);
         i++) {
        const char *removed = retired_guard_use(context, types, file, ts_node_named_child(node, i), use);
        if (removed) {
            return removed;
        }
    }
    return NULL;
}
static const char *retired_source_use(const corpus *context, function *types, int file, TSNode node, TSNode *use)
{
    const char *text = context->sources[file];
    if (is(node, "query_guard")) {
        const char *removed = retired_guard_use(context, types, file, ref_call_field(node, "condition"), use);
        if (removed) {
            return removed;
        }
    }
    if (is(node, "struct_field") && is(ts_node_parent(node), "prototype_body")) {
        char *name = name_text(text, ref_call_field(node, "name"));
        const char *removed = !strcmp(name, "children")
                                ? retired_child_use(context, types, file, ref_call_field(node, "value"), use)
                                : NULL;
        free(name);
        if (removed) {
            return removed;
        }
    }
    TSNode component = { 0 };
    if (is(node, "prototype_component")) {
        component = ref_call_field(node, "name");
    }
    if (is(node, "type_name") && ts_node_named_child_count(node) == 1) {
        component = ts_node_named_child(node, 0);
    }
    const char *retired = retired_type_use(context, types, file, component);
    if (retired) {
        *use = component;
        return retired;
    }
    if (is(node, "call_expression")) {
        TSNode target = ref_call_field(node, "function");
        while (is(target, "parenthesized_expression")) {
            target = ts_node_named_child(target, 0);
        }
        if (is_name(target) && !callee(context, file, target) &&
            !ref_qualifier_shadowed(text, target, target)) {
            char *name = name_text(text, target);
            const char *removed = names_match(name, "scene_rhythm")
                                    ? "scene_rhythm"
                                : names_match(name, "trigger_visual_reaction")
                                    ? "trigger_visual_reaction"
                                    : NULL;
            bool component_call = native_component_call(name);
            free(name);
            if (removed) {
                *use = target;
                return removed;
            }
            TSNode args = ref_call_field(node, "arguments");
            for (uint32_t i = 0; component_call && i < ts_node_named_child_count(args);
                 i++) {
                TSNode arg = ts_node_named_child(args, i),
                       label = ref_call_field(arg, "name"),
                       value = ref_call_field(arg, "value");
                bool selected = ts_node_is_null(label) && i == 0;
                if (!ts_node_is_null(label)) {
                    char *label_text = name_text(text, label);
                    selected = names_match(label_text, "Component");
                    free(label_text);
                } else {
                    value = arg;
                }
                removed = selected ? retired_type_use(context, types, file, value)
                                   : NULL;
                if (removed && !ref_qualifier_shadowed(text, value, value)) {
                    *use = value;
                    return removed;
                }
            }
        }
    }
    if (is(node, "member_expression")) {
        TSNode object = ref_call_field(node, "object"),
               member = ref_call_field(node, "member");
        if (is_name(object) && is_name(member) &&
            !ref_qualifier_shadowed(text, node, object)) {
            char *owner = name_text(text, object);
            const char *removed = !strcmp(owner, "self")
                                    ? retired_type_use(context, types, file, member)
                                    : NULL;
            free(owner);
            if (removed) {
                *use = member;
                return removed;
            }
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        const char *removed = retired_source_use(context, types, file, ts_node_named_child(node, i), use);
        if (removed) {
            return removed;
        }
    }
    return NULL;
}
/* Refuse source that needs an author decision before staging any asset. Local
 * declarations and qualified authored callables retain their own meaning. */
static char **migrate_retired_features(char **sources, char **paths, int count, char *error, size_t size)
{
    corpus context = { .sources = (const char *const *)sources };
    function *types = NULL;
    script_syntax **syntax = xcalloc((size_t)count, sizeof *syntax);
    char **migrated = xcalloc((size_t)(count + 1), sizeof *migrated);
    for (int i = 0; !error[0] && i < count; i++) {
        if (!has_suffix(paths[i], ".chibi")) {
            continue;
        }
        syntax[i] = script_parse(sources[i]);
        if (syntax[i]->diagnostic_count) {
            snprintf(error, size, "%s: fix invalid source before retiring rhythm and visual motion", paths[i]);
            break;
        }
        collect(ts_tree_root_node(syntax[i]->tree), i, (TSNode){ 0 }, &context.functions);
        retirement_types(ts_tree_root_node(syntax[i]->tree), i, &types);
    }
    for (int i = 0; !error[0] && i < count; i++) {
        if (syntax[i]) {
            TSNode use = { 0 };
            const char *removed = retired_source_use(
                &context,
                types,
                i,
                ts_tree_root_node(syntax[i]->tree),
                &use
            );
            if (removed) {
                script_range range = script_node_range(use);
                snprintf(
                    error,
                    size,
                    "%s:%d:%d: %s was removed in project version 39; remove or "
                    "rewrite this use before migrating",
                    paths[i],
                    range.line,
                    range.column,
                    removed
                );
            }
            continue;
        }
        migrated[i] = retire_asset_features(sources[i], error, size);
        if (error[0]) {
            char *message = xstrdup(error);
            snprintf(error, size, "%s: %s", paths[i], message);
            free(message);
        }
    }
    for (int i = 0; i < count; i++) {
        script_syntax_free(syntax[i]);
    }
    free(syntax);
    afree(context.functions);
    afree(types);
    return migrated;
}
int chibi_project_migrate_content(
    const char *content,
    const char *start_scene,
    int from,
    char *error,
    size_t error_size
)
{
    /* Version 39 retires rhythm and visual motion from scenes, prototypes and
     * materials wherever the project keeps them, and refuses scripts that
     * still use them. */
    char **paths = NULL;
    int status = collect_sources(content, DECLARATIVE_ASSET_EXTENSION, &paths, error, error_size);
    if (!status) {
        status = collect_sources(content, ".chibi", &paths, error, error_size);
    }
    /* The scan skips hidden entries, and the start scene may lie outside the
     * content, so add it unless the scan found it. */
    struct stat info;
    if (!status && start_scene && !collected(paths, start_scene) &&
        !stat(start_scene, &info) && S_ISREG(info.st_mode)) {
        apush(paths, xstrdup(start_scene));
    }
    int count = acount(paths);
    char **sources = xmalloc((size_t)(count + 1) * sizeof *sources);
    for (int i = 0; i < count; i++) {
        sources[i] = status ? NULL : read_text(paths[i]);
        if (!status && !sources[i]) {
            snprintf(error, error_size, "Cannot read source %s", paths[i]);
            status = -1;
        }
    }
    char **migrated = NULL;
    if (!status) {
        switch (from) {
            case 38:
                error[0] = '\0';
                migrated = migrate_retired_features(sources, paths, count, error, error_size);
                status = error[0] ? -1 : 0;
                break;
            default: assert(false);
        }
    }
    /* Every source is staged before any original is replaced, and a failed
     * step leaves the originals. Each step is idempotent, so a step
     * interrupted while replacing reruns cleanly. The manifest records each
     * finished step, so a later failure reruns none. */
    int staged = 0;
    for (; !status && staged < count; staged++) {
        if (migrated[staged]) {
            status = stage_text(paths[staged], migrated[staged], error, error_size);
        }
    }
    for (int i = 0; i < staged; i++) {
        char *from = migrated[i] ? staged_path(paths[i]) : NULL;
        if (from && status) {
            remove(from);
        } else if (from && rename(from, paths[i])) {
            snprintf(error, error_size, "Cannot replace source %s", paths[i]);
            status = -1;
        }
        free(from);
    }
    for (int i = 0; i < count; i++) {
        free(migrated ? migrated[i] : NULL);
        free(sources[i]);
        free(paths[i]);
    }
    free(migrated);
    free(sources);
    afree(paths);
    return status;
}
