#include "project_migrate29.h"

#include "project_migrate26.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_edit.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Version 30 handlers name the payload fields they read. The rewrite needs
 * each event's fields and their types: the engine's events as version 29
 * declared them, then the project's event declarations. */
typedef struct
{
    char *name, *type; /* type is NULL when the migration cannot spell it. */
} payload_field;
typedef struct
{
    char *name;
    payload_field *fields;
} payload_event;

static const struct
{
    const char *event, *field, *type;
} VERSION_29_EVENTS[] = {
    { "Begin", NULL, NULL },
    { "Tick", "dt", "float s" },
    { "Input", "Direction", "struct<CF_V2>" },
    { "Contact Begin", "Other", "handle<ecs.entity>" },
    { "Contact Begin", "Point", "struct<CF_V2>" },
    { "Contact Begin 3D", "Other", "handle<ecs.entity>" },
    { "Contact Begin 3D", "Point", "struct<CF_V3>" },
    { "Contact End", "Other", "handle<ecs.entity>" },
    { "Contact End 3D", "Other", "handle<ecs.entity>" },
};

/* The scalar type names version 29 read from an event field's `type`. */
static const char *const VERSION_29_SCALARS[] = {
    "bool",  "int8",   "byte",  "int16",  "uint16", "int",    "uint",
    "int64", "uint64", "float", "double", "name",   "string",
};

static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static char *text_of(const char *text, TSNode node)
{
    uint32_t start = ts_node_start_byte(node), length = ts_node_end_byte(node) - start;
    char *out = xmalloc(length + 1);
    memcpy(out, text + start, length);
    out[length] = '\0';
    return out;
}
static bool spelled(const char *text, TSNode node, const char *name)
{
    if (ts_node_is_null(node)) {
        return false;
    }
    size_t length = ts_node_end_byte(node) - ts_node_start_byte(node);
    return strlen(name) == length &&
           !strncmp(text + ts_node_start_byte(node), name, length);
}
/* Names match as the catalog matches them: without case, spaces,
 * underscores or hyphens. */
static bool same_name(const char *a, const char *b)
{
    for (;;) {
        while (*a == ' ' || *a == '_' || *a == '-') {
            a++;
        }
        while (*b == ' ' || *b == '_' || *b == '-') {
            b++;
        }
        if (!*a || !*b) {
            return !*a && !*b;
        }
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) {
            return false;
        }
    }
}
static payload_event *find_event(payload_event *events, const char *name)
{
    for (int i = 0; i < acount(events); i++) {
        if (same_name(events[i].name, name)) {
            return &events[i];
        }
    }
    return NULL;
}
static void add_field(payload_event *event, const char *name, const char *type)
{
    apush(event->fields, ((payload_field){ xstrdup(name), type ? xstrdup(type) : NULL }));
}
static bool scalar(const char *type)
{
    for (size_t i = 0; i < sizeof VERSION_29_SCALARS / sizeof *VERSION_29_SCALARS; i++) {
        if (!strcmp(type, VERSION_29_SCALARS[i])) {
            return true;
        }
    }
    return false;
}
/* The source spelling of an event variable's type, or NULL. */
static char *variable_type(const av_value *variable)
{
    const av_value *kind = av_obj_get(variable, "type"),
                   *named = av_obj_get(variable, "type_name");
    const av_value *container = av_obj_get(variable, "container");
    const char *type = kind && av_is_str(kind) ? av_get_str(kind) : NULL;
    const char *name = named && av_is_str(named) ? av_get_str(named) : NULL;
    char spelling[256];
    if (!type) {
        return NULL;
    }
    if (scalar(name ? name : type)) {
        snprintf(spelling, sizeof spelling, "%s", name ? name : type);
    } else if (!strcmp(type, "const char *")) {
        snprintf(spelling, sizeof spelling, "string");
    } else if (!strcmp(type, "handle") && name && !strcmp(name, "ecs.entity")) {
        snprintf(spelling, sizeof spelling, "handle<ecs.entity>");
    } else if (!strcmp(type, "struct") && name) {
        snprintf(spelling, sizeof spelling, "struct<%s>", name);
    } else {
        return NULL;
    }
    if (container && av_is_str(container) && !strcmp(av_get_str(container), "array")) {
        strncat(spelling, "[]", sizeof spelling - strlen(spelling) - 1);
    } else if (container && av_is_str(container) && strcmp(av_get_str(container), "single")) {
        return NULL;
    }
    return xstrdup(spelling);
}
/* Reads one event declaration into events; skips other documents. A field
 * keeps a NULL type when the migration cannot spell it, so only a handler
 * that reads it is refused. */
static bool read_event(const char *kdl, payload_event **events, char *error, size_t size)
{
    char problem[256];
    av_doc *document = asset_text_read(kdl, problem, sizeof problem);
    if (!document) {
        return true;
    }
    const av_value *root = av_doc_root(document);
    const av_value *domain = av_obj_get(root, "domain"),
                   *name = av_obj_get(root, "name");
    bool ok = true;
    if (domain && av_is_str(domain) && !strcmp(av_get_str(domain), "event") &&
        name && av_is_str(name)) {
        payload_event event = { xstrdup(av_get_str(name)), NULL };
        const av_value *variables = av_obj_get(root, "variables");
        for (size_t i = 0;
             variables && av_is_arr(variables) && i < av_arr_size(variables);
             i++) {
            const av_value *variable = av_arr_get(variables, i),
                           *label = av_obj_get(variable, "name");
            if (!label || !av_is_str(label)) {
                snprintf(
                    error,
                    size,
                    "event %s has a payload field without a name",
                    event.name
                );
                ok = false;
                break;
            }
            char *type = variable_type(variable);
            add_field(&event, av_get_str(label), type);
            free(type);
        }
        apush(*events, event);
    }
    av_doc_free(document);
    return ok;
}
static void events_free(payload_event *events)
{
    for (int i = 0; i < acount(events); i++) {
        for (int j = 0; j < acount(events[i].fields); j++) {
            free(events[i].fields[j].name);
            free(events[i].fields[j].type);
        }
        afree(events[i].fields);
        free(events[i].name);
    }
    afree(events);
}

typedef struct
{
    const char *text;
    const payload_event *event;
    TSNode parameters; /* The positional list, or a null node for hidden names. */
    const char *alias;
    bool *used;
    char **others; /* Bare names that read no payload field. */
    text_edit **edits;
    char ***owned;
    const char *refusal;
    TSNode refused;
} handler_rewrite;

static void refuse(handler_rewrite *r, TSNode node, const char *reason)
{
    if (!r->refusal) {
        r->refusal = reason;
        r->refused = node;
    }
}
static void replace(handler_rewrite *r, TSNode node, const char *with)
{
    char *owned = xstrdup(with);
    apush(*r->owned, owned);
    apush(*r->edits, ((text_edit){ (int)ts_node_start_byte(node), (int)ts_node_end_byte(node), owned }));
}
static uint32_t named_count(TSNode node)
{
    return ts_node_is_null(node) ? 0 : ts_node_named_child_count(node);
}
/* The payload position of the listed parameter spelled name; comments take
 * none. */
static int positional(const handler_rewrite *r, const char *name)
{
    int ordinal = 0;
    for (uint32_t i = 0; i < named_count(r->parameters); i++) {
        TSNode parameter = ts_node_named_child(r->parameters, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        if (spelled(r->text, field(parameter, "name"), name)) {
            return ordinal;
        }
        ordinal++;
    }
    return -1;
}
static int exact_field(const payload_event *event, const char *name)
{
    for (int k = 0; k < acount(event->fields); k++) {
        if (!strcmp(event->fields[k].name, name)) {
            return k;
        }
    }
    return -1;
}
/* The field spelled name, else the only one matching it as the catalog
 * matches names. */
static int matching_field(const payload_event *event, const char *name)
{
    int found = exact_field(event, name);
    if (found >= 0) {
        return found;
    }
    for (int k = 0; k < acount(event->fields); k++) {
        if (same_name(event->fields[k].name, name)) {
            if (found >= 0) {
                return -1;
            }
            found = k;
        }
    }
    return found;
}
static int in_payload(const handler_rewrite *r, int k)
{
    return k < acount(r->event->fields) ? k : -1;
}
/* Version 29 read `event.F` or `alias.F` as the field F names, else as the
 * listed parameter F. */
static void member_read(handler_rewrite *r, TSNode node, TSNode member)
{
    char *name = text_of(r->text, member);
    int k = matching_field(r->event, name);
    if (k < 0 && !ts_node_is_null(r->parameters)) {
        k = in_payload(r, positional(r, name));
    }
    free(name);
    if (k < 0) {
        refuse(r, node, "reads a payload field its event does not have");
        return;
    }
    r->used[k] = true;
    replace(r, node, r->event->fields[k].name);
}
/* A bare name read the listed parameter it spells, or without a list the
 * field of its own name. */
static void name_read(handler_rewrite *r, TSNode node)
{
    char *name = text_of(r->text, node);
    if (r->alias && !strcmp(name, r->alias)) {
        refuse(r, node, "reads the whole payload through its alias; read its fields");
    }
    int k = in_payload(
        r,
        ts_node_is_null(r->parameters) ? exact_field(r->event, name) : positional(r, name)
    );
    if (k >= 0) {
        r->used[k] = true;
        if (strcmp(name, r->event->fields[k].name)) {
            replace(r, node, r->event->fields[k].name);
        }
        free(name);
        return;
    }
    apush(r->others, name);
}
/* Roles whose identifiers name or select rather than read a value. */
static const char *const NAMING_ROLES[] = {
    "alias",   "attribute", "binding",        "bindings", "comptime",   "event",
    "kind",    "member",    "name",           "outcome",  "package",    "path",
    "storage", "type",      "type_arguments", "unit",     "visibility",
};
/* Whether child i of node is read as a value: not a name it declares or
 * selects, a type, a loop key or a callee. */
static bool read_child(TSNode node, uint32_t i)
{
    TSNode child = ts_node_child(node, i);
    const char *role = ts_node_field_name_for_child(node, i);
    if (!ts_node_is_named(child) || !role) {
        return ts_node_is_named(child);
    }
    for (size_t k = 0; k < sizeof NAMING_ROLES / sizeof *NAMING_ROLES; k++) {
        if (!strcmp(role, NAMING_ROLES[k])) {
            return false;
        }
    }
    if (!strcmp(role, "key")) {
        return !is(node, "for_statement");
    }
    return strcmp(role, "function") || !is(child, "identifier");
}
/* Visits the values node reads. */
static void reads(handler_rewrite *r, TSNode node)
{
    if (r->refusal) {
        return;
    }
    if (is(node, "member_expression")) {
        TSNode object = field(node, "object");
        if ((is(object, "builtin_reference") && spelled(r->text, object, "event")) ||
            (r->alias && is(object, "identifier") &&
             spelled(r->text, object, r->alias))) {
            member_read(r, node, field(node, "member"));
            return;
        }
        reads(r, object);
        return;
    }
    if (is(node, "builtin_reference")) {
        if (spelled(r->text, node, "event")) {
            refuse(r, node, "reads the whole `event`; read its fields");
        }
        return;
    }
    if (is(node, "identifier")) {
        name_read(r, node);
        return;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        if (read_child(node, i)) {
            reads(r, ts_node_child(node, i));
        }
    }
}
/* Whether node declares a parameter, local, constant, loop or pattern name
 * spelled like name. */
static bool declares(const char *text, TSNode node, const char *name)
{
    bool binds = is(node, "parameter") || is(node, "local_declaration") ||
                 is(node, "constant_declaration") ||
                 is(node, "for_statement") || is(node, "is_expression");
    if (binds && (spelled(text, field(node, "name"), name) ||
                  spelled(text, field(node, "key"), name) ||
                  spelled(text, field(node, "binding"), name))) {
        return true;
    }
    for (uint32_t i = 0; i < named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if ((is(node, "pattern_bindings") && spelled(text, child, name)) ||
            declares(text, child, name)) {
            return true;
        }
    }
    return false;
}
/* Whether callable binds name throughout its body: as a parameter, or a
 * local or constant at the top of the body. */
static bool shadows(const char *text, TSNode callable, const char *name)
{
    TSNode parameters = field(callable, "parameters"), body = field(callable, "body");
    for (uint32_t i = 0; i < named_count(parameters); i++) {
        if (spelled(text, field(ts_node_named_child(parameters, i), "name"), name)) {
            return true;
        }
    }
    for (uint32_t i = 0; i < named_count(body); i++) {
        TSNode child = ts_node_named_child(body, i);
        if ((is(child, "local_declaration") || is(child, "constant_declaration")) &&
            spelled(text, field(child, "name"), name)) {
            return true;
        }
    }
    return false;
}
/* Whether code outside handler reads alias, where no callable around the
 * read binds that name itself. */
static bool reads_alias(const char *text, TSNode node, TSNode handler, const char *alias)
{
    bool callable = is(node, "function_declaration") ||
                    is(node, "macro_declaration") || is(node, "handler");
    if (ts_node_eq(node, handler) || (callable && shadows(text, node, alias))) {
        return false;
    }
    if (is(node, "identifier")) {
        return spelled(text, node, alias);
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        if (read_child(node, i) &&
            reads_alias(text, ts_node_child(node, i), handler, alias)) {
            return true;
        }
    }
    return false;
}

typedef struct
{
    const char *text;
    TSNode root;
    payload_event *events;
    text_edit *edits;
    char **owned;
    const char *refusal;
    TSNode refused;
} source_rewrite;

/* ` :: (F: T, ...) `, the fields of event that used marks, in payload order. */
static char *header_text(const payload_event *event, const bool *used)
{
    size_t size = 8;
    for (int k = 0; k < acount(event->fields); k++) {
        size += used[k]
                  ? strlen(event->fields[k].name) + strlen(event->fields[k].type) + 4
                  : 0;
    }
    char *header = xmalloc(size);
    int at = snprintf(header, size, " :: (");
    const char *separator = "";
    for (int k = 0; k < acount(event->fields); k++) {
        if (used[k]) {
            at += snprintf(
                header + at,
                size - (size_t)at,
                "%s%s: %s",
                separator,
                event->fields[k].name,
                event->fields[k].type
            );
            separator = ", ";
        }
    }
    snprintf(header + at, size - (size_t)at, ") ");
    return header;
}
/* The nearest `event name = Other` declaration in scope at node, or a null
 * node. */
static TSNode event_alias(const char *text, TSNode node, TSNode name)
{
    uint32_t length = ts_node_end_byte(name) - ts_node_start_byte(name);
    for (TSNode scope = ts_node_parent(node); !ts_node_is_null(scope);
         scope = ts_node_parent(scope)) {
        for (uint32_t i = 0; i < named_count(scope); i++) {
            TSNode child = ts_node_named_child(scope, i),
                   declared = field(child, "name");
            if (is(child, "event_declaration") &&
                ts_node_end_byte(declared) - ts_node_start_byte(declared) == length &&
                !strncmp(text + ts_node_start_byte(declared), text + ts_node_start_byte(name), length)) {
                return child;
            }
        }
    }
    return (TSNode){ 0 };
}
/* An event the migration cannot find, such as one a test catalog declares,
 * takes its fields from the handler's typed list; without a list it has no
 * payload, as input actions and named events have none. */
static void listed_fields(handler_rewrite *r, payload_event *written)
{
    for (uint32_t i = 0; i < named_count(r->parameters); i++) {
        TSNode parameter = ts_node_named_child(r->parameters, i),
               type = field(parameter, "type");
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        if (ts_node_is_null(type)) {
            refuse(r, parameter, "names an untyped payload field of an event the migration cannot find");
            return;
        }
        char *label = text_of(r->text, field(parameter, "name")),
             *spelling = text_of(r->text, type);
        add_field(written, label, spelling);
        free(label);
        free(spelling);
    }
}
/* `on X(a)`, `on X as e` and `on X` become `on X :: (F: T)`, listing the
 * fields the handler read under their own names, in payload order. */
static void rewrite_handler(source_rewrite *s, TSNode handler)
{
    TSNode name = field(handler, "name"), body = field(handler, "body");
    TSNode alias = field(handler, "alias"), parameters = field(handler, "parameters");
    TSNode declared = event_alias(s->text, handler, name);
    TSNode event = ts_node_is_null(declared) ? name : field(declared, "value");
    char *alias_name = ts_node_is_null(alias) ? NULL : text_of(s->text, alias);
    char *event_name = is(event, "identifier") ? text_of(s->text, event) : NULL;
    payload_event written = { 0 };
    const payload_event *known = event_name ? find_event(s->events, event_name) : NULL;
    handler_rewrite r = { .text = s->text,
                          .event = known ? known : &written,
                          .parameters = named_count(parameters) ? parameters
                                                                : (TSNode){ 0 },
                          .alias = alias_name,
                          .edits = &s->edits,
                          .owned = &s->owned };
    if (!event_name || (!ts_node_is_null(declared) &&
                        !ts_node_is_null(event_alias(s->text, declared, event)))) {
        refuse(&r, name, "names its event by a path or an alias of an alias; rewrite it by hand");
    }
    if (!known) {
        listed_fields(&r, &written);
    }
    int count = acount(r.event->fields);
    r.used = xcalloc((size_t)count + 1, sizeof *r.used);
    int listed = 0;
    for (uint32_t i = 0; i < named_count(r.parameters); i++) {
        TSNode parameter = ts_node_named_child(r.parameters, i);
        if (ts_node_is_extra(parameter)) {
            continue;
        }
        listed++;
        char *parameter_name = text_of(s->text, field(parameter, "name"));
        if (declares(s->text, body, parameter_name)) {
            refuse(&r, handler, "declares a name that a payload parameter uses; rename it");
        }
        free(parameter_name);
    }
    if (listed > count) {
        refuse(&r, r.parameters, "lists more parameters than its event's payload has");
    }
    if (alias_name && declares(s->text, body, alias_name)) {
        refuse(&r, handler, "declares a name that its payload alias uses; rename it");
    }
    reads(&r, body);
    for (int k = 0; k < count && !r.refusal; k++) {
        const char *field_name = r.event->fields[k].name;
        if (declares(s->text, body, field_name)) {
            refuse(&r, handler, "declares a name that a payload field uses; rename it");
        }
        for (int i = 0; r.used[k] && listed && i < acount(r.others); i++) {
            if (!strcmp(r.others[i], field_name)) {
                refuse(&r, handler, "reads a payload field's name with another meaning; rename it");
            }
        }
        if (r.used[k] && !r.event->fields[k].type) {
            refuse(&r, handler, "reads a payload field whose declared type the migration cannot spell; name it by hand");
        }
    }
    if (alias_name && !r.refusal && reads_alias(s->text, s->root, handler, alias_name)) {
        refuse(&r, alias, "reads its payload alias outside the handler; move that code into it");
    }
    if (r.refusal) {
        s->refusal = r.refusal;
        s->refused = r.refused;
    } else {
        char *header = header_text(r.event, r.used);
        apush(s->owned, header);
        apush(s->edits, ((text_edit){ (int)ts_node_end_byte(name), (int)ts_node_start_byte(body), header }));
    }
    for (int i = 0; i < acount(r.others); i++) {
        free(r.others[i]);
    }
    afree(r.others);
    for (int k = 0; k < acount(written.fields); k++) {
        free(written.fields[k].name);
        free(written.fields[k].type);
    }
    afree(written.fields);
    free(r.used);
    free(alias_name);
    free(event_name);
}
static void rewrite_handlers(source_rewrite *s, TSNode node)
{
    if (s->refusal) {
        return;
    }
    if (is(node, "handler") && ts_node_is_null(field(node, "binding"))) {
        TSNode kind = field(node, "kind");
        if (spelled(s->text, kind, "on")) {
            rewrite_handler(s, node);
            return;
        }
        if (!ts_node_is_null(field(node, "alias"))) {
            s->refusal = "names the payload of an `after` handler; read the "
                         "action's results";
            s->refused = field(node, "alias");
            return;
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        rewrite_handlers(s, ts_node_named_child(node, i));
    }
}

/* The engine's events as version 29 declared them, then the project's. */
static payload_event *version_29_events(const char *const *declarations, int count, char *error, size_t size)
{
    payload_event *events = NULL;
    for (size_t i = 0; i < sizeof VERSION_29_EVENTS / sizeof *VERSION_29_EVENTS; i++) {
        payload_event *event = find_event(events, VERSION_29_EVENTS[i].event);
        if (!event) {
            apush(events, ((payload_event){ xstrdup(VERSION_29_EVENTS[i].event), NULL }));
            event = &events[acount(events) - 1];
        }
        if (VERSION_29_EVENTS[i].field) {
            add_field(event, VERSION_29_EVENTS[i].field, VERSION_29_EVENTS[i].type);
        }
    }
    for (int i = 0; i < count; i++) {
        if (!read_event(declarations[i], &events, error, size)) {
            events_free(events);
            return NULL;
        }
    }
    return events;
}

int chibi_migrate29_handlers(
    const char *source,
    const char *const *declarations,
    int declaration_count,
    char **migrated,
    char *error,
    size_t size
)
{
    *migrated = NULL;
    script_syntax *syntax = chibi_migrate_parse(source, 29);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        snprintf(error, size, "Cannot migrate invalid version 29 source");
        return -1;
    }
    payload_event *events = version_29_events(declarations, declaration_count, error, size);
    if (!events) {
        script_syntax_free(syntax);
        return -1;
    }
    source_rewrite s = { .text = source,
                         .root = ts_tree_root_node(syntax->tree),
                         .events = events };
    rewrite_handlers(&s, s.root);
    int status = 0;
    if (s.refusal) {
        script_range range = script_node_range(s.refused);
        snprintf(error, size, "handler at %d:%d %s", range.line, range.column, s.refusal);
        status = -1;
    } else if (acount(s.edits)) {
        *migrated = text_edits_apply(source, s.edits, acount(s.edits));
    }
    for (int i = 0; i < acount(s.owned); i++) {
        free(s.owned[i]);
    }
    afree(s.owned);
    afree(s.edits);
    events_free(events);
    script_syntax_free(syntax);
    return status;
}
