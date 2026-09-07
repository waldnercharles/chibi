#include "../game/authored_data.h"

#include "../third_party/yyjson/yyjson.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const FILES[] = { "Player",      "Enemy",   "Wave",
                                     "Arena",       "Weapon",  "Projectile",
                                     "Progression", "Support", "Material" };

static void write_file(const char *path, const char *json)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(json, file) >= 0);
    assert(fclose(file) == 0);
}

static int apply_ok(const game_authored_data *, void *)
{
    return 0;
}

static void check_number(const game_authored_data *data, const char *field, double expected)
{
    double value = 0;
    assert(game_authored_data_read_number(data, field, &value) == 0);
    assert(value == expected);
}

static void check_invalid(const char *value)
{
    char json[256];
    snprintf(json, sizeof json, "{\"version\":1,\"name\":\"Wave\",\"kind\":\"wave\",\"data\":{\"enemy_count\":%s}}", value);
    char error[256] = { 0 };
    assert(!game_authored_data_from_json(json, GAME_DATA_WAVE, error, sizeof error));
    assert(error[0]);
}

void test_authored_data()
{
    char error[256] = { 0 };
    for (int kind = GAME_DATA_PLAYER; kind <= GAME_DATA_MATERIAL; kind++) {
        char path[128];
        snprintf(path, sizeof path, "content/gameplay/%s.json", FILES[kind]);
        game_authored_data *data = game_authored_data_load(path, kind, error, sizeof error);
        assert(data && !game_authored_data_dirty(data) && !game_authored_data_unsaved(data));
        assert(strcmp(game_authored_data_name(data), FILES[kind]) == 0);
        game_authored_data_free(data);
    }

    game_authored_data *player = game_authored_data_load(
        "content/gameplay/Player.json",
        GAME_DATA_PLAYER,
        error,
        sizeof error
    );
    assert(player);
    assert(game_authored_data_set_number(player, "move_speed", 201.5, error, sizeof error) == 0);
    check_number(player, "move_speed", 201.5);
    check_number(player, "max_health", 100);
    yyjson_doc *json = yyjson_read(
        game_authored_data_json(player),
        strlen(game_authored_data_json(player)),
        0
    );
    yyjson_val *composition = yyjson_obj_get(yyjson_doc_get_root(json), "composition");
    assert(yyjson_arr_size(composition) == 3);
    assert(strcmp(yyjson_get_str(yyjson_arr_get(composition, 0)), "GameTransform") == 0);
    yyjson_doc_free(json);
    assert(game_authored_data_set_number(player, "unknown", 1, error, sizeof error) == -1);
    assert(
        game_authored_data_set_number(player, "move_speed", INFINITY, error, sizeof error) == -1
    );
    assert(game_authored_data_set_number(player, "move_speed", 1e100, error, sizeof error) == -1);
    check_number(player, "move_speed", 201.5);

    char directory[] = "/tmp/c-blueprints-data-XXXXXX";
    assert(mkdtemp(directory));
    char path[128];
    snprintf(path, sizeof path, "%s/Player.json", directory);
    assert(game_authored_data_save(player, path) == 0);
    assert(!game_authored_data_unsaved(player) && game_authored_data_dirty(player));
    assert(game_authored_data_set_path(player, path) == 0);
    assert(game_authored_data_set_number(player, "move_speed", 222, error, sizeof error) == 0);
    assert(game_authored_data_apply(player, apply_ok, NULL) == 0);
    assert(game_authored_data_unsaved(player) && !game_authored_data_dirty(player));
    assert(game_authored_data_save(player, "/no-such-gameplay-directory/Player.json") == -1);
    assert(game_authored_data_unsaved(player));
    assert(game_authored_data_reload(player, error, sizeof error) == 0);
    check_number(player, "move_speed", 201.5);
    assert(!game_authored_data_unsaved(player));
    write_file(path, "invalid");
    assert(game_authored_data_reload(player, error, sizeof error) == -1);
    check_number(player, "move_speed", 201.5);
    assert(strcmp(game_authored_data_name(player), "Player") == 0);
    assert(unlink(path) == 0);
    assert(game_authored_data_reload(player, error, sizeof error) == -1);
    check_number(player, "move_speed", 201.5);
    assert(rmdir(directory) == 0);

    const char *renamed = "{\"version\":1,\"name\":\"Renamed\",\"kind\":"
                          "\"player\",\"data\":{\"move_speed\":55}}";
    assert(game_authored_data_set_json(player, renamed, error, sizeof error) == 0);
    assert(strcmp(game_authored_data_name(player), "Renamed") == 0);
    assert(game_authored_data_revert(player) == 0);
    assert(strcmp(game_authored_data_name(player), "Player") == 0);
    check_number(player, "move_speed", 201.5);
    assert(game_authored_data_set_json(player, "{", error, sizeof error) == -1);
    assert(strcmp(game_authored_data_name(player), "Player") == 0);
    assert(!game_authored_data_from_json(renamed, GAME_DATA_WAVE, error, sizeof error));
    assert(!game_authored_data_from_json("{", GAME_DATA_PLAYER, error, sizeof error));
    assert(!game_authored_data_from_json(
        "{\"version\":1,\"name\":\"Invalid\",\"kind\":\"player\",\"data\":{"
        "\"unknown\":1}}",
        GAME_DATA_PLAYER,
        error,
        sizeof error
    ));
    game_authored_data_free(player);

    game_authored_data *wave = game_authored_data_load(
        "content/gameplay/Wave.json",
        GAME_DATA_WAVE,
        error,
        sizeof error
    );
    assert(wave);
    assert(
        game_authored_data_set_number(wave, "enemy_count", 2147483647.0, error, sizeof error) == 0
    );
    check_number(wave, "enemy_count", 2147483647.0);
    assert(
        game_authored_data_set_number(wave, "enemy_count", 2147483648.0, error, sizeof error) ==
        -1
    );
    assert(
        game_authored_data_set_number(wave, "enemy_count", -2147483649.0, error, sizeof error) ==
        -1
    );
    assert(game_authored_data_set_number(wave, "enemy_count", 1.5, error, sizeof error) == -1);
    check_number(wave, "enemy_count", 2147483647.0);
    assert(
        game_authored_data_set_number(wave, "enemy_count", -2147483648.0, error, sizeof error) == 0
    );
    check_number(wave, "enemy_count", -2147483648.0);
    game_authored_data_free(wave);
    check_invalid("2147483648");
    check_invalid("-2147483649");
    check_invalid("18446744073709551615");
    check_invalid("1.5");
    check_invalid("true");
    puts("authored data PASS: fixtures, numeric edits, validation, preserved composition, names, save/apply baselines, and reload isolation");
}
