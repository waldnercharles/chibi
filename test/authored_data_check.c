#include "../core/registry.h"
#include "../game/authored_data.h"

#include <stdio.h>
#include <string.h>

static int applied(const game_authored_data *data, void *user)
{
    double *value = user;
    return game_authored_data_read_number(data, "move_speed", value);
}

static int check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition ? 0 : 1;
}

int main(void)
{
    registry_validate();
    int failures = 0;
    const char *paths[] = {
        "content/gameplay/Player.json",      "content/gameplay/Enemy.json",
        "content/gameplay/Wave.json",        "content/gameplay/Arena.json",
        "content/gameplay/Weapon.json",      "content/gameplay/Projectile.json",
        "content/gameplay/Progression.json", "content/gameplay/Support.json",
        "content/gameplay/Material.json",
    };
    for (int i = 0; i < 9; i++) {
        char error[256] = { 0 };
        game_authored_data *data = game_authored_data_load(paths[i], (game_data_kind)i, error, sizeof error);
        failures += check(data != NULL, error);
        failures += check(data && !game_authored_data_dirty(data), "loaded data starts applied");
        game_authored_data_free(data);
    }

    char error[256] = { 0 };
    game_authored_data *player = game_authored_data_load(paths[0], GAME_DATA_PLAYER, error, sizeof error);
    double speed = 0;
    failures += check(
        player && game_authored_data_read_number(player, "move_speed", &speed) == 0 &&
            speed == 180,
        "runtime reads authored player tuning"
    );
    const char
        *edited = "{\"version\":1,\"name\":\"Player\",\"kind\":\"player\","
                  "\"data\":{\"max_health\":125,\"move_speed\":240,\"fire_"
                  "rate\":5},\"composition\":[\"GameTransform\"]}";
    failures += check(
        game_authored_data_set_json(player, edited, error, sizeof error) == 0 &&
            game_authored_data_dirty(player),
        "editor edit is dirty"
    );
    failures += check(
        game_authored_data_revert(player) == 0 && !game_authored_data_dirty(player),
        "revert restores applied asset"
    );
    failures += check(
        game_authored_data_set_json(player, edited, error, sizeof error) == 0,
        "representative edit is accepted"
    );
    failures += check(
        game_authored_data_apply(player, applied, &speed) == 0 &&
            speed == 240 && !game_authored_data_dirty(player),
        "apply publishes authored tuning"
    );
    failures += check(
        game_authored_data_set_path(player, "/tmp/c-blueprints-player-data.json") == 0 &&
            game_authored_data_save(player, game_authored_data_path(player)) == 0,
        "save writes authored asset atomically"
    );
    failures += check(
        game_authored_data_set_json(player, "{\"version\":1}", error, sizeof error) != 0,
        "invalid asset is rejected"
    );
    failures += check(
        game_authored_data_reload(player, error, sizeof error) == 0 &&
            game_authored_data_read_number(player, "move_speed", &speed) == 0 &&
            speed == 240,
        "reload restores saved applied data"
    );
    remove("/tmp/c-blueprints-player-data.json");
    game_authored_data_free(player);
    return failures;
}
