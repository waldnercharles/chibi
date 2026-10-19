#include "authoring/source_asset.h"

#include "capabilities/catalog.h"
#include "language/syntax.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <yyjson.h>

typedef struct
{
    script_syntax *syntax;
    av_doc *doc;
    char *error;
    size_t size;
    bool failed;
} reader;
static TSNode field(TSNode node, const char *name)
{
    return ts_node_is_null(node)
             ? (TSNode){ 0 }
             : ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static bool is(TSNode node, const char *kind)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), kind);
}
static bool fail(reader *r, TSNode node, const char *message)
{
    if (!r->failed && r->size) {
        script_range at = script_node_range(node);
        snprintf(r->error, r->size, "%d:%d: %s", at.line, at.column, message);
    }
    r->failed = true;
    return false;
}
static char *text(reader *r, TSNode node)
{
    size_t start = ts_node_start_byte(node), size = ts_node_end_byte(node) - start;
    char *copy = xmalloc(size + 1);
    memcpy(copy, r->syntax->text + start, size);
    copy[size] = 0;
    return copy;
}
static av_value *located(TSNode node, av_value *value)
{
    av_set_source(
        value,
        (av_source){ .start = ts_node_start_byte(node),
                     .end = ts_node_end_byte(node),
                     .name_start = ts_node_start_byte(node),
                     .name_end = ts_node_end_byte(node) }
    );
    return value;
}
static av_value *value(reader *, TSNode, int);
static av_value *object(reader *r, TSNode node, int depth)
{
    av_value *out = av_obj(r->doc);
    for (uint32_t i = 0; !r->failed && i < ts_node_named_child_count(node); i++) {
        TSNode member = ts_node_named_child(node, i);
        if (is(member, "comment")) {
            continue;
        }
        TSNode name = field(member, "name"), item = field(member, "value");
        if (!is(name, "identifier") || ts_node_is_null(item)) {
            fail(r, member, "An authored default field needs an identifier and a value");
            break;
        }
        char *key = text(r, name);
        if (av_obj_get(out, key)) {
            fail(r, name, "Duplicate authored default field");
        } else {
            av_value *v = value(r, item, depth + 1);
            if (v) {
                av_obj_add(out, located(name, av_str(r->doc, key)), v);
            }
        }
        free(key);
    }
    return located(node, out);
}
static av_value *value(reader *r, TSNode node, int depth)
{
    if (depth > 64) {
        fail(r, node, "Authored default nesting exceeds 64");
        return NULL;
    }
    if (is(node, "struct_expression")) {
        return object(r, node, depth);
    }
    if (is(node, "array_expression")) {
        av_value *array = av_arr(r->doc);
        for (uint32_t i = 0; !r->failed && i < ts_node_named_child_count(node); i++) {
            TSNode item = ts_node_named_child(node, i);
            if (is(item, "comment")) {
                continue;
            }
            av_value *v = value(r, item, depth + 1);
            if (v) {
                av_arr_append(array, v);
            }
        }
        return located(node, array);
    }
    if (is(node, "parenthesized_expression")) {
        return value(r, field(node, "expression"), depth + 1);
    }
    if (is(node, "member_expression")) {
        TSNode object_node = field(node, "object"), member = field(node, "member");
        if ((!is(object_node, "identifier") && !is(object_node, "member_expression")) ||
            !is(member, "identifier")) {
            fail(r, node, "An authored symbol path uses identifier members");
            return NULL;
        }
        av_value *object = value(r, object_node, depth + 1);
        if (!object) {
            return NULL;
        }
        char *name = text(r, member);
        size_t size = strlen(av_get_str(object)) + strlen(name) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s.%s", av_get_str(object), name);
        av_value *out = av_str(r->doc, path);
        free(path);
        free(name);
        return located(node, out);
    }
    if (is(node, "unary_expression")) {
        char *op = text(r, field(node, "operator"));
        av_value *operand = value(r, field(node, "operand"), depth + 1), *out = NULL;
        if (operand && av_is_num(operand)) {
            if (!strcmp(op, "+")) {
                out = operand;
            } else if (!strcmp(op, "-")) {
                if (av_is_uint(operand) &&
                    av_get_uint(operand) <= UINT64_C(0x8000000000000000)) {
                    uint64_t n = av_get_uint(operand);
                    out = av_sint(r->doc, n == UINT64_C(0x8000000000000000) ? INT64_MIN : -(int64_t)n);
                } else if (av_is_sint(operand) && av_get_sint(operand) != INT64_MIN) {
                    out = av_sint(r->doc, -av_get_sint(operand));
                } else if (av_get_type(operand) == AV_REAL) {
                    out = av_real(r->doc, -av_get_num(operand));
                }
            }
        }
        free(op);
        if (!out) {
            fail(r, node, "Invalid or overflowing static numeric sign");
            return NULL;
        }
        return located(node, out);
    }
    char *token = text(r, node);
    av_value *out = NULL;
    if (is(node, "identifier")) {
        out = av_str(r->doc, token);
    } else if (is(node, "boolean")) {
        out = av_bool(r->doc, !strcmp(token, "true"));
    } else if (is(node, "nil")) {
        out = av_null(r->doc);
    } else if (is(node, "string")) {
        yyjson_doc *doc = yyjson_read(token, strlen(token), 0);
        yyjson_val *v = doc ? yyjson_doc_get_root(doc) : NULL;
        if (yyjson_is_str(v) && yyjson_get_len(v) == strlen(yyjson_get_str(v))) {
            out = av_strn(r->doc, yyjson_get_str(v), yyjson_get_len(v));
        }
        yyjson_doc_free(doc);
    } else if (is(node, "number")) {
        char *end;
        errno = 0;
        bool negative = token[0] == '-';
        const char *digits = token + (negative || token[0] == '+');
        if (!strncmp(digits, "0x", 2) ||
            (!strchr(digits, '.') && !strchr(digits, 'e') && !strchr(digits, 'E'))) {
            unsigned long long n = strtoull(digits, &end, !strncmp(digits, "0x", 2) ? 16 : 10);
            if (!errno && *digits && !*end &&
                (!negative || n <= UINT64_C(0x8000000000000000))) {
                out = negative
                        ? av_sint(r->doc, n == UINT64_C(0x8000000000000000) ? INT64_MIN : -(int64_t)n)
                        : av_uint(r->doc, n);
            }
        } else {
            double n = strtod(token, &end);
            if (!errno && *token && !*end && isfinite(n)) {
                out = av_real(r->doc, n);
            }
        }
    }
    free(token);
    if (!out) {
        fail(r, node, "Prototype defaults must be static literals, symbol names, arrays or objects");
        return NULL;
    }
    return located(node, out);
}
static bool same_spelling(const char *a, const char *b)
{
    for (;;) {
        while (*a == '_') {
            a++;
        }
        while (*b == '_') {
            b++;
        }
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        if (!*a) {
            return true;
        }
        a++;
        b++;
    }
}
static bool normalize(reader *r, TSNode at, av_value *values, const type_desc *type)
{
    for (size_t i = 0; i < av_obj_size(values); i++) {
        av_value *key = av_obj_key_at(values, i), *v = av_obj_value_at(values, i);
        const type_field *found = NULL;
        for (int j = 0; j < type->field_count; j++) {
            if (same_spelling(av_get_str(key), type->fields[j].name)) {
                if (found) {
                    return fail(r, at, "Ambiguous reflected field spelling");
                }
                found = &type->fields[j];
            }
        }
        if (!found) {
            return fail(r, at, "Unknown reflected field in prototype defaults");
        }
        for (size_t j = 0; j < i; j++) {
            if (!strcmp(av_get_str(av_obj_key_at(values, j)), found->name)) {
                return fail(r, at, "Prototype defaults repeat the same reflected field");
            }
        }
        av_set_str(key, found->name);
        if (found->type == PIN_ENUM && av_is_str(v)) {
            const char *label = av_get_str(v), *dot = strrchr(label, '.');
            if (dot) {
                if (!found->type_name ||
                    (size_t)(dot - label) != strlen(found->type_name) ||
                    strncmp(label, found->type_name, (size_t)(dot - label))) {
                    return fail(r, at, "Prototype default names the wrong enum type");
                }
                av_set_str(v, dot + 1);
            }
        }
        if (found->type == PIN_STRUCT && av_is_obj(v)) {
            const type_desc *nested = type_catalog_find(found->type_name);
            if (!nested || !normalize(r, at, v, nested)) {
                return false;
            }
        }
    }
    return true;
}
static TSNode root(reader *r, const char *kind)
{
    TSNode tree = ts_tree_root_node(r->syntax->tree), declaration = { 0 };
    if (r->syntax->diagnostic_count) {
        fail(r, tree, "Invalid authored asset declaration");
        return declaration;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(tree); i++) {
        TSNode child = ts_node_named_child(tree, i);
        if (!is(child, "comment")) {
            if (!ts_node_is_null(declaration)) {
                fail(r, child, "An authored asset file must contain exactly one declaration");
                break;
            }
            declaration = child;
        }
    }
    if (!is(declaration, kind)) {
        fail(r, tree, "Invalid authored asset declaration");
    }
    return declaration;
}
static bool symbol_value(reader *r, const av_value *v, bool path, const char *message)
{
    av_source source = av_get_source(v);
    TSNode node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(r->syntax->tree),
        (uint32_t)source.start,
        (uint32_t)source.end
    );
    return is(node, "identifier") || (path && is(node, "member_expression")) ||
           fail(r, node, message);
}
static bool behavior_values(reader *r, const av_value *values)
{
    if (!av_is_arr(values)) {
        return fail(r, ts_tree_root_node(r->syntax->tree), "Prototype behaviors must be an array of behavior symbols");
    }
    for (size_t i = 0; i < av_arr_size(values); i++) {
        if (!symbol_value(r, av_arr_get(values, i), true, "A behavior reference must use a symbol, not a string")) {
            return false;
        }
    }
    return true;
}
static bool prototype_references(reader *r, const char *key, const av_value *v)
{
    if (!strcmp(key, "behaviors")) {
        return behavior_values(r, v);
    }
    if (!strcmp(key, "children") && av_is_arr(v)) {
        for (size_t i = 0; i < av_arr_size(v); i++) {
            const av_value *child = av_arr_get(v, i),
                           *prototype = av_obj_get(child, "prototype"),
                           *behaviors = av_obj_get(child, "behaviors");
            if ((
                    prototype && !symbol_value(r, prototype, true, "A child prototype reference must use a symbol, not a string")
                ) ||
                (behaviors && !behavior_values(r, behaviors))) {
                return false;
            }
        }
    }
    if (!strcmp(key, "links") && av_is_obj(v)) {
        for (size_t i = 0; i < av_obj_size(v); i++) {
            const av_value *targets = av_obj_value_at(v, i);
            size_t count = av_is_arr(targets) ? av_arr_size(targets) : 1;
            for (size_t j = 0; j < count; j++) {
                const av_value *target = av_is_arr(targets) ? av_arr_get(targets, j)
                                                            : targets;
                if (!symbol_value(r, target, true, "A relationship target must use a child member path, not a string")) {
                    return false;
                }
            }
        }
    }
    return true;
}
av_doc *authoring_prototype_source_read(
    const char *source,
    const prototype_registry *registry,
    char *error,
    size_t size
)
{
    reader r = { .syntax = script_parse(source),
                 .doc = av_doc_new(),
                 .error = error,
                 .size = size };
    TSNode declaration = root(&r, "prototype_declaration");
    av_value *out = av_obj(r.doc);
    av_doc_set_root(r.doc, out);
    TSNode name = field(declaration, "name");
    if (!r.failed) {
        av_obj_add_val(r.doc, out, "name", value(&r, name, 0));
    }
    TSNode bases = field(declaration, "bases");
    av_value *includes = av_obj_add_arr(r.doc, out, "includes");
    for (uint32_t i = 0; !r.failed && !ts_node_is_null(bases) &&
                         i < ts_node_named_child_count(bases);
         i++) {
        TSNode base = ts_node_named_child(bases, i);
        if (is(base, "comment")) {
            continue;
        }
        if (!is(base, "identifier")) {
            fail(&r, base, "A prototype base must name a prototype symbol");
            break;
        }
        av_arr_append(includes, value(&r, base, 0));
    }
    av_value *components = av_obj_add_obj(r.doc, out, "components");
    TSNode body = field(declaration, "body");
    for (uint32_t i = 0; !r.failed && i < ts_node_named_child_count(body); i++) {
        TSNode child = ts_node_named_child(body, i);
        if (is(child, "comment")) {
            continue;
        }
        TSNode key_node = field(child, "name");
        char *key = text(&r, key_node);
        if (is(child, "prototype_component")) {
            const ecs_component_desc *desc = NULL;
            for (int j = 0; registry && j < registry->component_count; j++) {
                if (same_spelling(key, registry->components[j]->name)) {
                    if (desc) {
                        fail(&r, key_node, "Ambiguous prototype component spelling");
                        break;
                    }
                    desc = registry->components[j];
                }
            }
            if (registry && !desc) {
                fail(&r, key_node, "Unknown prototype component");
            }
            const char *canonical = desc ? desc->name : key;
            if (av_obj_get(components, canonical)) {
                fail(&r, key_node, "Duplicate prototype component");
            }
            av_value *defaults = object(&r, field(child, "body"), 0);
            if (desc && !normalize(&r, child, defaults, desc->type)) {
                r.failed = true;
            }
            av_obj_add(components, located(key_node, av_str(r.doc, canonical)), defaults);
        } else if (is(child, "struct_field")) {
            if (strcmp(key, "behaviors") && strcmp(key, "identity") &&
                strcmp(key, "children") && strcmp(key, "links")) {
                fail(&r, key_node, "Unknown prototype property");
            } else if (av_obj_get(out, key)) {
                fail(&r, key_node, "Duplicate prototype property");
            } else {
                av_value *v = value(&r, field(child, "value"), 0);
                if (v && prototype_references(&r, key, v)) {
                    av_obj_add(out, located(key_node, av_str(r.doc, key)), v);
                }
            }
        } else {
            fail(&r, child, "Unknown prototype declaration member");
        }
        free(key);
    }
    script_syntax_free(r.syntax);
    if (r.failed) {
        av_doc_free(r.doc);
        return NULL;
    }
    return r.doc;
}
ecs_relationship_desc *authoring_relationship_source_read(const char *source, char *error, size_t size)
{
    reader r = { .syntax = script_parse(source),
                 .doc = av_doc_new(),
                 .error = error,
                 .size = size };
    TSNode declaration = root(&r, "relationship_declaration");
    av_value *properties = !r.failed ? object(&r, field(declaration, "body"), 0) : NULL;
    ecs_relationship_desc *desc = xcalloc(1, sizeof *desc);
    desc->name = !r.failed ? text(&r, field(declaration, "name")) : xstrdup("");
    size_t length = strlen(desc->name) + sizeof "relationship.";
    char *key = xmalloc(length);
    snprintf(key, length, "relationship.%s", desc->name);
    desc->key = key;
    desc->id = ecs_component_marker_id(key);
    for (size_t i = 0; !r.failed && i < av_obj_size(properties); i++) {
        const char *property = av_get_str(av_obj_key_at(properties, i));
        av_value *v = av_obj_value_at(properties, i);
        unsigned trait = !strcmp(property, "exclusive")   ? ECS_REL_EXCLUSIVE
                       : !strcmp(property, "acyclic")     ? ECS_REL_ACYCLIC
                       : !strcmp(property, "traversable") ? ECS_REL_TRAVERSABLE
                                                          : 0;
        if (trait && av_is_bool(v)) {
            if (av_get_bool(v)) {
                desc->traits |= trait;
            }
        } else if (!strcmp(property, "on_delete_target") && av_is_str(v)) {
            if (!symbol_value(&r, v, false, "A relationship cleanup policy must use Remove, Delete or Panic, not a string")) {
                break;
            }
            const char *policy = av_get_str(v);
            if (!strcmp(policy, "Remove")) {
                desc->on_delete_target = ECS_CLEANUP_REMOVE;
            } else if (!strcmp(policy, "Delete")) {
                desc->on_delete_target = ECS_CLEANUP_DELETE;
            } else if (!strcmp(policy, "Panic")) {
                desc->on_delete_target = ECS_CLEANUP_PANIC;
            } else {
                fail(&r, declaration, "Unknown relationship target deletion policy");
            }
        } else {
            fail(&r, declaration, "Invalid relationship property or value type");
        }
    }
    if ((desc->traits & ECS_REL_TRAVERSABLE) && !(desc->traits & ECS_REL_EXCLUSIVE)) {
        fail(&r, declaration, "A source-query traversable relationship must be exclusive");
    }
    if (desc->traits & ECS_REL_TRAVERSABLE) {
        desc->traits |= ECS_REL_ACYCLIC;
    }
    script_syntax_free(r.syntax);
    av_doc_free(r.doc);
    if (r.failed) {
        authoring_relationship_source_free(desc);
        return NULL;
    }
    return desc;
}
void authoring_relationship_source_free(ecs_relationship_desc *desc)
{
    if (!desc) {
        return;
    }
    free((char *)desc->name);
    free((char *)desc->key);
    free(desc);
}
av_doc *authoring_prototype_document_read(
    const char *source,
    const prototype_registry *registry,
    char *error,
    size_t size
)
{
    script_syntax *syntax = script_parse(source);
    TSNode tree = ts_tree_root_node(syntax->tree);
    bool authored = false;
    for (uint32_t i = 0; i < ts_node_named_child_count(tree); i++) {
        authored |= is(ts_node_named_child(tree, i), "prototype_declaration");
    }
    script_syntax_free(syntax);
    return authored ? authoring_prototype_source_read(source, registry, error, size)
                    : asset_text_read(source, error, size);
}
static int relationship_order(const void *a, const void *b)
{
    const ecs_relationship_desc *const *left = a, *const *right = b;
    return strcmp((*left)->name, (*right)->name);
}

bool authoring_relationship_declarations_load(
    const char *content,
    ecs_relationship_desc ***out,
    char *error,
    size_t size
)
{
    if (size) {
        error[0] = 0;
    }
    char path[4096];
    if (snprintf(path, sizeof path, "%s/relationships", content) >= (int)sizeof path) {
        if (size) {
            snprintf(error, size, "Relationship directory path exceeds limit");
        }
        return false;
    }
    struct stat info;
    if (!lstat(path, &info) && !S_ISDIR(info.st_mode)) {
        if (size) {
            snprintf(error, size, "Relationship declaration directory must be a real directory");
        }
        return false;
    }
    DIR *directory = opendir(path);
    if (!directory) {
        if (errno == ENOENT) {
            return true;
        }
        if (size) {
            snprintf(error, size, "Cannot open relationship declarations");
        }
        return false;
    }
    bool valid = true;
    struct dirent *entry;
    size_t total = 0;
    while (valid && (entry = readdir(directory))) {
        size_t length = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || length <= 6 ||
            strcmp(entry->d_name + length - 6, ".chibi")) {
            continue;
        }
        char file[4096];
        if (snprintf(file, sizeof file, "%s/%s", path, entry->d_name) >= (int)sizeof file ||
            acount(*out) >= 1024) {
            valid = false;
            break;
        }
        if (lstat(file, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
            info.st_size > 512 * 1024 ||
            (size_t)info.st_size > 32 * 1024 * 1024 - total) {
            valid = false;
            break;
        }
        char *text = text_file_read(file);
        if (!text || strlen(text) > 512 * 1024 ||
            strlen(text) > 32 * 1024 * 1024 - total) {
            free(text);
            valid = false;
            break;
        }
        total += strlen(text);
        ecs_relationship_desc *desc = authoring_relationship_source_read(text, error, size);
        free(text);
        if (!desc) {
            valid = false;
            break;
        }
        for (int i = 0; valid && i < acount(*out); i++) {
            valid = desc->id != (*out)[i]->id && strcmp(desc->name, (*out)[i]->name);
        }
        if (!valid) {
            authoring_relationship_source_free(desc);
            break;
        }
        apush(*out, desc);
    }
    closedir(directory);
    if (!valid && size && !error[0]) {
        snprintf(error, size, "Invalid relationship file count, bytes or duplicate name");
    }
    if (valid && acount(*out) > 1) {
        qsort(*out, (size_t)acount(*out), sizeof **out, relationship_order);
    }
    return valid;
}
