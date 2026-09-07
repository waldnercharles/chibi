#include "authored_data.h"

#include "../core/registry.h"
#include "../core/text_file.h"
#include "../core/xalloc.h"
#include "../third_party/yyjson/yyjson.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
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
    char *saved;
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

static bool integer_type(pin_type type)
{
    return type >= PIN_I8 && type <= PIN_U64;
}

static bool valid_number(pin_type type, yyjson_val *value)
{
    if (!yyjson_is_num(value)) {
        return false;
    }
    double number = yyjson_get_num(value);
    if (type == PIN_F32) {
        return isfinite(number) && fabs(number) <= FLT_MAX;
    }
    if (type == PIN_F64) {
        return isfinite(number);
    }
    if (!yyjson_is_int(value)) {
        return false;
    }
    int64_t minimum;
    uint64_t maximum;
    switch (type) {
        case PIN_I8:
            minimum = INT8_MIN;
            maximum = INT8_MAX;
            break;
        case PIN_U8:
            minimum = 0;
            maximum = UINT8_MAX;
            break;
        case PIN_I16:
            minimum = INT16_MIN;
            maximum = INT16_MAX;
            break;
        case PIN_U16:
            minimum = 0;
            maximum = UINT16_MAX;
            break;
        case PIN_I32:
            minimum = INT32_MIN;
            maximum = INT32_MAX;
            break;
        case PIN_U32:
            minimum = 0;
            maximum = UINT32_MAX;
            break;
        case PIN_I64:
            minimum = INT64_MIN;
            maximum = INT64_MAX;
            break;
        case PIN_U64:
            minimum = 0;
            maximum = UINT64_MAX;
            break;
        default: return false;
    }
    if (yyjson_is_uint(value)) {
        return yyjson_get_uint(value) <= maximum;
    }
    int64_t integer = yyjson_get_sint(value);
    return integer >= minimum && (integer < 0 || (uint64_t)integer <= maximum);
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

static int validate(const char *json, game_data_kind kind, char **name, char *error, size_t error_size)
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
        yyjson_obj_iter iterator = yyjson_obj_iter_with(data);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&iterator))) {
            const type_field *field = NULL;
            int index = known_field(type, yyjson_get_str(key));
            if (index >= 0) {
                field = &type->fields[index];
            }
            yyjson_val *value = yyjson_obj_iter_get_val(key);
            if (!field || !valid_number(field->type, value)) {
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
    if (!result) {
        *name = xstrdup(yyjson_get_str(asset_name));
    }
    yyjson_doc_free(doc);
    return result;
}

static game_authored_data *make(const char *json, game_data_kind kind, char *error, size_t error_size)
{
    char *name = NULL;
    if (!json || !game_authored_data_kind_name(kind) ||
        validate(json, kind, &name, error, error_size) != 0) {
        return NULL;
    }
    game_authored_data *data = xcalloc(1, sizeof *data);
    data->kind = kind;
    data->name = name;
    data->working = xstrdup(json);
    data->applied = xstrdup(json);
    data->saved = xstrdup(json);
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
    free(data->saved);
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
    return data && strcmp(data->working, data->applied) != 0;
}

int game_authored_data_unsaved(const game_authored_data *data)
{
    return data && strcmp(data->working, data->saved) != 0;
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
    char *name = NULL;
    if (!data || validate(json, data->kind, &name, error, error_size) != 0) {
        return -1;
    }
    char *copy = xstrdup(json);
    free(data->working);
    data->working = copy;
    free(data->name);
    data->name = name;
    return 0;
}

int game_authored_data_save(game_authored_data *data, const char *path)
{
    if (!data || !path || !path[0]) {
        return -1;
    }
    size_t size = strlen(path) + 5;
    char *tmp = xmalloc(size);
    snprintf(tmp, size, "%s.tmp", path);
    FILE *file = fopen(tmp, "wb");
    int result = -1;
    if (file) {
        result = fputs(data->working, file) < 0 ? -1 : 0;
        if (fclose(file) != 0) {
            result = -1;
        }
    }
    if (!result) {
        result = rename(tmp, path);
    }
    if (result) {
        remove(tmp);
    }
    free(tmp);
    if (result) {
        return -1;
    }
    free(data->saved);
    data->saved = xstrdup(data->working);
    return 0;
}

int game_authored_data_apply(game_authored_data *data, game_authored_data_apply_fn apply, void *user)
{
    if (!data || !apply || apply(data, user) != 0) {
        return -1;
    }
    char *copy = xstrdup(data->working);
    free(data->applied);
    data->applied = copy;
    return 0;
}

int game_authored_data_reload(game_authored_data *data, char *error, size_t error_size)
{
    if (!data || !data->path) {
        return -1;
    }
    char *json = text_file_read(data->path);
    char *name = NULL;
    if (!json) {
        error_set(error, error_size, "could not read gameplay asset %s", data->path);
        return -1;
    }
    if (validate(json, data->kind, &name, error, error_size) != 0) {
        free(json);
        return -1;
    }
    char *copy = xstrdup(json);
    free(json);
    free(data->working);
    free(data->applied);
    data->working = copy;
    data->applied = xstrdup(copy);
    free(data->saved);
    data->saved = xstrdup(copy);
    free(data->name);
    data->name = name;
    return 0;
}

int game_authored_data_revert(game_authored_data *data)
{
    if (!data || !data->applied) {
        return -1;
    }
    return game_authored_data_set_json(data, data->applied, NULL, 0);
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

int game_authored_data_set_number(
    game_authored_data *data,
    const char *field,
    double value,
    char *error,
    size_t error_size
)
{
    assert(data && field);
    const type_desc *type = registry_type_by_name(game_authored_data_type_name(data->kind));
    assert(type);
    int index = known_field(type, field);
    if (index < 0 || !isfinite(value)) {
        error_set(error, error_size, "unknown or invalid gameplay field %s", field);
        return -1;
    }
    yyjson_doc *source = yyjson_read(data->working, strlen(data->working), 0);
    assert(source);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(source, NULL);
    yyjson_doc_free(source);
    assert(doc);
    yyjson_mut_val *number;
    if (integer_type(type->fields[index].type)) {
        if (trunc(value) != value || value < -0x1p63 || value >= 0x1p64) {
            yyjson_mut_doc_free(doc);
            error_set(error, error_size, "invalid integer gameplay field %s", field);
            return -1;
        }
        number = value < 0 ? yyjson_mut_sint(doc, (int64_t)value)
                           : yyjson_mut_uint(doc, (uint64_t)value);
    } else {
        number = yyjson_mut_real(doc, value);
    }
    yyjson_mut_val *object = yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "data");
    yyjson_mut_obj_put(object, yyjson_mut_strcpy(doc, field), number);
    char *json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, NULL);
    assert(json);
    int result = game_authored_data_set_json(data, json, error, error_size);
    free(json);
    yyjson_mut_doc_free(doc);
    return result;
}
