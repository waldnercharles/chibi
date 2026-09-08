#include "editor/documents/scene_retention.h"

#include "../third_party/yyjson/yyjson.h"
#include "assets/prototype.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

scene_retained_component *scene_retention_find(scene_retained_component *components, const char *name)
{
    for (int i = 0; i < acount(components); i++) {
        if (!strcmp(components[i].name, name)) {
            return &components[i];
        }
    }
    return NULL;
}

static void free_component(scene_retained_component *component)
{
    free(component->name);
    free(component->values_json);
    free(component->defaults_json);
}

void scene_retention_remove(scene_retained_component **components, const char *name)
{
    scene_retained_component *found = scene_retention_find(*components, name);
    if (!found) {
        return;
    }
    int index = (int)(found - *components);
    free_component(found);
    adel(*components, index);
}

void scene_retention_free(scene_retained_component *components)
{
    for (int i = 0; i < acount(components); i++) {
        free_component(&components[i]);
    }
    afree(components);
}

char *scene_retention_save(const scene_retained_component *components)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    for (int i = 0; i < acount(components); i++) {
        const scene_retained_component *component = &components[i];
        yyjson_doc *
            values = yyjson_read(component->values_json, strlen(component->values_json), 0);
        yyjson_doc *defaults = yyjson_read(
            component->defaults_json,
            strlen(component->defaults_json),
            0
        );
        assert(values && defaults);
        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_val(doc, entry, "values", yyjson_val_mut_copy(doc, yyjson_doc_get_root(values)));
        yyjson_mut_obj_add_val(doc, entry, "defaults", yyjson_val_mut_copy(doc, yyjson_doc_get_root(defaults)));
        yyjson_mut_obj_add_bool(doc, entry, "automatic", component->automatic);
        yyjson_mut_obj_add_val(doc, root, component->name, entry);
        yyjson_doc_free(values);
        yyjson_doc_free(defaults);
    }
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

int scene_retention_load(const char *json, scene_retained_component **components)
{
    if (!json) {
        json = "{}";
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    scene_retained_component *loaded = NULL;
    int result = -1;
    if (!yyjson_is_obj(root)) {
        goto cleanup;
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(root);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        const char *name = yyjson_get_str(key);
        yyjson_val *entry = yyjson_obj_iter_get_val(key);
        yyjson_val *values = yyjson_obj_get(entry, "values");
        yyjson_val *defaults = yyjson_obj_get(entry, "defaults");
        yyjson_val *automatic = yyjson_obj_get(entry, "automatic");
        if (!name[0] || scene_retention_find(loaded, name) || !yyjson_is_obj(values) ||
            !yyjson_is_obj(defaults) || !yyjson_is_bool(automatic)) {
            goto cleanup;
        }
        scene_retained_component component = {
            .name = xstrdup(name),
            .values_json = yyjson_val_write(values, 0, NULL),
            .defaults_json = yyjson_val_write(defaults, 0, NULL),
            .automatic = yyjson_get_bool(automatic),
        };
        char *check = game_prototype_rebase_component_json(
            component.values_json,
            component.defaults_json,
            "{}"
        );
        if (!check) {
            free_component(&component);
            goto cleanup;
        }
        free(check);
        apush(loaded, component);
    }
    scene_retention_free(*components);
    *components = loaded;
    loaded = NULL;
    result = 0;
cleanup:
    scene_retention_free(loaded);
    yyjson_doc_free(doc);
    return result;
}

bool scene_retention_validate(const scene_retained_component *components, const game_prototype_registry *registry)
{
    for (int i = 0; i < acount(components); i++) {
        const char *snapshots[] = { components[i].values_json,
                                    components[i].defaults_json };
        for (int j = 0; j < 2; j++) {
            size_t size = strlen(snapshots[j]) + 64;
            char *json = xmalloc(size);
            snprintf(json, size, "{\"name\":\"Retained\",\"components\":%s}", snapshots[j]);
            char error[256];
            game_prototype *parsed = game_prototype_load(json, registry, error, sizeof error);
            free(json);
            bool valid = parsed && ((j == 1 && !acount(parsed->components)) ||
                                    (acount(parsed->components) == 1 &&
                                     !strcmp(
                                         parsed->components[0].desc->name,
                                         components[i].name
                                     )));
            game_prototype_free(parsed);
            if (!valid) {
                return false;
            }
        }
    }
    return true;
}
