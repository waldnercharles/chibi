#include "../game/scene.h"

#include <stdint.h>
#include <stdio.h>

static void reverse_parallel_for(int count, void (*worker)(int, void *), void *user)
{
    for (int i = count - 1; i >= 0; i--) {
        worker(i, user);
    }
}

int main(void)
{
    game_scene *one = game_scene_new(64, 16, UINT64_C(0x12345678));
    game_scene *many = game_scene_new(64, 16, UINT64_C(0x12345678));
    if (!one || !many) {
        return 1;
    }
    game_scene_set_parallel_for(many, reverse_parallel_for, NULL);
    int identical = 1;
    for (int frame = 0; frame < 600; frame++) {
        if (game_scene_frame(one, (double)frame / 60.0) != ECS_OK ||
            game_scene_frame(many, (double)frame / 60.0) != ECS_OK ||
            game_scene_hash(one) != game_scene_hash(many)) {
            identical = 0;
            break;
        }
    }
    printf("scene_600_frames_identical=%d\n", identical);
    printf("scene_final_hash=%llu\n", (unsigned long long)game_scene_hash(one));
    game_scene_free(one);
    game_scene_free(many);

    game_scene *budget = game_scene_new(2000, 500, UINT64_C(0xdecafbad));
    if (!budget) {
        return 1;
    }
    double total_ms = 0.0;
    double max_ms = 0.0;
    for (int frame = 0; frame < 100; frame++) {
        if (game_scene_frame(budget, (double)frame / 60.0) != ECS_OK) {
            game_scene_free(budget);
            return 1;
        }
        double ms = game_scene_behavior_ms(budget);
        total_ms += ms;
        if (ms > max_ms) {
            max_ms = ms;
        }
    }
    printf("budget_entities=%d\n", game_scene_entity_count(budget));
    printf("budget_behaviors=%d\n", game_scene_behavior_count(budget));
    printf("budget_average_behavior_ms=%.6f\n", total_ms / 100.0);
    printf("budget_max_behavior_ms=%.6f\n", max_ms);
    game_scene_free(budget);
    return 0;
}
