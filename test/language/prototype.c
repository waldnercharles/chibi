#include "assets/prototype.h"

#include "assets/scene_document.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/copy.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *label;
} nested_default;
typedef struct
{
    int count;
    nested_default nested;
} component_default;
static const type_field nested_fields[] = {
    { .name = "label", .type = PIN_STR, .size = sizeof(char *), .type_name = "const char *" },
};
static const type_desc nested_type = {
    .name = "DefaultNested",
    .size = sizeof(nested_default),
    .fields = nested_fields,
    .field_count = 1,
};
static const type_field component_fields[] = {
    { .name = "count",
      .type = PIN_I32,
      .offset = offsetof(component_default, count),
      .size = sizeof(int) },
    { .name = "nested",
      .type = PIN_STRUCT,
      .offset = offsetof(component_default, nested),
      .size = sizeof(nested_default),
      .type_name = "DefaultNested" },
};
static const type_desc component_type = {
    .name = "DefaultRow",
    .size = sizeof(component_default),
    .fields = component_fields,
    .field_count = 2,
};
static const ecs_component_desc component = {
    .id = 9001,
    .key = "test.DefaultRow",
    .name = "DefaultRow",
    .element_size = sizeof(component_default),
    .type = &component_type,
};
static const ecs_component_desc nested = {
    .id = 9002,
    .key = "test.DefaultNested",
    .name = "DefaultNested",
    .element_size = sizeof(nested_default),
    .type = &nested_type,
};
/* The engine computes Derived from DefaultRow, so it is never authored. */
static const ecs_component_desc derived = {
    .id = 9003,
    .key = "test.Derived",
    .name = "Derived",
    .element_size = sizeof(component_default),
    .type = &component_type,
    .derived_from = 9001,
};
static const ecs_component_desc *const components[] = { &component, &nested, &derived };
static prototype *document, *loaded;
static scene_document *scene, *reloaded;
static char *saved;
static av_value *before, *after, *values, *rebased;
static pin_value shared_text;
static ecs_world *text_world;
static char **owned_strings;

static void cleanup()
{
    prototype_free(document);
    prototype_free(loaded);
    document = loaded = NULL;
    av_value_free(before);
    av_value_free(after);
    av_value_free(values);
    av_value_free(rebased);
    before = after = values = rebased = NULL;
    scene_document_free(scene);
    scene_document_free(reloaded);
    scene = reloaded = NULL;
    free(saved);
    saved = NULL;
    pin_text_destroy(shared_text.text);
    shared_text = (pin_value){ 0 };
    ecs_world_free(text_world);
    text_world = NULL;
    prototype_owned_strings_free(owned_strings);
    owned_strings = NULL;
}

typedef struct
{
    const char *name, *label;
    const pin_string *text;
    int count;
} text_default;
static const type_field text_fields[] = {
    { .name = "name",
      .type = PIN_NAME,
      .offset = offsetof(text_default, name),
      .size = sizeof(char *) },
    { .name = "label",
      .type = PIN_STR,
      .offset = offsetof(text_default, label),
      .size = sizeof(char *) },
    { .name = "text",
      .type = PIN_STR,
      .offset = offsetof(text_default, text),
      .size = sizeof(pin_string *),
      .text_descriptor = true },
    { .name = "count",
      .type = PIN_I32,
      .offset = offsetof(text_default, count),
      .size = sizeof(int) },
};
static const type_desc text_type = {
    .name = "TextOwner",
    .size = sizeof(text_default),
    .fields = text_fields,
    .field_count = 4,
};
static const ecs_component_desc text_component = {
    .id = 9004,
    .key = "test.TextOwner",
    .name = "TextOwner",
    .element_size = sizeof(text_default),
    .type = &text_type,
};

/* Both cleanup paths must release managed backing, not just its descriptor.
 * Names share the ownership vector but remain ordinary native C strings. */
TEST_CASE(prototype_text_ownership_releases_backing_and_preserves_shares)
{
    char name[] = "identity", label[] = "plain", bytes[] = { 'A', 0, 'B', 0 };
    pin_string text = { .bytes = bytes, .length = 3 };
    text_default data = { name, label, &text, 7 };
    prototype_default initial = { text_component.id, &data, sizeof data };
    const ecs_component_desc *descs[] = { &text_component };
    prototype_registry registry = { .components = descs,
                                    .component_count = 1,
                                    .defaults = &initial,
                                    .default_count = 1 };
    document = prototype_new("TextOwner");
    document->registry_storage = registry;
    REQUIRE(prototype_add_graph(document, "retained.chibi") == 0);
    pin_copy_scope copies = { 0 };
    pin_copy_scope_begin(&copies);
    int added = prototype_add_component(document, &text_component);
    pin_copy_scope_end(&copies);
    REQUIRE(added == 0);
    REQUIRE(copies.counts.text_bytes == sizeof label + sizeof bytes);
    const text_default *row = document->components[0].data;
    REQUIRE(row->text->owner && row->text->length == 3);
    REQUIRE(row->text->bytes != bytes && !memcmp(row->text->bytes, bytes, sizeof bytes));
    REQUIRE(
        !pin_value_text_share(NULL, &shared_text, (pin_value){ .type = PIN_STR, .text = row->text })
    );
    REQUIRE(shared_text.text->owner == row->text->owner);
    memset(name, 'X', sizeof name - 1);
    memset(label, 'X', sizeof label - 1);
    memset(bytes, 'X', sizeof bytes);
    REQUIRE(!strcmp(row->name, "identity") && !strcmp(row->label, "plain"));
    REQUIRE(!prototype_write_field(document, "TextOwner", "text", shared_text));
    REQUIRE(prototype_write_field(document, "TextOwner", "label", shared_text) == -1);
    REQUIRE(!strcmp(row->label, "plain"));
    REQUIRE(
        !prototype_write_field(document, "TextOwner", "name", (pin_value){ .type = PIN_NAME, .str = "" })
    );
    REQUIRE(!row->name[0]);
    REQUIRE(!prototype_write_field(document, "TextOwner", "text", (pin_value){ .type = PIN_STR }));
    REQUIRE(!row->text);
    prototype_free(document);
    document = NULL;
    REQUIRE(shared_text.text->length == 3 && !memcmp(shared_text.text->bytes, "A\0B", 4));

    /* Fail only after all three string fields allocated their owned copies. */
    type_field malformed_fields[4];
    memcpy(malformed_fields, text_fields, sizeof malformed_fields);
    malformed_fields[3].offset = sizeof data;
    type_desc malformed_type = text_type;
    malformed_type.fields = malformed_fields;
    ecs_component_desc malformed = text_component;
    malformed.type = &malformed_type;
    data.text = shared_text.text;
    document = prototype_new("Rollback");
    document->registry_storage = registry;
    REQUIRE(!prototype_add_graph(document, "preserved.chibi"));
    int strings = acount(document->strings);
    REQUIRE(prototype_add_component(document, &malformed) == -1);
    REQUIRE(!acount(document->components) && acount(document->strings) == strings);
    REQUIRE(prototype_has_graph(document, "preserved.chibi"));
    return true;
}

TEST_CASE(prototype_override_text_ownership_transfers_on_success_and_error)
{
    const ecs_component_desc *descs[] = { &text_component };
    prototype_registry registry = { .components = descs, .component_count = 1 };
    text_world = ecs_world_new();
    REQUIRE(ecs_world_register_component(text_world, &text_component, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(text_world);
    char error[192];
    for (int failure = 0; failure < 2; failure++) {
        av_doc *input = av_doc_new();
        av_value *root = av_obj(input);
        av_doc_set_root(input, root);
        av_value *fields = av_obj_add_obj(input, root, "TextOwner");
        av_obj_add_str(input, fields, "name", "name");
        av_obj_add_str(input, fields, "label", "label");
        av_obj_add_str(input, fields, "text", "text");
        if (failure) {
            av_obj_add_int(input, fields, "unknown", 1);
        }
        int result = prototype_apply_components_value(
            text_world,
            entity,
            root,
            &registry,
            &owned_strings,
            error,
            sizeof error
        );
        av_doc_free(input);
        REQUIRE(result == (failure ? -1 : 0));
        const text_default
            *row = ecs_component_data_const(text_world, entity, text_component.id);
        REQUIRE(row && !strcmp(row->name, "name") && !strcmp(row->label, "label"));
        REQUIRE(row->text->length == 4 && !memcmp(row->text->bytes, "text", 5));
        REQUIRE(acount(owned_strings) == 3 * (failure + 1));
    }
    return true;
}

TEST_CASE(prototype_defaults_are_owned_and_detached_values_take_precedence)
{
    char label[] = "initial";
    component_default data = { 9, { label } };
    prototype_default initial = { component.id, &data, sizeof data };
    prototype_registry registry = {
        .components = components,
        .component_count = 2,
        .defaults = &initial,
        .default_count = 1,
    };
    document = prototype_new("DefaultOwner");
    document->registry_storage = registry;
    REQUIRE(prototype_add_component(document, &component) == 0);
    label[0] = 'X';
    data.count = 41;
    const component_default *copy = document->components[0].data;
    REQUIRE(copy->count == 9 && !strcmp(copy->nested.label, "initial"));
    REQUIRE(prototype_detach_component(document, component.name) == 0);
    REQUIRE(prototype_add_component(document, &component) == 0);
    copy = document->components[0].data;
    REQUIRE(copy->count == 9 && !strcmp(copy->nested.label, "initial"));

    data = (component_default){ 9, { "baseline" } };
    before = prototype_component_defaults_value(&registry, &component);
    values = prototype_component_value(document, component.name);
    REQUIRE(before && values);
    data = (component_default){ 12, { "updated" } };
    after = prototype_component_defaults_value(&registry, &component);
    REQUIRE(after != NULL);
    rebased = prototype_rebase_component_value(values, before, after);
    REQUIRE(rebased != NULL);
    av_value_free(values);
    values = NULL;
    av_value_free(before);
    before = NULL;
    av_value_free(after);
    after = NULL;
    const av_value *row = av_obj_get(rebased, component.key);
    REQUIRE(av_get_int(av_obj_get(row, "count")) == 12);
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_obj_get(row, "nested"), "label")), "initial"));

    char error[192];
    loaded = prototype_load(
        "name \"Partial\"\ncomponents { test.DefaultRow { nested "
        "label=\"document\"; }; }\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(loaded != NULL);
    copy = loaded->components[0].data;
    REQUIRE(copy->count == 0 && !strcmp(copy->nested.label, "document"));
    REQUIRE(prototype_remove_component(loaded, component.name) == 0);
    initial.size--;
    REQUIRE(prototype_add_component(loaded, &component) == -1);
    REQUIRE(acount(loaded->components) == 0);
    initial.size++;
    type_desc malformed = component_type;
    ecs_component_desc invalid = component;
    invalid.type = &malformed;
    malformed.size++;
    REQUIRE(prototype_add_component(loaded, &invalid) == -1);
    REQUIRE(acount(loaded->components) == 0);
    malformed.size = component_type.size;
    type_field fields[2];
    memcpy(fields, component_fields, sizeof fields);
    malformed.fields = fields;
    fields[0].offset = malformed.size;
    REQUIRE(prototype_add_component(loaded, &invalid) == -1);
    REQUIRE(acount(loaded->components) == 0);
    fields[0] = component_fields[0];
    fields[0].size = 1;
    REQUIRE(prototype_add_component(loaded, &invalid) == -1);
    REQUIRE(acount(loaded->components) == 0);
    /* Public documents must reject a live pointer-shaped field rather than
     * silently omitting it or replacing its address with null. */
    memcpy(fields, component_fields, sizeof fields);
    fields[1].type = PIN_POINTER;
    invalid.type = &malformed;
    document->components[0].desc = &invalid;
    bool rejected = !prototype_to_value(document) && !prototype_save(document) &&
                    !prototype_components_value(document) &&
                    !prototype_component_value(document, component.name);
    document->components[0].desc = &component;
    REQUIRE(rejected);
    return true;
}

static bool keeps_prototype_behaviors(const scene_document *document)
{
    const scene_entity *entities = document->entities;
    return acount(entities) == 3 && !entities[0].own_behaviors &&
           !acount(entities[0].behaviors) && entities[1].own_behaviors &&
           acount(entities[1].behaviors) == 1 &&
           !strcmp(entities[1].behaviors[0], "Steer") &&
           entities[2].own_behaviors && !acount(entities[2].behaviors);
}

TEST_CASE(derived_components_are_never_authored_or_captured)
{
    const prototype_registry registry = { .components = components,
                                          .component_count = 3 };
    char error[256];
    loaded = prototype_load(
        "name \"Computed\"\ncomponents {\n    Derived count=1\n}\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(!loaded && strstr(error, "Derived is computed by the engine"));
    document = prototype_new("Computed");
    document->registry_storage = registry;
    REQUIRE(prototype_add_component(document, &derived) != 0);
    prototype_free(document);
    document = NULL;
    ecs_world *world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &derived, NULL) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(world);
    REQUIRE(ecs_world_record_add(world, entity, component.id, NULL, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(ecs_component_has(world, entity, derived.id));
    document = prototype_capture_entity("Captured", world, entity, &registry);
    bool captured = document && acount(document->components) == 1 &&
                    document->components[0].desc == &component;
    ecs_world_free(world);
    REQUIRE(captured);
    return true;
}

TEST_CASE(scene_entities_keep_prototype_behaviors_until_they_list_their_own)
{
    char error[192];
    scene = scene_document_load(
        "version 1\nname \"Start\"\n(array)entities {\n"
        "    - name=\"Inherits\" prototype=\"Mover\"\n"
        "    - name=\"Replaces\" prototype=\"Mover\" { (array)behaviors "
        "\"Steer\"; }\n"
        "    - name=\"Opts out\" prototype=\"Mover\" { (array)behaviors; }\n"
        "}\n",
        error,
        sizeof error
    );
    REQUIRE(scene && keeps_prototype_behaviors(scene));
    saved = scene_document_save(scene, (ecs_component_catalog){ 0 });
    REQUIRE(saved);
    reloaded = scene_document_load(saved, error, sizeof error);
    REQUIRE(reloaded && keeps_prototype_behaviors(reloaded));
    REQUIRE(scene_document_add_entity(reloaded, -1, "Added", "Mover") == 0);
    REQUIRE(!alast(reloaded->entities).own_behaviors);
    return true;
}

static bool at_path(const scene_document *document, const char *path, const char *prototype_name)
{
    int index = scene_document_entity_index(document, path);
    if (index < 0 || strcmp(document->entities[index].prototype, prototype_name)) {
        return false;
    }
    char *named = scene_document_entity_path(document, index);
    bool same = !strcmp(named, path);
    free(named);
    return same;
}

/* Children nest under their parent, are named by path, and save back nested
 * in the same order. */
TEST_CASE(scene_children_nest_by_path)
{
    char error[192];
    const char
        *text = "version 1\nname \"Fleet\"\n(array)entities {\n"
                "    - name=\"Ship\" prototype=\"Hull\" { (array)children {\n"
                "        - name=\"Turret\" prototype=\"Gun\" { (array)children "
                "{\n"
                "            - name=\"Barrel\" prototype=\"Tube\"; }; }\n"
                "        - name=\"Light\" prototype=\"Lamp\"; }; }\n"
                "    - name=\"Turret\" prototype=\"Gun\"\n"
                "}\n";
    scene = scene_document_load(text, error, sizeof error);
    REQUIRE(scene && acount(scene->entities) == 5);
    REQUIRE(at_path(scene, "Ship", "Hull") && at_path(scene, "Ship.Turret.Barrel", "Tube"));
    REQUIRE(at_path(scene, "Ship.Light", "Lamp") && at_path(scene, "Turret", "Gun"));
    REQUIRE(scene->entities[scene_document_entity_index(scene, "Turret")].parent == -1);
    REQUIRE(scene_document_entity_index(scene, "Barrel") < 0);
    REQUIRE(scene_document_entity_index(scene, "Ship..Light") < 0);
    /* Names are unique among siblings and cannot hold the separator. */
    int ship = scene_document_entity_index(scene, "Ship");
    REQUIRE(scene_document_add_entity(scene, ship, "Light", "Lamp") != 0);
    REQUIRE(scene_document_add_entity(scene, ship, "Port.Light", "Lamp") != 0);
    REQUIRE(scene_document_rename_entity(scene, ship + 1, "Light", error, sizeof error) != 0);
    REQUIRE(scene_document_add_entity(scene, ship, "Shield", "Dome") == 0);
    saved = scene_document_save(scene, (ecs_component_catalog){ 0 });
    reloaded = scene_document_load(saved, error, sizeof error);
    REQUIRE(reloaded && acount(reloaded->entities) == 6);
    /* The appended child saves among its siblings, ahead of the later root. */
    REQUIRE(scene_document_entity_index(reloaded, "Ship.Shield") == 4);
    /* Removing a parent removes its descendants and keeps the rest in place. */
    REQUIRE(scene_document_remove_entity(reloaded, 1) == 0);
    REQUIRE(acount(reloaded->entities) == 4);
    REQUIRE(at_path(reloaded, "Ship.Light", "Lamp") && at_path(reloaded, "Ship.Shield", "Dome"));
    REQUIRE(at_path(reloaded, "Turret", "Gun"));
    return true;
}

/* A chain of one object per level, `depth` levels deep. */
static char *nested_scene(int depth)
{
    static const char
        header[] = "version 1\nname \"Deep\"\n(array)entities {\n",
        open[] = "- name=\"N\" prototype=\"P\" { (array)children {\n",
        leaf[] = "- name=\"N\" prototype=\"P\"\n", close[] = "}\n}\n";
    size_t size = sizeof header + (size_t)depth * (sizeof open + sizeof close) +
                  sizeof leaf + 2;
    char *text = xmalloc(size);
    strcpy(text, header);
    for (int i = 1; i < depth; i++) {
        strcat(text, open);
    }
    strcat(text, leaf);
    for (int i = 1; i < depth; i++) {
        strcat(text, close);
    }
    strcat(text, "}\n");
    return text;
}

TEST_CASE(scene_children_nest_at_most_the_depth_limit)
{
    char error[192];
    char *deepest = nested_scene(SCENE_DEPTH_MAX),
         *deeper = nested_scene(SCENE_DEPTH_MAX + 1);
    scene = scene_document_load(deepest, error, sizeof error);
    reloaded = scene_document_load(deeper, error, sizeof error);
    free(deepest);
    free(deeper);
    REQUIRE(scene && acount(scene->entities) == SCENE_DEPTH_MAX);
    REQUIRE(!reloaded && strstr(error, "nests deeper than 256 levels"));
    REQUIRE(scene_document_add_entity(scene, SCENE_DEPTH_MAX - 1, "M", "P") != 0);
    REQUIRE(scene_document_add_entity(scene, SCENE_DEPTH_MAX - 2, "M", "P") == 0);
    char *path = scene_document_entity_path(scene, SCENE_DEPTH_MAX - 1);
    bool long_path = strlen(path) == 2 * SCENE_DEPTH_MAX - 1;
    free(path);
    REQUIRE(long_path);
    saved = scene_document_save(scene, (ecs_component_catalog){ 0 });
    return saved != NULL;
}

static const char ship_text
    [] = "name \"Ship\"\n"
         "components {\n    test.DefaultRow count=5\n}\n"
         "(array)behaviors \"Fly\"\n"
         "(array)children {\n"
         "    - name=\"Left\" prototype=\"Turret\" {\n"
         "        components {\n            test.DefaultRow count=2\n        "
         "}\n"
         "        (array)children {\n"
         "            - name=\"Muzzle\" prototype=\"Barrel\"\n"
         "        }\n"
         "    }\n"
         "    - name=\"Right\" prototype=\"Turret\" { (array)behaviors; }\n"
         "}\n";
static const char *const library[][2] = {
    { "Ship", ship_text },
    { "Turret", "name \"Turret\"\ncomponents {\n    test.DefaultRow count=1\n}\n(array)behaviors \"Aim\"\n" },
    { "Barrel", "name \"Barrel\"\ncomponents {\n    test.DefaultNested label=\"steel\"\n}\n" },
    { "Loop", "name \"Loop\"\n(array)children {\n    - name=\"Again\" prototype=\"Loop\"\n}\n" },
    { "Lost", "name \"Lost\"\n(array)children {\n    - name=\"Gone\" prototype=\"Nope\"\n}\n" },
    { "Escort",
      "name \"Escort\"\n(array)includes \"Ship\"\n"
      "(array)children {\n    - name=\"Rear\" prototype=\"Turret\"\n}\n" },
    { "Clash",
      "name \"Clash\"\n(array)includes \"Ship\"\n"
      "(array)children {\n    - name=\"Left\" prototype=\"Barrel\"\n}\n" },
};
static const char *find_prototype(const char *name, void *)
{
    for (size_t i = 0; i < sizeof library / sizeof library[0]; i++) {
        if (!strcmp(library[i][0], name)) {
            return library[i][1];
        }
    }
    return NULL;
}
static prototype *load_named(const char *name, char *error, size_t size)
{
    const prototype_registry registry = { .components = components,
                                          .component_count = 2,
                                          .include = find_prototype };
    return prototype_load(find_prototype(name, NULL), &registry, error, size);
}
static int count_of(const prototype *document)
{
    return ((const component_default *)document->components[0].data)->count;
}
static struct
{
    ecs_entity entities[4];
    const char *graphs[4];
    int count;
    const char *refuse;
} attached;
static int attach(ecs_world *, ecs_entity entity, const char *graph, void *)
{
    if (attached.refuse && !strcmp(graph, attached.refuse)) {
        return -1;
    }
    assert(attached.count < 4);
    attached.entities[attached.count] = entity;
    attached.graphs[attached.count++] = graph;
    return 0;
}

/* A Link names another entity by path; its inner handle is nested, so it
 * cannot. */
typedef struct
{
    uint64_t entity;
} link_inner;
typedef struct
{
    uint64_t other;
    link_inner inner;
} link_row;
static const type_field inner_fields[] = {
    { .name = "entity", .type = PIN_HANDLE, .size = sizeof(uint64_t), .type_name = "ecs.entity" },
};
static const type_desc inner_type = {
    .name = "LinkInner",
    .size = sizeof(link_inner),
    .fields = inner_fields,
    .field_count = 1,
};
static const type_field link_fields[] = {
    { .name = "other",
      .type = PIN_HANDLE,
      .offset = offsetof(link_row, other),
      .size = sizeof(uint64_t),
      .type_name = "ecs.entity" },
    { .name = "inner",
      .type = PIN_STRUCT,
      .offset = offsetof(link_row, inner),
      .size = sizeof(link_inner),
      .type_name = "LinkInner" },
};
static const type_desc link_type = {
    .name = "LinkRow",
    .size = sizeof(link_row),
    .fields = link_fields,
    .field_count = 2,
};
static const ecs_component_desc link = {
    .id = 9004,
    .key = "test.Link",
    .name = "Link",
    .element_size = sizeof(link_row),
    .type = &link_type,
};
static const ecs_component_desc inner = {
    .id = 9005,
    .key = "test.LinkInner",
    .name = "LinkInner",
    .element_size = sizeof(link_inner),
    .type = &inner_type,
};
static const ecs_component_desc *const linked[] = { &link, &inner };
/* Pendulum's root names its child Bob, and its document gives Bob a path to
 * Hook in place of the one Weight gives it, to its own Inside. */
static const char *const links_library[][2] = {
    { "Pendulum",
      "name \"Pendulum\"\ncomponents {\n    test.Link other=\"Bob\"\n}\n"
      "(array)children {\n"
      "    - name=\"Hook\" prototype=\"Weight\"\n"
      "    - name=\"Bob\" prototype=\"Weight\" {\n"
      "        components {\n            test.Link other=\"Hook\"\n        }\n"
      "    }\n"
      "}\n" },
    { "Weight",
      "name \"Weight\"\ncomponents {\n    test.Link other=\"Inside\"\n}\n"
      "(array)children {\n    - name=\"Inside\" prototype=\"Plain\"\n}\n" },
    { "Plain", "name \"Plain\"\n" },
    /* Bob's path names Lure's Inside, not the one Weight's names. */
    { "Lure",
      "name \"Lure\"\n(array)children {\n"
      "    - name=\"Inside\" prototype=\"Plain\"\n"
      "    - name=\"Bob\" prototype=\"Weight\" {\n"
      "        components {\n            test.Link other=\"Inside\"\n        "
      "}\n"
      "    }\n"
      "}\n" },
    /* Parked's Link is detached, so its path doesn't reach Weight's. */
    { "Parked", "name \"Parked\"\ndetached_components {\n    test.Link other=\"Nowhere\"\n}\n" },
    { "Composite", "name \"Composite\"\n(array)includes \"Weight\" \"Parked\"\n" },
    { "Stray", "name \"Stray\"\ncomponents {\n    test.Link other=\"Nowhere\"\n}\n" },
    { "Deep", "name \"Deep\"\ncomponents {\n    test.Link {\n        inner entity=\"Plain\"\n    }\n}\n" },
};
static const char *find_linked(const char *name, void *)
{
    for (size_t i = 0; i < sizeof links_library / sizeof links_library[0]; i++) {
        if (!strcmp(links_library[i][0], name)) {
            return links_library[i][1];
        }
    }
    return NULL;
}
static const prototype_registry links_registry = { .components = linked,
                                                   .component_count = 2,
                                                   .include = find_linked };
static prototype *load_linked(const char *name, char *error, size_t size)
{
    return prototype_load(find_linked(name, NULL), &links_registry, error, size);
}
static const prototype_link *only_link(const prototype *document)
{
    return acount(document->links) == 1 ? &document->links[0] : NULL;
}
static uint64_t other_of(ecs_world *world, ecs_entity entity)
{
    const link_row *row = ecs_component_data_const(world, entity, link.id);
    return row ? row->other : 0;
}

/* A handle field may name an entity by path, from the roots of the document
 * that wrote it, and saves, clones and overrides as that path. */
TEST_CASE(entity_paths_load_and_save_as_written)
{
    char error[256];
    document = load_linked("Pendulum", error, sizeof error);
    REQUIRE(document && acount(document->child_prototypes) == 2);
    const prototype_link *own = only_link(document);
    REQUIRE(own && !own->outer && own->field == 0 && !strcmp(own->path, "Bob"));
    REQUIRE(!((const link_row *)document->components[0].data)->other);
    const prototype_link *hook = only_link(document->child_prototypes[0]);
    REQUIRE(hook && !hook->outer && !strcmp(hook->path, "Inside"));
    const prototype_link *bob = only_link(document->child_prototypes[1]);
    REQUIRE(bob && bob->outer && !strcmp(bob->path, "Hook"));
    saved = prototype_save(document);
    REQUIRE(saved && strstr(saved, "test.Link other=\"Bob\"") && strstr(saved, "other=\"Hook\""));
    loaded = prototype_clone(document->child_prototypes[1], error, sizeof error);
    REQUIRE(loaded && only_link(loaded) && only_link(loaded)->outer);
    /* Bob's path differs from Weight's; Hook's is Weight's own. */
    prototype *weight = load_linked("Weight", error, sizeof error);
    REQUIRE(weight);
    before = prototype_overrides_value(weight, document->child_prototypes[1]);
    after = prototype_overrides_value(weight, document->child_prototypes[0]);
    av_value *fields = before ? av_obj_get(before, "test.Link") : NULL;
    REQUIRE(fields && !strcmp(av_get_str(av_obj_get(fields, "other")), "Hook"));
    REQUIRE(after && !av_obj_size(after));
    av_value_free(before);
    prototype *lure = load_linked("Lure", error, sizeof error);
    REQUIRE(lure && acount(lure->child_prototypes) == 2);
    before = prototype_overrides_value(weight, lure->child_prototypes[1]);
    fields = before ? av_obj_get(before, "test.Link") : NULL;
    REQUIRE(fields && !strcmp(av_get_str(av_obj_get(fields, "other")), "Inside"));
    prototype_free(lure);
    prototype *composite = load_linked("Composite", error, sizeof error);
    REQUIRE(composite && only_link(composite) && !strcmp(only_link(composite)->path, "Inside"));
    prototype_free(composite);
    /* Writing the handle or removing the component drops the path. */
    REQUIRE(!prototype_write_field(weight, "Link", "other", (pin_value){ .type = PIN_HANDLE }));
    REQUIRE(!acount(weight->links));
    av_value_free(after);
    /* A current naming no path, as a captured entity, keeps the base's. */
    after = prototype_overrides_value(document->child_prototypes[0], weight);
    REQUIRE(after && !av_obj_get(after, "test.Link"));
    REQUIRE(!prototype_remove_component(document, "Link") && !acount(document->links));
    prototype_free(weight);
    prototype_free(loaded);
    loaded = load_linked("Deep", error, sizeof error);
    REQUIRE(!loaded && strstr(error, "field entity has the wrong value type"));
    return true;
}

/* Spawning writes each path's entity, in the tree of the document that
 * named it; a path naming no entity stops the spawn. */
TEST_CASE(entity_paths_spawn_as_their_entities)
{
    char error[256];
    document = load_linked("Pendulum", error, sizeof error);
    REQUIRE(document);
    ecs_world *world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &link, NULL) == ECS_OK);
    ecs_entity root = { 0 };
    REQUIRE(prototype_spawn_components(document, world, &root, error, sizeof error) == ECS_OK);
    ecs_entity bob = ecs_entity_from_u64(other_of(world, root));
    ecs_entity hook = ecs_entity_from_u64(other_of(world, bob));
    ecs_entity inside = ecs_entity_from_u64(other_of(world, hook));
    bool tree = ecs_world_live_count(world) == 5 &&
                ecs_parent(world, bob).slot == root.slot &&
                ecs_parent(world, hook).slot == root.slot && bob.slot != hook.slot &&
                ecs_parent(world, inside).slot == hook.slot;
    prototype *stray = load_linked("Stray", error, sizeof error);
    bool refused = stray &&
                   prototype_spawn_components(stray, world, &root, error, sizeof error) !=
                       ECS_OK &&
                   strstr(error, "names Nowhere, which is not a child of prototype Stray") &&
                   ecs_world_live_count(world) == 5;
    prototype_free(stray);
    ecs_world_free(world);
    REQUIRE(tree && refused);
    return true;
}

/* A prototype's children load as their own prototypes, with the child's
 * components and behaviors, and save back as written. */
TEST_CASE(prototype_children_resolve_and_save_nested)
{
    char error[256];
    document = load_named("Ship", error, sizeof error);
    REQUIRE(document && acount(document->children->entities) == 3);
    REQUIRE(acount(document->child_prototypes) == 3);
    const prototype *left = document->child_prototypes[0];
    const prototype *muzzle = document->child_prototypes[1];
    const prototype *right = document->child_prototypes[2];
    REQUIRE(!strcmp(left->name, "Left") && count_of(left) == 2);
    REQUIRE(acount(left->graphs) == 1 && !strcmp(left->graphs[0], "Aim"));
    REQUIRE(!strcmp(muzzle->name, "Muzzle") && document->children->entities[1].parent == 0);
    REQUIRE(
        !strcmp(right->name, "Right") && count_of(right) == 1 && !acount(right->graphs)
    );
    REQUIRE(!strcmp(prototype_identity(left), prototype_identity(right)));
    saved = prototype_save(document);
    REQUIRE(saved);
    const prototype_registry registry = { .components = components,
                                          .component_count = 2,
                                          .include = find_prototype };
    loaded = prototype_load(saved, &registry, error, sizeof error);
    REQUIRE(loaded && acount(loaded->child_prototypes) == 3);
    REQUIRE(scene_document_entity_index(loaded->children, "Left.Muzzle") == 1);
    prototype_free(loaded);
    loaded = prototype_load_authored(ship_text, &registry, error, sizeof error);
    REQUIRE(loaded && acount(loaded->children->entities) == 3 && !acount(loaded->child_prototypes));
    prototype_free(loaded);
    const prototype_registry unresolved = { .components = components,
                                            .component_count = 2 };
    loaded = prototype_load(ship_text, &unresolved, error, sizeof error);
    REQUIRE(!loaded && strstr(error, "children need a resolver"));
    /* An include's children come first, and a name stays unique among them. */
    loaded = load_named("Escort", error, sizeof error);
    REQUIRE(loaded && acount(loaded->child_prototypes) == 4);
    REQUIRE(!strcmp(loaded->child_prototypes[3]->name, "Rear"));
    prototype_free(loaded);
    loaded = load_named("Clash", error, sizeof error);
    REQUIRE(!loaded && strstr(error, "children of prototype Clash"));
    loaded = load_named("Loop", error, sizeof error);
    REQUIRE(!loaded && strstr(error, "cycle at Loop"));
    loaded = load_named("Lost", error, sizeof error);
    REQUIRE(!loaded && !strcmp(error, "child prototype Nope is unavailable"));
    return true;
}

/* Spawning makes every entity of the tree before any behavior attaches, and a
 * failure leaves none. */
TEST_CASE(prototype_children_spawn_with_it)
{
    char error[256];
    document = load_named("Ship", error, sizeof error);
    REQUIRE(document);
    ecs_world *world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &nested, NULL) == ECS_OK);
    attached = (typeof(attached)){ 0 };
    prototype_spawn_hooks hooks = { attach, NULL };
    ecs_entity ship = { 0 };
    int spawned = prototype_spawn(document, world, &hooks, &ship, error, sizeof error);
    ecs_entity left = attached.entities[1];
    const component_default
        *row = ecs_component_data_const(world, left, component.id);
    bool tree = spawned == ECS_OK && ecs_world_live_count(world) == 4 &&
                attached.count == 2 && !strcmp(attached.graphs[0], "Fly") &&
                attached.entities[0].slot == ship.slot &&
                !strcmp(attached.graphs[1], "Aim") &&
                ecs_parent(world, left).slot == ship.slot && row && row->count == 2 &&
                ecs_pair_source_count(world, ECS_CHILD_OF, ship) == 2 &&
                ecs_pair_source_count(world, ECS_CHILD_OF, left) == 1;
    bool destroyed = ecs_world_destroy_entity(world, ship) == ECS_OK &&
                     ecs_world_live_count(world) == 0;
    attached = (typeof(attached)){ .refuse = "Aim" };
    bool refused = prototype_spawn(document, world, &hooks, &ship, error, sizeof error) !=
                       ECS_OK &&
                   ecs_world_live_count(world) == 0;
    bool components_only = prototype_spawn_components(document, world, &ship, error, sizeof error) ==
                               ECS_OK &&
                           ecs_world_live_count(world) == 4;
    ecs_world_free(world);
    /* Muzzle's component is missing here, so the spawn stops at it. */
    world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    bool unplaced = prototype_spawn_components(document, world, &ship, error, sizeof error) !=
                        ECS_OK &&
                    ecs_world_live_count(world) == 0;
    ecs_world_free(world);
    /* Without an attach hook the spawn is refused before it touches the world. */
    world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &nested, NULL) == ECS_OK);
    bool unhooked = prototype_spawn(document, world, NULL, &ship, error, sizeof error) ==
                        ECS_ERR_ARGUMENT &&
                    !ecs_world_component(world, prototype_tag_desc(document)->id);
    ecs_world_free(world);
    REQUIRE(tree && destroyed && refused && components_only && unplaced && unhooked);
    return true;
}

/* FanN has two children using FanN+1, down to a childless Fan20, so Fan0's
 * load would read two million documents. */
static const char *find_fan(const char *name, void *)
{
    static char texts[21][160];
    int level = atoi(name + 3);
    assert(!strncmp(name, "Fan", 3) && level >= 0 && level <= 20);
    if (level == 20) {
        return "name \"Fan20\"\n";
    }
    snprintf(
        texts[level],
        sizeof texts[level],
        "name \"Fan%d\"\n(array)children {\n    - name=\"A\" "
        "prototype=\"Fan%d\"\n"
        "    - name=\"B\" prototype=\"Fan%d\"\n}\n",
        level,
        level + 1,
        level + 1
    );
    return texts[level];
}

/* A load stops once includes and children have read too many documents. */
TEST_CASE(prototype_expansion_is_bounded)
{
    char error[256];
    const prototype_registry registry = { .components = components,
                                          .component_count = 2,
                                          .include = find_fan };
    document = prototype_load(find_fan("Fan10", NULL), &registry, error, sizeof error);
    REQUIRE(document && document->expanded == 2046);
    loaded = prototype_load(find_fan("Fan0", NULL), &registry, error, sizeof error);
    REQUIRE(!loaded && strstr(error, "reads more than 16384 prototypes"));
    return true;
}

/* WideN has two children using WideN+1, down to a 1 MiB Wide6, so Wide0's
 * load would read 64 copies of it. */
static char *wide_leaf;
static const char *find_wide(const char *name, void *)
{
    static char texts[6][160];
    char *leaf = wide_leaf;
    int level = atoi(name + 4);
    assert(!strncmp(name, "Wide", 4) && level >= 0 && level <= 6);
    if (level == 6 && !leaf) {
        size_t size = 1024 * 1024;
        leaf = wide_leaf = xmalloc(size + 1);
        int n = snprintf(leaf, size, "name \"Wide6\"\n/* ");
        memset(leaf + n, 'x', size - (size_t)n - 3);
        memcpy(leaf + size - 3, "*/\n", 4);
    }
    if (level == 6) {
        return leaf;
    }
    snprintf(
        texts[level],
        sizeof texts[level],
        "name \"Wide%d\"\n(array)children {\n    - name=\"A\" "
        "prototype=\"Wide%d\"\n"
        "    - name=\"B\" prototype=\"Wide%d\"\n}\n",
        level,
        level + 1,
        level + 1
    );
    return texts[level];
}

/* So does one whose documents add up to too many bytes. */
TEST_CASE(prototype_expansion_bytes_are_bounded)
{
    char error[256];
    const prototype_registry registry = { .components = components,
                                          .component_count = 2,
                                          .include = find_wide };
    document = prototype_load(find_wide("Wide1", NULL), &registry, error, sizeof error);
    REQUIRE(document && document->read > 32 * 1024 * 1024);
    loaded = prototype_load(find_wide("Wide0", NULL), &registry, error, sizeof error);
    REQUIRE(!loaded && strstr(error, "reads more than 64 MiB of prototypes"));
    free(wide_leaf);
    wide_leaf = NULL;
    return true;
}

typedef struct
{
    double amount, positive, hint;
    int count;
} ranged_values;
static const type_field ranged_fields[] = {
    { .name = "amount",
      .type = PIN_F64,
      .size = sizeof(double),
      .range_contract = true,
      .min = 0,
      .max = 10,
      .range_default = 3.5 },
    { .name = "positive",
      .type = PIN_F64,
      .size = sizeof(double),
      .offset = offsetof(ranged_values, positive),
      .range_contract = true,
      .min = 2,
      .max = 5 },
    { .name = "hint",
      .type = PIN_F64,
      .size = sizeof(double),
      .offset = offsetof(ranged_values, hint),
      .min = 0,
      .max = 1 },
    { .name = "count",
      .type = PIN_I32,
      .size = sizeof(int),
      .offset = offsetof(ranged_values, count),
      .range_contract = true,
      .min = 0,
      .max = 10 },
};
static const type_desc ranged_type = {
    .name = "Ranged",
    .size = sizeof(ranged_values),
    .fields = ranged_fields,
    .field_count = 4,
};
static const ecs_component_desc ranged_desc = {
    .id = 9004,
    .key = "Ranged",
    .name = "Ranged",
    .element_size = sizeof(ranged_values),
    .type = &ranged_type,
};
TEST_CASE(prototype_ranges_validate_load_defaults_reload_and_nonfinite_values)
{
    const ecs_component_desc *descs[] = { &ranged_desc };
    prototype_registry registry = { .components = descs, .component_count = 1 };
    char error[256];
    document = prototype_load(
        "name \"Ranges\"\ncomponents { Ranged amount=-4 hint=20; }\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(document && acount(document->components) == 1);
    const ranged_values *row = document->components[0].data;
    REQUIRE(row->amount == 0 && row->positive == 2 && row->hint == 20);
    REQUIRE(
        !prototype_write_field(document, "Ranged", "amount", (pin_value){ .type = PIN_F64, .d = 2.75 })
    );
    REQUIRE(row->amount == 2.75);
    loaded = prototype_load(
        "name \"Ranges\"\ncomponents { Ranged amount=200 positive=-30 "
        "count=18446744073709551615; }\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(loaded);
    const ranged_values *next = loaded->components[0].data;
    REQUIRE(next->amount == 10 && next->positive == 2 && next->count == 10);
    prototype_free(loaded);
    loaded = NULL;
    /* The typed asset-value entry point and prepared streams can receive
     * nonfinite numeric payloads even though KDL text rejects #nan/#inf. */
    av_doc *input = av_doc_new();
    av_value *root = av_obj(input);
    av_doc_set_root(input, root);
    av_obj_add_str(input, root, "name", "Ranges");
    av_value *components_value = av_obj_add_obj(input, root, "components");
    av_value *fields = av_obj_add_obj(input, components_value, "Ranged");
    av_obj_add_val(input, fields, "amount", av_real(input, NAN));
    av_obj_add_val(input, fields, "positive", av_real(input, INFINITY));
    loaded = prototype_load_value(root, &registry, error, sizeof error);
    av_doc_free(input);
    REQUIRE(loaded);
    next = loaded->components[0].data;
    REQUIRE(next->amount == 3.5 && next->positive == 5);
    return true;
}

void test_prototype_document()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prototype_text_ownership_releases_backing_and_preserves_shares);
    RUN_TEST_CASE(prototype_override_text_ownership_transfers_on_success_and_error);
    RUN_TEST_CASE(prototype_defaults_are_owned_and_detached_values_take_precedence);
    RUN_TEST_CASE(scene_entities_keep_prototype_behaviors_until_they_list_their_own);
    RUN_TEST_CASE(scene_children_nest_by_path);
    RUN_TEST_CASE(scene_children_nest_at_most_the_depth_limit);
    RUN_TEST_CASE(derived_components_are_never_authored_or_captured);
    RUN_TEST_CASE(prototype_children_resolve_and_save_nested);
    RUN_TEST_CASE(prototype_children_spawn_with_it);
    RUN_TEST_CASE(entity_paths_load_and_save_as_written);
    RUN_TEST_CASE(entity_paths_spawn_as_their_entities);
    RUN_TEST_CASE(prototype_expansion_is_bounded);
    RUN_TEST_CASE(prototype_expansion_bytes_are_bounded);
    RUN_TEST_CASE(prototype_ranges_validate_load_defaults_reload_and_nonfinite_values);
    pu_clear_setup();
}
