#include "assets/refactor.h"

#include "assets/asset_source.h"
#include "assets/scene_document.h"
#include "assets/ui_document.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/events.h"
#include "graph/document/graph_signature.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "runtime/declared_components.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *reference_text(pin_value value)
{
    pin_string text = pin_text(value);
    return !memchr(text.bytes, 0, text.length) && !text.bytes[text.length]
             ? text.bytes
             : NULL;
}
static pin_value renamed_value(pin_type type, const char *bytes, pin_string *text)
{
    if (type == PIN_NAME) {
        return (pin_value){ .type = PIN_NAME, .str = bytes };
    }
    *text = (pin_string){ .bytes = bytes,
                          .length = bytes ? (uint32_t)strlen(bytes) : 0 };
    return (pin_value){ .type = PIN_STR, .text = text };
}

#define NAME_MAX_LENGTH 120

/* MeshParts.parts holds code parts, each naming its material. */
#define MESH_PARTS "MeshParts.parts"

/* An asset the plan edits: parsed on first use, unless only deleted. */
typedef struct
{
    int asset;
    bool parsed;
    behavior_asset *graphs; /* declarative graph assets */
    prototype *prototype;
    scene_document *scene;
    char *rml; /* a ui asset's markup */
    char *name;
    bool deleted;
    const reference **references;
} loaded;

typedef struct
{
    const refactor_project *project;
    reference_symbol symbol;
    loaded **assets;
    char *error;
    size_t error_size;
} work;

static bool fail(work *w, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(w->error, w->error_size, format, args);
    va_end(args);
    return false;
}

static const reference_asset *asset_at(const work *w, int asset)
{
    return reference_index_asset_at(w->project->index, asset);
}

static const char *asset_name(const work *w, int asset)
{
    return asset_at(w, asset)->name;
}

static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

/* ---------- Loading and saving ---------- */

static void release(loaded *l)
{
    behavior_asset_free(l->graphs);
    if (l->prototype) {
        prototype_free(l->prototype);
    }
    scene_document_free(l->scene);
    free(l->rml);
    l->rml = NULL;
    l->graphs = NULL;
    l->prototype = NULL;
    l->scene = NULL;
}

/* A ui asset's markup, kept as written: renames rewrite it in place. */
static bool read_markup(const char *text, loaded *l, char *why, size_t size)
{
    ui_document document = { 0 };
    bool read = ui_document_read(text, NULL, NULL, NULL, &document, why, size);
    ui_document_free(&document);
    l->rml = read ? xstrdup(text) : NULL;
    return read;
}

/* Reads an asset's text with its kind's loader; false with why. */
static bool read_asset(
    const refactor_project *project,
    reference_asset_kind kind,
    const char *text,
    loaded *l,
    char *why,
    size_t size
)
{
    switch (kind) {
        case REFERENCE_ASSET_SYSTEM:
        case REFERENCE_ASSET_BEHAVIOR:
        case REFERENCE_ASSET_RELATIONSHIP:
            snprintf(why, size, "Executable source uses canonical source refactoring");
            return false;
        case REFERENCE_ASSET_PROTOTYPE:
            /* As written: renames edit its own content, and the prototypes it
             * names may be renamed in the same plan. */
            l->prototype = prototype_load_authored(text, project->registry, why, size);
            return l->prototype != NULL;
        case REFERENCE_ASSET_SCENE:
            l->scene = scene_document_load(text, why, size);
            return l->scene != NULL;
        case REFERENCE_ASSET_UI: return read_markup(text, l, why, size);
        case REFERENCE_ASSET_KIND_COUNT: break;
        default:
            l->graphs = behavior_asset_load(text);
            snprintf(why, size, "%s", l->graphs ? "" : graph_last_error());
            return l->graphs != NULL;
    }
    return false;
}

static loaded *entry(work *w, int asset)
{
    for (int i = 0; i < acount(w->assets); i++) {
        if (w->assets[i]->asset == asset) {
            return w->assets[i];
        }
    }
    loaded *l = xcalloc(1, sizeof *l);
    l->asset = asset;
    apush(w->assets, l);
    return l;
}

/* The asset, parsed for editing; NULL with the error set. */
static loaded *load(work *w, int asset)
{
    loaded *l = entry(w, asset);
    if (l->parsed) {
        return l;
    }
    const reference_asset *a = asset_at(w, asset);
    const char *text = w->project->texts[asset];
    char why[256] = "";
    if (!text) {
        fail(w, "%s has no saved text to rewrite", a->name);
        return NULL;
    }
    if (!read_asset(w->project, a->kind, text, l, why, sizeof why)) {
        fail(w, "%s does not load: %s", a->name, why);
        return NULL;
    }
    l->parsed = true;
    return l;
}

static void note(loaded *l, const reference *r)
{
    for (int i = 0; r && i < acount(l->references); i++) {
        if (l->references[i] == r) {
            return;
        }
    }
    if (r) {
        apush(l->references, r);
    }
}

static char *saved_source(const refactor_project *, reference_asset_kind kind, loaded *l)
{
    switch (kind) {
        case REFERENCE_ASSET_SYSTEM:
        case REFERENCE_ASSET_BEHAVIOR:
        case REFERENCE_ASSET_RELATIONSHIP: return NULL;
        case REFERENCE_ASSET_PROTOTYPE: return prototype_save(l->prototype);
        case REFERENCE_ASSET_SCENE:
            return scene_document_save(l->scene, project_components());
        case REFERENCE_ASSET_KIND_COUNT: break;
        default: return behavior_asset_save(l->graphs);
    }
    return NULL;
}

/* Rewritten callers resolve against the signatures that this plan changes. */
/* Reconcile declarative edits with the original asset text. */
static char *saved_text(
    const refactor_project *project,
    reference_asset_kind kind,
    loaded *l,
    char *why,
    size_t size
)
{
    if (kind == REFERENCE_ASSET_UI) {
        return xstrdup(l->rml);
    }
    char *emitted = saved_source(project, kind, l);
    av_doc *doc = emitted ? av_doc_read(emitted, why, size) : NULL;
    char *text = doc
                   ? asset_source_reconcile(project->texts[l->asset], av_doc_root(doc), why, size)
                   : NULL;
    av_doc_free(doc);
    free(emitted);
    return text;
}

/* The asset's new file text, checked to load again; NULL with the error. */
static char *file_text(work *w, loaded *l)
{
    reference_asset_kind kind = asset_at(w, l->asset)->kind;
    char why[256] = "";
    char *text = saved_text(w->project, kind, l, why, sizeof why);
    loaded check = { .asset = l->asset };
    if (!text || !read_asset(w->project, kind, text, &check, why, sizeof why)) {
        fail(w, "the rewritten %s would not load: %s", asset_name(w, l->asset), why);
        free(text);
        text = NULL;
    }
    release(&check);
    return text;
}

/* ---------- Places ---------- */

/* Whether two references sit at one place, as two parts of one mesh naming
 * the same material, which one rewrite renames together. */
static bool same_place(const reference *a, const reference *b)
{
    return a->source == b->source && a->role == b->role &&
           a->graph_id == b->graph_id && a->node_id == b->node_id &&
           a->pin_id == b->pin_id && same_text(a->object, b->object) &&
           same_text(a->component, b->component);
}

/* The field a reference's component names: Ui.document's document. */
static const char *component_field(const reference *r)
{
    const char *dot = strchr(r->component, '.');
    assert(dot);
    return dot + 1;
}

/* The component name before the field, in `out`. */
static const char *component_of(const reference *r, char *out, size_t size)
{
    snprintf(out, size, "%.*s", (int)(component_field(r) - 1 - r->component), r->component);
    return out;
}

static bool mesh_parts(const reference *r)
{
    return r->component && !strcmp(r->component, MESH_PARTS);
}

/* Parts code with each part's material `from` renamed; NULL without one. */
static char *parts_renamed(const char *parts, const char *from, const char *to, int *count)
{
    av_doc *read = parts ? av_value_read(parts, NULL, 0) : NULL;
    av_doc *doc = read ? av_doc_clone_value(av_doc_root(read)) : NULL;
    av_doc_free(read);
    size_t i, max;
    av_value *part;
    *count = 0;
    av_arr_foreach(av_doc_root(doc), i, max, part)
    {
        av_value *material = av_obj_get(part, "material");
        if (av_is_str(material) && !strcmp(av_get_str(material), from)) {
            av_set_str(material, to);
            ++*count;
        }
    }
    char *text = *count ? av_value_write(av_doc_root(doc)) : NULL;
    av_doc_free(doc);
    return text;
}

/* A text value at a place, renamed: the value itself, or a mesh's parts. */
static char *value_renamed(
    const reference *r,
    const char *value,
    const char *from,
    const char *to,
    int *count
)
{
    if (mesh_parts(r)) {
        return parts_renamed(value, from, to, count);
    }
    *count = same_text(value, from);
    return *count ? xstrdup(to) : NULL;
}

static bool renamed_count(work *w, const reference *r, int count, int expected)
{
    char place[256];
    refactor_describe(w->project->index, r, place, sizeof place);
    return count == expected ||
           fail(w, "%s changed since it was indexed; refresh and retry", place);
}

/* ---------- Graph places ---------- */

static node *node_at(work *w, loaded *l, const reference *r, graph **g)
{
    *g = behavior_asset_find_graph(l->graphs, r->graph_id);
    node *n = *g ? graph_node_by_id(*g, r->node_id) : NULL;
    if (!n) {
        fail(
            w,
            "%s has no node %llu",
            asset_name(w, l->asset),
            (unsigned long long)r->node_id
        );
    }
    return n;
}

/* A struct default's field, as a Shader value's material. */
static bool rename_struct_field(
    work *w,
    graph *g,
    node *n,
    int output,
    int index,
    const reference *r,
    const char *from,
    const char *to,
    int expected
)
{
    const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
    const type_desc *type = pin->type_name ? type_catalog_find(pin->type_name) : NULL;
    const type_field *field = type ? pin_type_field_path(type, component_field(r))
                                   : NULL;
    if (!field || !pin->default_value.ptr) {
        return fail(w, "%s holds no %s value", asset_name(w, r->source), r->component);
    }
    void *copy = xmalloc(type->size);
    memcpy(copy, pin->default_value.ptr, type->size);
    arena storage = { 0 };
    pin_value current;
    pin_read_field(&storage, copy, field, &current);
    int count;
    char *renamed = value_renamed(r, reference_text(current), from, to, &count);
    pin_value value = pin->default_value;
    value.ptr = copy;
    if (renamed) {
        pin_string text;
        pin_write_field(&storage, copy, field, renamed_value(field->type, renamed, &text));
    }
    bool ok = renamed_count(w, r, count, expected) &&
              (graph_set_pin_default(g, n, output, index, &value) == 0 ||
               fail(w, "could not set %s in %s", r->component, asset_name(w, r->source)));
    free(renamed);
    free(copy);
    arena_free(&storage);
    return ok;
}

/* A pin default that names the symbol, or a struct default's field. */
static bool rename_pin(work *w, graph *g, node *n, const reference *r, const char *from, const char *to, int expected)
{
    int output = 0, index = -1;
    const pin_spec *pin = graph_pin_by_id(n, r->pin_id, &output, &index);
    if (!pin) {
        return fail(
            w,
            "%s has no pin %llu",
            asset_name(w, r->source),
            (unsigned long long)r->pin_id
        );
    }
    if (pin->type == PIN_STRUCT && r->component) {
        return rename_struct_field(w, g, n, output, index, r, from, to, expected);
    }
    int count;
    char *renamed = value_renamed(r, reference_text(pin->default_value), from, to, &count);
    pin_value value = pin->default_value;
    pin_string text;
    value = renamed_value(value.type, renamed, &text);
    bool ok = renamed_count(w, r, count, expected) &&
              (graph_set_pin_default(g, n, output, index, &value) == 0 ||
               fail(w, "could not set a pin in %s", asset_name(w, r->source)));
    free(renamed);
    return ok;
}

static bool rename_node(work *w, loaded *l, const reference *r, const char *from, const char *to, int expected)
{
    graph *g;
    node *n = node_at(w, l, r, &g);
    if (!n) {
        return false;
    }
    if (r->pin_id &&
        (r->component || r->role == REFERENCE_SPAWN ||
         (r->role == REFERENCE_BIND && w->symbol.kind == REFERENCE_SYMBOL_ASSET))) {
        return rename_pin(w, g, n, r, from, to, expected);
    }
    if (r->role == REFERENCE_CALL && w->symbol.kind == REFERENCE_SYMBOL_ASSET) {
        if (!renamed_count(w, r, same_text(n->reference_asset, from), 1)) {
            return false;
        }
        n->reference_asset = graph_own_str(g, to, strlen(to));
        return true;
    }
    /* Events match by reference_name; a call's only labels it, so a stale
       one is renamed too. */
    bool event = r->role == REFERENCE_HANDLE || r->role == REFERENCE_SEND;
    if (event && !renamed_count(w, r, same_text(n->reference_name, from), 1)) {
        return false;
    }
    if (!event && r->role != REFERENCE_CALL) {
        return fail(w, "cannot rename a node's reference in %s", asset_name(w, r->source));
    }
    return graph_set_node_reference(g, n, n->reference_id, to) == 0 ||
           fail(w, "%s is running; stop it and retry", asset_name(w, r->source));
}

/* ---------- Prototype, scene and ui places ---------- */

/* Whether a behavior list can swap `from` for `to`. */
static bool swappable(work *w, char *const *list, const char *from, const char *to)
{
    bool found = false;
    for (int i = 0; i < acount(list); i++) {
        if (!strcmp(list[i], to)) {
            return fail(w, "%s is already attached there", to);
        }
        found |= !strcmp(list[i], from);
    }
    return found || fail(w, "%s is no longer attached there; refresh and retry", from);
}

static bool rename_prototype_field(
    work *w,
    prototype *prototype,
    const reference *r,
    const char *from,
    const char *to,
    int expected
)
{
    char component[128];
    component_of(r, component, sizeof component);
    arena storage = { 0 };
    pin_string text;
    pin_value current = { 0 };
    prototype_read_field(&storage, prototype, component, component_field(r), &current);
    int count;
    char *renamed = value_renamed(r, reference_text(current), from, to, &count);
    bool ok = renamed_count(w, r, count, expected) &&
              (prototype_write_field(
                   prototype,
                   component,
                   component_field(r),
                   renamed_value(PIN_STR, renamed, &text)
               ) == 0 ||
               fail(w, "could not set %s in %s", r->component, prototype->name));
    free(renamed);
    arena_free(&storage);
    return ok;
}

/* An override's components: the object itself, or its components. */
static av_value *override_components(av_value *root)
{
    av_value *components = av_obj_get(root, "components");
    return components ? components : root;
}

/* An object's overrides with the field renamed. An object whose overrides
   leave the field to its prototype holds nothing to rename here. */
static bool rename_override(
    work *w,
    scene_document *scene,
    int entity,
    const reference *r,
    const char *from,
    const char *to,
    int expected
)
{
    av_value *components = scene->entities[entity].components;
    av_value *copy = components ? av_value_copy(components) : NULL;
    char component[128];
    av_value *value = av_obj_get(
        av_obj_get(override_components(copy), component_of(r, component, sizeof component)),
        component_field(r)
    );
    int count = 0;
    char *renamed = value ? value_renamed(r, av_get_str(value), from, to, &count)
                          : NULL;
    bool ok = !value || renamed_count(w, r, count, expected);
    if (ok && renamed) {
        av_set_str(value, renamed);
        ok = scene_document_set_entity_components(scene, entity, copy) == 0 ||
             fail(w, "could not set %s on %s", r->component, r->object);
    }
    free(renamed);
    av_value_free(copy);
    return ok;
}

/* A scene's page: its own ui: follows; the project's default is
 * project.kdl's, which the plan does not reach. */
static bool rename_page(work *w, loaded *l, const reference *r, const char *from, const char *to, int expected)
{
    if (strcmp(r->component, REFERENCE_SCENE_PAGE)) {
        return fail(w, "project.kdl names %s as its scenes' page; point it at another first", from);
    }
    bool named = same_text(l->scene->ui, from);
    scene_document_set_ui(l->scene, to);
    return renamed_count(w, r, named, expected);
}

/* A scene's object, or a prototype's child, by its path. */
static bool rename_entity(
    work *w,
    loaded *l,
    scene_document *scene,
    const reference *r,
    const char *from,
    const char *to,
    int expected
)
{
    int entity = scene_document_entity_index(scene, r->object);
    if (entity < 0) {
        return fail(w, "%s has no object %s", asset_name(w, l->asset), r->object);
    }
    scene_entity *object = &scene->entities[entity];
    if (r->role == REFERENCE_USE) {
        return renamed_count(w, r, same_text(object->prototype, from), 1) &&
               (scene_document_set_entity_prototype(scene, entity, to) == 0 ||
                fail(w, "could not place %s as %s", to, r->object));
    }
    if (r->role != REFERENCE_ATTACH) {
        return r->component
                 ? rename_override(w, scene, entity, r, from, to, expected)
                 : fail(w, "cannot rename a reference on %s", r->object);
    }
    if (!swappable(w, object->behaviors, from, to)) {
        return false;
    }
    const char **names = NULL;
    for (int i = 0; i < acount(object->behaviors); i++) {
        apush(names, strcmp(object->behaviors[i], from) ? object->behaviors[i] : to);
    }
    bool ok = scene_document_set_entity_behaviors(scene, entity, names, acount(names)) ==
              0;
    afree(names);
    return ok || fail(w, "could not attach %s to %s", to, r->object);
}

static bool rename_prototype(work *w, loaded *l, const reference *r, const char *from, const char *to, int expected)
{
    prototype *prototype = l->prototype;
    if (r->object) {
        return rename_entity(w, l, prototype->children, r, from, to, expected);
    }
    if (r->role != REFERENCE_ATTACH) {
        return r->component
                 ? rename_prototype_field(w, prototype, r, from, to, expected)
                 : fail(w, "cannot rename a reference in %s", prototype->name);
    }
    return swappable(w, prototype->graphs, from, to) &&
           ((prototype_remove_graph(prototype, from) == 0 &&
             prototype_add_graph(prototype, to) == 0) ||
            fail(w, "could not attach %s to %s", to, prototype->name));
}

static bool rename_object(work *w, loaded *l, const reference *r, const char *from, const char *to, int expected)
{
    return r->object ? rename_entity(w, l, l->scene, r, from, to, expected)
                     : rename_page(w, l, r, from, to, expected);
}

/* Widget names, or events a ui document sends. */
static bool rename_ui(work *w, loaded *l, const reference *r, const char *from, const char *to, int expected)
{
    bool event = r->role == REFERENCE_SEND;
    if (event && strpbrk(to, " :\"'")) {
        return fail(w, "%s cannot send an event named \"%s\"", asset_name(w, l->asset), to);
    }
    int count;
    char *rml = ui_document_rename(l->rml, event, from, to, &count);
    free(l->rml);
    l->rml = rml;
    /* One element's keys bound to one event are one reference. */
    return renamed_count(w, r, count > expected ? expected : count, expected);
}

static bool rename_place(work *w, loaded *l, const reference *r, const char *from, const char *to, int expected)
{
    switch (asset_at(w, l->asset)->kind) {
        case REFERENCE_ASSET_UI: return rename_ui(w, l, r, from, to, expected);
        case REFERENCE_ASSET_PROTOTYPE:
            return rename_prototype(w, l, r, from, to, expected);
        case REFERENCE_ASSET_SCENE:
            return rename_object(w, l, r, from, to, expected);
        default: break;
    }
    return r->node_id
             ? rename_node(w, l, r, from, to, expected)
             : fail(w, "cannot rename a reference in %s", asset_name(w, l->asset));
}

static char *qualified_name(const char *library, const char *function)
{
    size_t size = strlen(library) + strlen(function) + 2;
    char *name = xmalloc(size);
    snprintf(name, size, "%s.%s", library, function);
    return name;
}

static bool rename_binding(work *w, loaded *l, const reference *r, const char *from, const char *to, int expected)
{
    if (r->role != REFERENCE_BIND || r->symbol.kind != REFERENCE_SYMBOL_GRAPH ||
        !r->component || strcmp(r->component, "Shader.material")) {
        return rename_place(w, l, r, from, to, expected);
    }
    bool library = w->symbol.kind == REFERENCE_SYMBOL_ASSET;
    const char *owner = asset_name(w, r->symbol.asset);
    char *before = qualified_name(library ? from : owner, library ? r->symbol.name : from);
    char *after = qualified_name(library ? to : owner, library ? r->symbol.name : to);
    bool ok = rename_place(w, l, r, before, after, expected);
    free(before);
    free(after);
    return ok;
}

/* Renames each place that names `from` once: the references at one place
   rename together. */
static bool rename_uses(work *w, const reference **uses, const char *from, const char *to)
{
    for (int i = 0; i < acount(uses); i++) {
        loaded *l = load(w, uses[i]->source);
        if (!l) {
            return false;
        }
        note(l, uses[i]);
        bool first = true;
        int expected = 0;
        for (int j = 0; j < acount(uses); j++) {
            first &= j >= i || !same_place(uses[j], uses[i]);
            expected += same_place(uses[j], uses[i]);
        }
        if (first && !rename_binding(w, l, uses[i], from, to, expected)) {
            return false;
        }
    }
    return true;
}

/* ---------- Symbols ---------- */

static bool valid_name(work *w, const char *name)
{
    size_t length = name ? strlen(name) : 0;
    if (!length || length > NAME_MAX_LENGTH || isspace((unsigned char)name[0]) ||
        isspace((unsigned char)name[length - 1])) {
        return fail(w, "a name is 1 to %d characters without spaces at its ends", NAME_MAX_LENGTH);
    }
    for (const char *p = name; *p; p++) {
        if (iscntrl((unsigned char)*p)) {
            return fail(w, "a name has no control characters");
        }
    }
    return true;
}

/* Asset names name files, and events name their definitions' files. */
static bool file_safe(work *w, const char *name)
{
    return (name[0] != '.' && !strpbrk(name, "/\\:*?\"<>|") && !strstr(name, "..")) ||
           fail(w, "\"%s\" cannot name a file", name);
}

/* File systems that ignore case cannot tell a rename that only changes case
   from the file itself. */
static bool file_name(work *w, const char *from, const char *to)
{
    return file_safe(w, to) &&
           (strcasecmp(from, to) ||
            fail(w, "%s differs from %s only in case; rename it to another name first", to, from));
}

/* Another asset of the kind whose file the name would take. */
static bool name_taken(const work *w, reference_asset_kind kind, int asset, const char *name)
{
    const reference_index *index = w->project->index;
    for (int i = 0; i < reference_index_asset_count(index); i++) {
        const reference_asset *other = reference_index_asset_at(index, i);
        if (i != asset && other->kind == kind && other->name &&
            !strcasecmp(other->name, name)) {
            return true;
        }
    }
    return false;
}

static const char *const KIND_NAMES[REFERENCE_ASSET_KIND_COUNT] = {
    "scene", "prototype", "behavior", "event",       "material",     "shape",
    "field", "library",   "system",   "ui document", "relationship",
};

static const reference **uses_of(const work *w, reference_symbol symbol)
{
    const reference **uses = NULL;
    reference_index_find_uses(w->project->index, symbol, &uses);
    return uses;
}

static reference_symbol event_named(const work *w, const char *name)
{
    int definition = reference_index_event_asset(w->project->index, name);
    return (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT,
                               .asset = definition,
                               .name = name };
}

/* Every asset rename names its file here. */
static bool name_asset(work *w, int asset, const char *to)
{
    const reference_asset *a = asset_at(w, asset);
    if (!file_name(w, a->name, to)) {
        return false;
    }
    if (name_taken(w, a->kind, asset, to)) {
        return fail(w, "a %s named %s exists", KIND_NAMES[a->kind], to);
    }
    loaded *l = load(w, asset);
    if (!l) {
        return false;
    }
    if (l->graphs && behavior_asset_set_name(l->graphs, to)) {
        return fail(w, "%s is running; stop it and retry", asset_name(w, asset));
    }
    if (l->prototype) {
        free(l->prototype->name);
        l->prototype->name = xstrdup(to);
    }
    l->name = xstrdup(to);
    return true;
}

static bool rename_event(work *w, const char *from, const char *to)
{
    reference_symbol event = event_named(w, from);
    if (host_event_reserved(from) || host_event_reserved(to)) {
        return fail(w, "the engine names its host events");
    }
    if (!file_safe(w, to) || (event.asset >= 0 && !name_asset(w, event.asset, to))) {
        return false;
    }
    if (reference_index_find_name(w->project->index, REFERENCE_ASSET_EVENT, to) >= 0 ||
        reference_index_count_uses(w->project->index, event_named(w, to), ~0u)) {
        return fail(w, "an event named %s exists", to);
    }
    const reference **uses = uses_of(w, event);
    bool ok = (acount(uses) || event.asset >= 0 ||
               fail(w, "nothing names the event %s", from)) &&
              rename_uses(w, uses, from, to);
    afree(uses);
    if (ok && event.asset >= 0) {
        note(entry(w, event.asset), reference_index_definition(w->project->index, event));
    }
    return ok;
}

static bool rename_asset(work *w, int asset, const char *to)
{
    const reference_asset *a = asset_at(w, asset);
    if (a->kind == REFERENCE_ASSET_EVENT) {
        return rename_event(w, a->name, to);
    }
    if (a->unresolved || a->kind == REFERENCE_ASSET_SCENE) {
        return fail(
            w,
            a->unresolved ? "%s is missing"
                          : "the project file names scenes; %s keeps its name",
            a->name
        );
    }
    char *from = xstrdup(a->name);
    bool ok = name_asset(w, asset, to);
    const reference **uses = ok ? uses_of(w, (reference_symbol){ .kind = REFERENCE_SYMBOL_ASSET, .asset = asset })
                                : NULL;
    ok = ok && rename_uses(w, uses, from, to);
    afree(uses);
    free(from);
    return ok;
}

static graph *member_graph(work *w, loaded *l, uint64_t graph_id)
{
    graph *g = behavior_asset_find_graph(l->graphs, graph_id);
    if (!g) {
        fail(w, "%s has no graph %llu", asset_name(w, l->asset), (unsigned long long)graph_id);
    }
    return g;
}

static bool rename_graph(work *w, reference_symbol symbol, const char *to)
{
    loaded *l = load(w, symbol.asset);
    graph *g = l ? member_graph(w, l, symbol.id) : NULL;
    if (!g) {
        return false;
    }
    if (g->kind != GRAPH_FUNCTION && g->kind != GRAPH_MACRO) {
        return fail(w, "only functions and macros rename here");
    }
    char *from = xstrdup(g->name ? g->name : "");
    bool ok = behavior_asset_rename_graph(l->graphs, g->graph_id, to) == 0 ||
              fail(w, "%s already has a graph named %s", asset_name(w, symbol.asset), to);
    const reference **uses = ok ? uses_of(w, symbol) : NULL;
    note(l, reference_index_definition(w->project->index, symbol));
    ok = ok && rename_uses(w, uses, from, to);
    afree(uses);
    free(from);
    return ok;
}

/* The payload pin an event node gives for a variable of the event's
   definition. */
static pin_spec *payload_pin(node *n, uint64_t variable, int *output, int *index)
{
    for (*output = 0; *output < 2; ++*output) {
        pin_spec *pins = *output ? n->output_pins : n->input_pins;
        for (*index = 0; *index < (*output ? n->output_count : n->input_count);
             ++*index) {
            if (pins[*index].declaration_id == variable) {
                return &pins[*index];
            }
        }
    }
    return NULL;
}

/* Whether a pin on that side of `n`, other than the one at `except`, has
   the name. */
static bool pin_named(const node *n, int output, int except, const char *name)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    for (int i = 0; i < (output ? n->output_count : n->input_count); i++) {
        if (i != except && same_text(pins[i].name, name)) {
            return true;
        }
    }
    return false;
}

/* Ui documents and action bindings send an event without a payload; senders
   and handlers in graphs carry it. */
static bool carries_payload(const reference *r)
{
    return r->node_id && !r->pin_id;
}

/* Senders and handlers name an event definition's variables on their
   payload pins, beside their own pins such as Entity and Then. */
static bool rename_payload(work *w, int definition, uint64_t variable, const char *to)
{
    const reference **uses = uses_of(w, event_named(w, asset_name(w, definition)));
    bool ok = true;
    for (int i = 0; ok && i < acount(uses); i++) {
        const reference *r = uses[i];
        if (!carries_payload(r)) {
            continue;
        }
        loaded *l = load(w, r->source);
        graph *g = NULL;
        node *n = l ? node_at(w, l, r, &g) : NULL;
        int output, index;
        ok = n != NULL;
        if (n && payload_pin(n, variable, &output, &index)) {
            note(l, r);
            ok = (!pin_named(n, output, index, to) ||
                  fail(
                      w,
                      "%s's %s node already has a pin named %s",
                      asset_name(w, r->source),
                      n->desc->name,
                      to
                  )) &&
                 (graph_set_pin_name(g, n, output, index, to) == 0 ||
                  fail(w, "could not rename the payload in %s", asset_name(w, r->source)));
        }
    }
    afree(uses);
    return ok;
}

static bool rename_variable(work *w, reference_symbol symbol, const char *to)
{
    loaded *l = load(w, symbol.asset);
    if (!l) {
        return false;
    }
    behavior_variable *v = behavior_asset_find_variable_by_id(l->graphs, symbol.id);
    if (!v) {
        return fail(
            w,
            "%s has no variable %llu",
            asset_name(w, symbol.asset),
            (unsigned long long)symbol.id
        );
    }
    if (behavior_asset_rename_variable(l->graphs, v->id, to)) {
        return fail(
            w,
            "%s already has a variable named %s",
            asset_name(w, symbol.asset),
            to
        );
    }
    /* Get and Set nodes follow the rename; bindings name the variable by ID. */
    const reference **uses = uses_of(w, symbol);
    for (int i = 0; i < acount(uses); i++) {
        note(l, uses[i]);
    }
    afree(uses);
    note(l, reference_index_definition(w->project->index, symbol));
    return asset_at(w, symbol.asset)->kind != REFERENCE_ASSET_EVENT ||
           rename_payload(w, symbol.asset, symbol.id, to);
}

/* A call in another asset names a parameter on its pin, by declaration or,
   without one, by name; the renamed pin keeps its declaration. */
static bool rename_call_pin(
    work *w,
    const reference *call,
    bool result,
    uint64_t parameter,
    const char *from,
    const char *to
)
{
    loaded *l = load(w, call->source);
    graph *g = NULL;
    node *n = l ? node_at(w, l, call, &g) : NULL;
    if (!n) {
        return false;
    }
    note(l, call);
    pin_spec *pins = result ? n->output_pins : n->input_pins;
    for (int i = 0; i < (result ? n->output_count : n->input_count); i++) {
        pin_spec *pin = &pins[i];
        bool named = !pin->declaration_id && pin->kind == PIN_DATA &&
                     !pin->field_source && same_text(pin->name, from);
        if (pin->declaration_id == parameter || named) {
            pin->declaration_id = parameter;
            return graph_set_pin_name(g, n, result, i, to) == 0 ||
                   fail(w, "could not rename a call's pin in %s", asset_name(w, call->source));
        }
    }
    return true;
}

static bool rename_parameter(work *w, reference_symbol symbol, const char *to)
{
    loaded *l = load(w, symbol.asset);
    if (l && l->graphs && l->graphs->domain == BEHAVIOR_DOMAIN_MODEL && !symbol.graph_id) {
        graph *owner = NULL;
        node *parameter = NULL;
        for (int i = 0; i < acount(l->graphs->graphs); i++) {
            graph *candidate = l->graphs->graphs[i];
            for (int j = 0; j < acount(candidate->nodes); j++) {
                node *n = candidate->nodes[j];
                if ((!strcmp(n->desc->name, "Model Parameter") ||
                     !strcmp(n->desc->name, "Model Material Slot") ||
                     !strcmp(n->desc->name, "Model Anchor")) &&
                    n->id != symbol.id && same_text(n->reference_name, to)) {
                    return fail(w, "the model already has a parameter named %s", to);
                }
                if (n->id == symbol.id &&
                    (!strcmp(n->desc->name, "Model Parameter") ||
                     !strcmp(n->desc->name, "Model Material Slot") ||
                     !strcmp(n->desc->name, "Model Anchor"))) {
                    owner = candidate;
                    parameter = n;
                }
            }
        }
        if (!parameter) {
            return fail(
                w,
                "the model has no parameter %llu",
                (unsigned long long)symbol.id
            );
        }
        char *from = xstrdup(parameter->reference_name ? parameter->reference_name : "");
        bool ok = graph_set_node_reference(owner, parameter, parameter->reference_id, to) ==
                  0;
        note(l, reference_index_definition(w->project->index, symbol));
        const reference **arguments = ok ? uses_of(w, symbol) : NULL;
        for (int i = 0; ok && i < acount(arguments); i++) {
            ok = rename_call_pin(
                w,
                arguments[i],
                !strcmp(parameter->desc->name, "Model Anchor"),
                symbol.id,
                from,
                to
            );
        }
        afree(arguments);
        free(from);
        return ok;
    }
    graph *g = l ? member_graph(w, l, symbol.graph_id) : NULL;
    if (!g) {
        return false;
    }
    /* Entry outputs declare inputs; Return inputs declare outputs. */
    int output = 0, index = -1;
    node *start = graph_node_by_id(g, g->entry_id);
    node *ret = graph_node_by_id(g, g->return_id);
    const pin_spec *pin = start ? graph_pin_by_id(start, symbol.id, &output, &index)
                                : NULL;
    bool result = !pin || !output;
    pin = result && ret ? graph_pin_by_id(ret, symbol.id, &output, &index) : pin;
    if (!pin || output == result) {
        return fail(
            w,
            "%s has no parameter %llu",
            asset_name(w, symbol.asset),
            (unsigned long long)symbol.id
        );
    }
    char *from = xstrdup(pin->name);
    bool ok = graph_signature_rename(l->graphs, g, result, index, to) == 0 ||
              fail(w, "the function already has %s named %s", result ? "an output" : "an input", to);
    note(l, reference_index_definition(w->project->index, symbol));
    const reference **arguments = ok ? uses_of(w, symbol) : NULL;
    for (int i = 0; ok && i < acount(arguments); i++) {
        loaded *caller = load(w, arguments[i]->source);
        ok = caller != NULL;
        if (caller) {
            note(caller, arguments[i]);
        }
    }
    afree(arguments);
    /* graph_signature_rename follows calls in the function's own asset. */
    reference_symbol function = { .kind = REFERENCE_SYMBOL_GRAPH,
                                  .asset = symbol.asset,
                                  .id = g->graph_id };
    const reference **calls = ok ? uses_of(w, function) : NULL;
    for (int i = 0; ok && i < acount(calls); i++) {
        ok = calls[i]->source == symbol.asset ||
             rename_call_pin(w, calls[i], result, symbol.id, from, to);
    }
    afree(calls);
    free(from);
    return ok;
}

static bool rename_local(work *w, reference_symbol symbol, const char *to)
{
    loaded *l = load(w, symbol.asset);
    graph *g = l ? member_graph(w, l, symbol.graph_id) : NULL;
    if (!g) {
        return false;
    }
    if (graph_rename_local(g, symbol.id, to)) {
        return fail(w, "the graph already has a value named %s", to);
    }
    const reference **uses = uses_of(w, symbol);
    for (int i = 0; i < acount(uses); i++) {
        note(l, uses[i]);
    }
    afree(uses);
    note(l, reference_index_definition(w->project->index, symbol));
    return true;
}

static bool rename_symbol(work *w, reference_symbol symbol, const char *to)
{
    switch (symbol.kind) {
        case REFERENCE_SYMBOL_ASSET: return rename_asset(w, symbol.asset, to);
        case REFERENCE_SYMBOL_EVENT: return rename_event(w, symbol.name, to);
        case REFERENCE_SYMBOL_GRAPH: return rename_graph(w, symbol, to);
        case REFERENCE_SYMBOL_VARIABLE: return rename_variable(w, symbol, to);
        case REFERENCE_SYMBOL_PARAMETER: return rename_parameter(w, symbol, to);
        case REFERENCE_SYMBOL_LOCAL: return rename_local(w, symbol, to);
        case REFERENCE_SYMBOL_COMPONENT:
        case REFERENCE_SYMBOL_FIELD: break;
    }
    return fail(w, "components and fields are named by C reflection");
}

/* ---------- Plans ---------- */

static int edit_order(const void *a, const void *b)
{
    return ((const refactor_edit *)a)->asset - ((const refactor_edit *)b)->asset;
}

/* Keys whose values name the thing that holds them, or describe it. */
static bool identifying_key(const char *key)
{
    return !strcmp(key, "name") || !strcmp(key, "description") ||
           !strcmp(key, "comment") || !strcmp(key, "identity");
}

#define MENTION_PATH 512

static void scan(av_value *value, const char *name, char *path, size_t length, int asset, refactor_plan *plan)
{
    size_t i, max;
    if (av_is_str(value) && !strcmp(av_get_str(value), name)) {
        apush(plan->mentions, ((refactor_mention){ asset, xstrdup(path[0] ? path + 1 : path) }));
    }
    if (length > MENTION_PATH - 64) {
        return;
    }
    av_value *key, *item;
    if (av_is_obj(value)) {
        av_obj_foreach(value, i, max, key, item)
        {
            if (!identifying_key(av_get_str(key))) {
                snprintf(path + length, MENTION_PATH - length, "/%s", av_get_str(key));
                scan(item, name, path, strlen(path), asset, plan);
            }
        }
    }
    if (av_is_arr(value)) {
        av_arr_foreach(value, i, max, item)
        {
            snprintf(path + length, MENTION_PATH - length, "/%zu", i);
            scan(item, name, path, strlen(path), asset, plan);
        }
    }
    path[length] = '\0';
}

/* Values that still name an asset or event once the plan applies. */
static void scan_mentions(const refactor_project *project, const char *name, refactor_plan *plan)
{
    const char **texts = NULL;
    int count = reference_index_asset_count(project->index);
    for (int asset = 0; asset < count; asset++) {
        apush(texts, project->texts[asset]);
    }
    for (int i = 0; i < acount(plan->edits); i++) {
        texts[plan->edits[i].asset] = plan->edits[i].text;
    }
    for (int asset = 0; asset < count; asset++) {
        const char *text = texts[asset];
        for (int i = 0; i < acount(plan->edits); i++) {
            text = plan->edits[i].asset == asset ? plan->edits[i].text : text;
        }
        char why[160];
        av_doc *doc = text ? asset_text_read(text, why, sizeof why) : NULL;
        char path[MENTION_PATH] = "";
        scan(doc ? av_doc_root(doc) : NULL, name, path, 0, asset, plan);
        av_doc_free(doc);
    }
    afree(texts);
}

/* An edit per asset the plan changes; one whose text and name stay, as an
   object leaving the renamed field to its prototype, is none. */
static bool finish(work *w, refactor_plan *plan)
{
    for (int i = 0; i < acount(w->assets); i++) {
        loaded *l = w->assets[i];
        assert(l->parsed || l->deleted);
        char *text = l->deleted ? NULL : file_text(w, l);
        if (!l->deleted && !text) {
            return false;
        }
        if (text && !l->name && same_text(text, w->project->texts[l->asset])) {
            free(text);
            continue;
        }
        apush(
            plan->edits,
            ((refactor_edit){ .asset = l->asset,
                              .text = text,
                              .name = l->name,
                              .references = l->references })
        );
        l->name = NULL;
        l->references = NULL;
    }
    asort(plan->edits, edit_order);
    return true;
}

static void work_free(work *w)
{
    for (int i = 0; i < acount(w->assets); i++) {
        release(w->assets[i]);
        free(w->assets[i]->name);
        afree(w->assets[i]->references);
        free(w->assets[i]);
    }
    afree(w->assets);
}

/* The name an asset or event rename or delete leaves behind, for mentions. */
static const char *mentioned(const work *w, reference_symbol symbol)
{
    switch (symbol.kind) {
        case REFERENCE_SYMBOL_ASSET: return asset_name(w, symbol.asset);
        case REFERENCE_SYMBOL_EVENT: return symbol.name;
        default: return NULL;
    }
}

static int conclude(work *w, bool ok, refactor_plan *plan)
{
    ok = ok && finish(w, plan);
    const char *name = ok ? mentioned(w, w->symbol) : NULL;
    if (name) {
        char *old = xstrdup(name);
        scan_mentions(w->project, old, plan);
        free(old);
    }
    work_free(w);
    if (!ok) {
        refactor_plan_free(plan);
        return -1;
    }
    return 0;
}

int refactor_rename(
    const refactor_project *project,
    reference_symbol symbol,
    const char *name,
    refactor_plan *plan,
    char *error,
    size_t error_size
)
{
    *plan = (refactor_plan){ 0 };
    work w = { .project = project, .symbol = symbol, .error = error, .error_size = error_size };
    error[0] = '\0';
    return conclude(&w, valid_name(&w, name) && rename_symbol(&w, symbol, name), plan);
}

/* ---------- Delete ---------- */

void refactor_blockers(const reference_index *index, reference_symbol symbol, const reference ***uses)
{
    const reference **found = NULL;
    reference_index_find_uses(index, symbol, &found);
    for (int i = 0; i < acount(found); i++) {
        const reference *r = found[i];
        bool recursive = symbol.kind == REFERENCE_SYMBOL_GRAPH &&
                         r->source == symbol.asset && r->graph_id == symbol.id;
        if (!recursive) {
            apush(*uses, r);
        }
    }
    afree(found);
    const reference_asset
        *definition = symbol.kind == REFERENCE_SYMBOL_VARIABLE
                        ? reference_index_asset_at(index, symbol.asset)
                        : NULL;
    if (!definition || definition->kind != REFERENCE_ASSET_EVENT) {
        return;
    }
    const reference **sends = NULL;
    reference_index_find_uses(
        index,
        (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT,
                            .asset = symbol.asset,
                            .name = definition->name },
        &sends
    );
    for (int i = 0; i < acount(sends); i++) {
        if (carries_payload(sends[i])) {
            apush(*uses, sends[i]);
        }
    }
    afree(sends);
}

static bool delete_member(work *w, reference_symbol symbol)
{
    loaded *l = load(w, symbol.asset);
    if (!l) {
        return false;
    }
    note(l, reference_index_definition(w->project->index, symbol));
    if (symbol.kind == REFERENCE_SYMBOL_VARIABLE) {
        const reference **uses = uses_of(w, symbol);
        for (int i = 0; i < acount(uses); i++) {
            note(l, uses[i]);
        }
        afree(uses);
        behavior_asset_remove_variable_uses(l->graphs, symbol.id);
        return behavior_asset_delete_variable(l->graphs, symbol.id) == 0 ||
               fail(
                   w,
                   "%s has no variable %llu",
                   asset_name(w, symbol.asset),
                   (unsigned long long)symbol.id
               );
    }
    graph *g = member_graph(w, l, symbol.id);
    return g &&
           (g->kind == GRAPH_FUNCTION || g->kind == GRAPH_MACRO ||
            fail(w, "only functions and macros delete")) &&
           (behavior_asset_delete_graph(l->graphs, symbol.id) == 0 ||
            fail(w, "%s is running; stop it and retry", asset_name(w, symbol.asset)));
}

static bool delete_asset(work *w, int asset)
{
    const reference_asset *a = asset_at(w, asset);
    if (a->unresolved || a->kind == REFERENCE_ASSET_SCENE) {
        return fail(
            w,
            a->unresolved ? "%s is missing"
                          : "the project file names scenes; delete %s by hand",
            a->name
        );
    }
    if (!w->project->texts[asset]) {
        return fail(w, "%s is not saved", a->name);
    }
    entry(w, asset)->deleted = true;
    return true;
}

static bool delete_symbol(work *w, reference_symbol symbol)
{
    switch (symbol.kind) {
        case REFERENCE_SYMBOL_VARIABLE:
        case REFERENCE_SYMBOL_GRAPH: return delete_member(w, symbol);
        case REFERENCE_SYMBOL_ASSET: return delete_asset(w, symbol.asset);
        case REFERENCE_SYMBOL_EVENT:
            symbol = event_named(w, symbol.name);
            return symbol.asset >= 0
                     ? delete_asset(w, symbol.asset)
                     : fail(
                           w,
                           "the event %s has no definition to delete",
                           symbol.name
                       );
        default: break;
    }
    return fail(w, "only variables, functions, macros, events and assets delete");
}

static bool refuse(work *w, const reference **blockers)
{
    char place[256];
    refactor_describe(w->project->index, blockers[0], place, sizeof place);
    return acount(blockers) == 1
             ? fail(w, "%s uses it", place)
             : fail(w, "%s and %d more use it", place, acount(blockers) - 1);
}

int refactor_delete(
    const refactor_project *project,
    reference_symbol symbol,
    bool force,
    refactor_plan *plan,
    char *error,
    size_t error_size
)
{
    *plan = (refactor_plan){ 0 };
    work w = { .project = project, .symbol = symbol, .error = error, .error_size = error_size };
    error[0] = '\0';
    const reference **blockers = NULL;
    refactor_blockers(project->index, symbol, &blockers);
    bool ok = !acount(blockers) || force || refuse(&w, blockers);
    ok = conclude(&w, ok && delete_symbol(&w, symbol), plan) == 0;
    /* A variable's own uses go with it. */
    for (int i = 0; ok && i < acount(blockers); i++) {
        if (symbol.kind != REFERENCE_SYMBOL_VARIABLE ||
            blockers[i]->source != symbol.asset) {
            apush(plan->dangling, blockers[i]);
        }
    }
    afree(blockers);
    return ok ? 0 : -1;
}

/* ---------- Descriptions ---------- */

void refactor_describe(const reference_index *index, const reference *r, char *out, size_t size)
{
    const char *asset = reference_index_asset_at(index, r->source)->name;
    const char *within = r->graph ? r->graph : r->object;
    snprintf(
        out,
        size,
        "%s%s%s%s%s",
        asset,
        within ? " > " : "",
        within ? within : "",
        r->component ? " > " : "",
        r->component ? r->component : ""
    );
}

void refactor_plan_free(refactor_plan *plan)
{
    for (int i = 0; i < acount(plan->edits); i++) {
        free(plan->edits[i].text);
        free(plan->edits[i].name);
        afree(plan->edits[i].references);
    }
    for (int i = 0; i < acount(plan->mentions); i++) {
        free(plan->mentions[i].path);
    }
    afree(plan->edits);
    afree(plan->mentions);
    afree(plan->dangling);
    *plan = (refactor_plan){ 0 };
}
