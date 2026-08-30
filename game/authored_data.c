#include "authored_data.h"

#include "../core/registry.h"
#include "../core/text_file.h"
#include "../core/xalloc.h"
#include "../third_party/yyjson/yyjson.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct game_authored_data
{
    game_data_kind kind;
    char *name;
    char *path;
    char *working;
    char *applied;
    int dirty;
};

static const char *const KIND_NAMES[] = {
    "player",     "enemy",       "wave",    "arena",   "weapon",
    "projectile", "progression", "support", "material"
};
static const char *const TYPE_NAMES[] = {
    "GamePlayerData",      "GameEnemyData",   "GameWaveData",
    "GameArenaData",       "GameWeaponData",  "GameProjectileData",
    "GameProgressionData", "GameSupportData", "GameMaterialData"
};

static void error_set(char *error, size_t size, const char *format, ...)
{
    if (!error || !size) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
}

const char *game_authored_data_kind_name(game_data_kind kind)
{
    return kind >= GAME_DATA_PLAYER && kind <= GAME_DATA_MATERIAL ? KIND_NAMES[kind]
                                                                  : NULL;
}

const char *game_authored_data_type_name(game_data_kind kind)
{
    return kind >= GAME_DATA_PLAYER && kind <= GAME_DATA_MATERIAL ? TYPE_NAMES[kind]
                                                                  : NULL;
}

static int integer_type(pin_type type)
{
    return type == PIN_I8 || type == PIN_I16 || type == PIN_I32 || type == PIN_I64;
}

static int known_field(const type_desc *type, const char *name)
{
    for (int i = 0; i < type->field_count; i++) {
        if (strcmp(type->fields[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int validate(const char *json, game_data_kind kind, char *name, size_t name_size, char *error, size_t error_size)
{
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!doc) {
        error_set(error, error_size, "gameplay asset is not valid JSON");
        return -1;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *version = yyjson_obj_get(root, "version");
    yyjson_val *asset_name = yyjson_obj_get(root, "name");
    yyjson_val *asset_kind = yyjson_obj_get(root, "kind");
    yyjson_val *data = yyjson_obj_get(root, "data");
    const type_desc *type = registry_type_by_name(game_authored_data_type_name(kind));
    int result = 0;
    if (!yyjson_is_obj(root) || !yyjson_is_uint(version) ||
        yyjson_get_uint(version) != 1 || !yyjson_is_str(asset_name) ||
        !yyjson_get_len(asset_name) || !yyjson_is_str(asset_kind) ||
        strcmp(yyjson_get_str(asset_kind), game_authored_data_kind_name(kind)) != 0 ||
        !yyjson_is_obj(data) || !type) {
        error_set(
            error,
            error_size,
            "invalid %s gameplay asset (expected version 1 and reflected data)",
            game_authored_data_kind_name(kind)
        );
        result = -1;
    }
    if (!result) {
        snprintf(name, name_size, "%s", yyjson_get_str(asset_name));
        yyjson_obj_iter iterator = yyjson_obj_iter_with(data);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&iterator))) {
            const type_field *field = NULL;
            int index = known_field(type, yyjson_get_str(key));
            if (index >= 0) {
                field = &type->fields[index];
            }
            yyjson_val *value = yyjson_obj_iter_get_val(key);
            if (!field || !yyjson_is_num(value) ||
                (integer_type(field->type) && !yyjson_is_sint(value) &&
                 !yyjson_is_uint(value))) {
                error_set(
                    error,
                    error_size,
                    "%s data has unknown or invalid field %s",
                    game_authored_data_kind_name(kind),
                    yyjson_get_str(key)
                );
                result = -1;
                break;
            }
        }
    }
    yyjson_doc_free(doc);
    return result;
}

static game_authored_data *make(const char *json, game_data_kind kind, char *error, size_t error_size)
{
    char name[128];
    if (!json || !game_authored_data_kind_name(kind) ||
        validate(json, kind, name, sizeof name, error, error_size) != 0) {
        return NULL;
    }
    game_authored_data *data = xcalloc(1, sizeof *data);
    data->kind = kind;
    data->name = xstrdup(name);
    data->working = xstrdup(json);
    data->applied = xstrdup(json);
    return data;
}

game_authored_data *game_authored_data_from_json(
    const char *json,
    game_data_kind kind,
    char *error,
    size_t error_size
)
{
    return make(json, kind, error, error_size);
}

game_authored_data *game_authored_data_load(const char *path, game_data_kind kind, char *error, size_t error_size)
{
    char *json = text_file_read(path);
    game_authored_data *data = json ? make(json, kind, error, error_size) : NULL;
    if (!json) {
        error_set(error, error_size, "could not read gameplay asset %s", path ? path : "");
    }
    if (data) {
        data->path = xstrdup(path);
    }
    free(json);
    return data;
}

void game_authored_data_free(game_authored_data *data)
{
    if (!data) {
        return;
    }
    free(data->name);
    free(data->path);
    free(data->working);
    free(data->applied);
    free(data);
}
const char *game_authored_data_name(const game_authored_data *data)
{
    return data ? data->name : NULL;
}
const char *game_authored_data_json(const game_authored_data *data)
{
    return data ? data->working : NULL;
}
const char *game_authored_data_path(const game_authored_data *data)
{
    return data ? data->path : NULL;
}
int game_authored_data_dirty(const game_authored_data *data)
{
    return data ? data->dirty : 0;
}

int game_authored_data_set_path(game_authored_data *data, const char *path)
{
    if (!data || !path || !path[0]) {
        return -1;
    }
    char *copy = xstrdup(path);
    free(data->path);
    data->path = copy;
    return 0;
}

int game_authored_data_set_json(game_authored_data *data, const char *json, char *error, size_t error_size)
{
    char name[128];
    if (!data || validate(json, data->kind, name, sizeof name, error, error_size) != 0) {
        return -1;
    }
    char *copy = xstrdup(json);
    free(data->working);
    data->working = copy;
    data->dirty = strcmp(data->working, data->applied) != 0;
    return 0;
}

int game_authored_data_save(const game_authored_data *data, const char *path)
{
    if (!data || !path || !path[0]) {
        return -1;
    }
    size_t size = strlen(path) + 5;
    char *tmp = xmalloc(size);
    snprintf(tmp, size, "%s.tmp", path);
    FILE *file = fopen(tmp, "wb");
    int result = 0;
    if (!file || fputs(data->working, file) < 0 || fclose(file) != 0) {
        result = -1;
    } else if (rename(tmp, path) != 0) {
        result = -1;
    }
    if (result) {
        remove(tmp);
    }
    free(tmp);
    return result ? -1 : 0;
}

int game_authored_data_apply(game_authored_data *data, game_authored_data_apply_fn apply, void *user)
{
    if (!data || !apply || apply(data, user) != 0) {
        return -1;
    }
    char *copy = xstrdup(data->working);
    free(data->applied);
    data->applied = copy;
    data->dirty = 0;
    return 0;
}

int game_authored_data_reload(game_authored_data *data, char *error, size_t error_size)
{
    if (!data || !data->path) {
        return -1;
    }
    char *json = text_file_read(data->path);
    char name[128];
    if (!json || validate(json, data->kind, name, sizeof name, error, error_size) != 0) {
        free(json);
        return -1;
    }
    char *copy = xstrdup(json);
    free(json);
    free(data->working);
    free(data->applied);
    data->working = copy;
    data->applied = xstrdup(copy);
    data->dirty = 0;
    free(data->name);
    data->name = xstrdup(name);
    return 0;
}

int game_authored_data_revert(game_authored_data *data)
{
    if (!data || !data->applied) {
        return -1;
    }
    char *copy = xstrdup(data->applied);
    free(data->working);
    data->working = copy;
    data->dirty = 0;
    return 0;
}

int game_authored_data_read_number(const game_authored_data *data, const char *field, double *value)
{
    if (!data || !field || !value) {
        return -1;
    }
    yyjson_doc *doc = yyjson_read(data->working, strlen(data->working), 0);
    yyjson_val *object = doc ? yyjson_obj_get(yyjson_doc_get_root(doc), "data") : NULL;
    yyjson_val *number = object ? yyjson_obj_get(object, field) : NULL;
    int result = !number || !yyjson_is_num(number);
    if (!result) {
        *value = yyjson_get_num(number);
    }
    yyjson_doc_free(doc);
    return result ? -1 : 0;
}
