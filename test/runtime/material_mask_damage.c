#include "authoring/preview.h"
#include "runtime/component_catalog.h"
#include "runtime/material_debris.h"
#include "runtime/material_destruction.h"
#include "runtime/material_ecs.h"
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/runtime/material_fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST_CASE(material_mask_workspace)
{
    unsigned char cells[9 * 7] = { 0 };
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 3; x++) {
            cells[y * 9 + x] = 1;
        }
    }
    cells[2 * 9 + 1] = 0; /* Hole in the largest, edge-connected component. */
    cells[2 * 9 + 5] = cells[2 * 9 + 6] = cells[3 * 9 + 5] = 1;
    cells[5 * 9 + 4] = cells[6 * 9 + 8] = 1;
    material_mask_work *work = material_mask_work_new();
    for (int anchored = 0; anchored < 2; anchored++) {
        material_mask mask = material_mask_copy_cells(9, 7, 1, cells);
        REQUIRE(mask.count == 19 && !mask.revision && !mask.rects && !mask.surface);
        int dust;
        material_mask *pieces = material_mask_detach_fragments(&mask, 2, anchored, &dust, work);
        REQUIRE(acount(pieces) == 1 && pieces[0].count == 3);
        REQUIRE(
            pieces[0].cells[2 * 9 + 5] && pieces[0].cells[2 * 9 + 6] &&
            pieces[0].cells[3 * 9 + 5]
        );
        REQUIRE(dust == (anchored ? 1 : 2));
        REQUIRE(mask.count == (anchored ? 15 : 14) && !mask.revision);
        for (int i = 0; i < 9 * 7; i++) {
            bool kept = cells[i] && (i % 9 < 3 || (anchored && i == 6 * 9 + 8));
            REQUIRE(mask.cells[i] == kept);
            REQUIRE(mask.distance[i] == (kept ? -.5f : .5f));
        }
        material_mask_rebuild_work(&mask, work);
        REQUIRE(mask.revision == 1);
        material_test_check_cover(&mask);
        material_test_check_cover(&pieces[0]);
        REQUIRE(acount(mask.surface) > 0 && acount(pieces[0].surface) > 0);
        material_mask_free(&pieces[0]);
        afree(pieces);
        /* Retained components need no fragment allocation. Both geometry and
         * connectivity scratch have already handled a larger component set. */
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        pieces = material_mask_detach_fragments(&mask, 2, anchored, &dust, work);
        material_mask_rebuild_work(&mask, work);
        xalloc_stats_get(&after);
        REQUIRE(!pieces && !dust);
        REQUIRE(before.calls == after.calls && before.requested_bytes == after.requested_bytes);
        material_test_check_cover(&mask);
        material_mask_free(&mask);
    }
    /* A differently sized curved mask shares work without sharing its storage. */
    material_mask ellipse = material_mask_new(17, 13, 2, true);
    material_mask_rebuild_work(&ellipse, work);
    material_test_check_cover(&ellipse);
    material_mask_free(&ellipse);
    material_mask_work_free(work);
    return true;
}

TEST_CASE(destruction)
{
    material_mask mask = material_mask_new(24, 12, 2, false);
    REQUIRE(acount(mask.rects) == 1);
    int before = mask.count;
    int removed = material_mask_carve(&mask, 0, 0, 13);
    REQUIRE(removed > 0);
    REQUIRE(!material_mask_contains(&mask, 0, 0));
    material_test_check_cover(&mask);
    int dust;
    material_mask *pieces = material_mask_extract_fragments(&mask, 3, false, &dust);
    REQUIRE(acount(pieces) == 1);
    int retained = mask.count + dust;
    for (int i = 0; i < acount(pieces); i++) {
        material_test_check_cover(&pieces[i]);
        retained += pieces[i].count;
        for (int j = 0; j < mask.width * mask.height; j++) {
            REQUIRE(!(mask.cells[j] && pieces[i].cells[j]));
        }
        material_mask_free(&pieces[i]);
    }
    REQUIRE(retained + removed == before);
    afree(pieces);
    material_test_check_cover(&mask);
    material_mask_carve(&mask, 0, 0, 100);
    REQUIRE(!mask.count && !acount(mask.rects));
    material_mask_free(&mask);

    mask = material_mask_new(24, 12, 2, true);
    REQUIRE(mask.count < 24 * 12);
    material_test_check_cover(&mask);
    material_mask_carve(&mask, 0, 0, 13);
    before = mask.count;
    pieces = material_mask_extract_fragments(&mask, 1000, false, &dust);
    REQUIRE(!acount(pieces) && dust > 0 && mask.count + dust == before);
    material_test_check_cover(&mask);
    material_mask_free(&mask);
    return true;
}

static bool surface_contains(const material_mask *mask, float x, float y)
{
    for (int i = 0; i < acount(mask->surface); i++) {
        const material_polygon *p = &mask->surface[i];
        bool inside = false;
        for (int a = 0, b = p->count - 1; a < p->count; b = a++) {
            float ax = p->points[2 * a], ay = p->points[2 * a + 1];
            float bx = p->points[2 * b], by = p->points[2 * b + 1];
            if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax) {
                inside = !inside;
            }
        }
        if (inside) {
            return true;
        }
    }
    return false;
}

TEST_CASE(contours)
{
    material_mask mask = material_mask_new(128, 128, 2, false);
    /* Solid chunks remain one rectangle with no gaps along their shared edges. */
    REQUIRE(acount(mask.surface) == 1);
    REQUIRE(surface_contains(&mask, -127.99f, 0));
    REQUIRE(surface_contains(&mask, 127.99f, 0));
    material_mask_free(&mask);
    unsigned char cells[] = { 1, 0, 0, 1 };
    mask = material_mask_from_cells(2, 2, 1, cells);
    REQUIRE(material_mask_contains(&mask, -.1f, -.1f));
    REQUIRE(!surface_contains(&mask, -.1f, -.1f));
    REQUIRE(surface_contains(&mask, -.4f, -.4f));
    REQUIRE(surface_contains(&mask, .4f, .4f));
    REQUIRE(!surface_contains(&mask, 0, 0)); /* No false bridge across diagonal islands. */
    material_mask_carve(&mask, -.5f, -.5f, .1f);
    REQUIRE(!surface_contains(&mask, -.4f, -.4f));
    REQUIRE(surface_contains(&mask, .4f, .4f));
    material_mask_free(&mask);
    float field[] = { -.1f, .9f, -.1f, .9f };
    mask = material_mask_from_field(2, 2, 1, field);
    REQUIRE(surface_contains(&mask, -.45f, .1f));
    REQUIRE(!surface_contains(&mask, -.35f, .1f));
    material_mask_free(&mask);
    mask = material_mask_new(16, 16, 1, false);
    material_mask_carve(&mask, 0, 0, 2.2f);
    REQUIRE(!surface_contains(&mask, 2.1f, .01f));
    REQUIRE(surface_contains(&mask, 2.3f, .01f));
    material_mask_free(&mask);
    return true;
}

enum
{
    CAPACITY_TEST_SOURCE = 901
};

typedef struct
{
    material_mask mask;
    material_point position;
    float angle;
    int created, created_cells, destroyed, damage_dust, damage_removed;
    bool retain_chips, available, reenter;
} material_capacity_probe;

static float material_float_from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static uint32_t material_float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

TEST_CASE(material_damage_boundary_rounding)
{
    material_mask mask = material_mask_new(1, 1, 1, false);
    /* Separate squares include this boundary cell; either FMA ordering excludes it. */
    REQUIRE(
        material_mask_apply_damage(
            &mask,
            -material_float_from_bits(0x40ed0aca),
            -material_float_from_bits(0x40a46390),
            material_float_from_bits(0x41103ba9),
            1,
            1
        ) == 1
    );
    REQUIRE(mask.count == 0 && !mask.cells[0]);
    material_mask_free(&mask);
    return true;
}

static bool capacity_read_source(void *user, uint64_t target, material_source *source)
{
    material_capacity_probe *probe = user;
    if (target != CAPACITY_TEST_SOURCE || !probe->available) {
        return false;
    }
    *source = (material_source){ .mask = &probe->mask,
                                 .position = probe->position,
                                 .angle = probe->angle,
                                 .hardness = 1,
                                 .minimum = 1,
                                 .initial_cells = probe->mask.count,
                                 .retain_chips = probe->retain_chips };
    return true;
}

static void capacity_create_fragment(void *user, uint64_t source, material_fragment fragment)
{
    material_capacity_probe *probe = user;
    assert(source == CAPACITY_TEST_SOURCE);
    probe->created++;
    probe->created_cells += fragment.mask.count;
    material_test_check_cover(&probe->mask);
    material_test_check_cover(&fragment.mask);
    if (probe->reenter) {
        /* A host can run independent mask processing during a fragment callback.
         * It must neither borrow the owner's workspace nor disturb its masks. */
        material_mask nested = material_mask_new(17, 13, 2, true);
        material_mask_carve(&nested, 0, 0, 4);
        material_test_check_cover(&nested);
        material_mask_free(&nested);
        material_test_check_cover(&probe->mask);
        material_test_check_cover(&fragment.mask);
    }
    material_mask_free(&fragment.mask);
}

static void capacity_destroy_source(void *user, uint64_t target)
{
    material_capacity_probe *probe = user;
    assert(target == CAPACITY_TEST_SOURCE);
    probe->destroyed++;
}

static void capacity_invalidate(void *user, uint64_t target)
{
    material_capacity_probe *probe = user;
    assert(target == CAPACITY_TEST_SOURCE);
    material_test_check_cover(&probe->mask);
}

static void capacity_result(void *user, const material_event *event)
{
    material_capacity_probe *probe = user;
    if (event->kind != MATERIAL_EVENT_DAMAGE) {
        return;
    }
    probe->damage_removed = event->removed;
    probe->damage_dust = event->dust;
}

static material_mask capacity_split_shape(void)
{
    unsigned char cells[25] = { 0 };
    for (int y = 0; y < 5; y++) {
        cells[y * 5] = cells[y * 5 + 1] = 1;
        cells[y * 5 + 3] = cells[y * 5 + 4] = 1;
    }
    cells[2 * 5 + 2] = 1;
    return material_mask_from_cells(5, 5, 1, cells);
}

static material_host capacity_host(material_capacity_probe *probe)
{
    return (material_host){ .user = probe,
                            .read_source = capacity_read_source,
                            .create_fragment = capacity_create_fragment,
                            .destroy_source = capacity_destroy_source,
                            .invalidate_geometry = capacity_invalidate,
                            .result = capacity_result };
}

typedef struct
{
    material_destruction *destruction;
    material_mask masks[3];
    bool live[3], enqueue, release_on_damage;
    uint64_t released[3];
    int releases;
} release_callback_probe;

static bool release_callback_read(void *user, uint64_t target, material_source *source)
{
    release_callback_probe *probe = user;
    if (target < CAPACITY_TEST_SOURCE || target >= CAPACITY_TEST_SOURCE + 3) {
        return false;
    }
    int index = (int)(target - CAPACITY_TEST_SOURCE);
    if (!probe->live[index]) {
        return false;
    }
    *source = (material_source){ .mask = &probe->masks[index],
                                 .minimum = 1,
                                 .hardness = 1 };
    return true;
}

static void release_callback_fragment(void *user, uint64_t target, material_fragment fragment)
{
    release_callback_probe *probe = user;
    material_test_check_cover(&fragment.mask);
    material_mask_free(&fragment.mask);
    if (probe->enqueue && target == CAPACITY_TEST_SOURCE) {
        material_destruction_queue_release(probe->destruction, CAPACITY_TEST_SOURCE + 1);
        material_destruction_queue_release(probe->destruction, CAPACITY_TEST_SOURCE + 1);
        /* Already queued original must not be duplicated by a callback. */
        material_destruction_queue_release(probe->destruction, CAPACITY_TEST_SOURCE + 2);
    }
}

static void release_callback_destroy(void *user, uint64_t target)
{
    release_callback_probe *probe = user;
    probe->live[target - CAPACITY_TEST_SOURCE] = false;
}

static void release_callback_result(void *user, const material_event *event)
{
    release_callback_probe *probe = user;
    if (event->kind == MATERIAL_EVENT_DAMAGE && probe->release_on_damage) {
        material_destruction_queue_release(probe->destruction, event->target);
    }
    if (event->kind == MATERIAL_EVENT_RELEASE) {
        assert(probe->releases < 3);
        probe->released[probe->releases++] = event->target;
    }
}

static void release_callback_invalidate(void *user, uint64_t target)
{
    release_callback_probe *probe = user;
    material_test_check_cover(&probe->masks[target - CAPACITY_TEST_SOURCE]);
}

TEST_CASE(material_callback_queues)
{
    for (int damage = 0; damage < 2; damage++) {
        release_callback_probe probe = { .destruction = material_destruction_new(),
                                         .enqueue = !damage,
                                         .release_on_damage = damage };
        for (int i = 0; i < 3; i++) {
            probe.masks[i] = material_mask_new(3, 3, 1, false);
            probe.live[i] = true;
        }
        material_host host = { .user = &probe,
                               .read_source = release_callback_read,
                               .create_fragment = release_callback_fragment,
                               .destroy_source = release_callback_destroy,
                               .invalidate_geometry = release_callback_invalidate,
                               .result = release_callback_result };
        if (damage) {
            material_destruction_queue_damage(
                probe.destruction,
                (material_damage){ .target = CAPACITY_TEST_SOURCE, .radius = .1f, .energy = 1 }
            );
            material_destruction_step(probe.destruction, &host, 1);
            REQUIRE(probe.releases == 1 && probe.released[0] == CAPACITY_TEST_SOURCE);
            REQUIRE(!probe.live[0] && probe.live[1] && probe.live[2]);
        } else {
            material_destruction_queue_release(probe.destruction, CAPACITY_TEST_SOURCE);
            material_destruction_queue_release(probe.destruction, CAPACITY_TEST_SOURCE + 2);
            material_destruction_step(probe.destruction, &host, 1);
            REQUIRE(probe.releases == 1 && probe.released[0] == CAPACITY_TEST_SOURCE);
            REQUIRE(probe.live[1] && probe.live[2]);
            material_destruction_step(probe.destruction, &host, 1);
            REQUIRE(probe.releases == 2 && probe.released[1] == CAPACITY_TEST_SOURCE + 2);
            REQUIRE(probe.live[1] && !probe.live[2]);
            material_destruction_step(probe.destruction, &host, 1);
            REQUIRE(probe.releases == 3 && probe.released[2] == CAPACITY_TEST_SOURCE + 1);
            REQUIRE(!probe.live[1]);
            material_destruction_step(probe.destruction, &host, 1);
            REQUIRE(probe.releases == 3);
        }
        for (int i = 0; i < 3; i++) {
            material_mask_free(&probe.masks[i]);
        }
        material_destruction_free(probe.destruction);
    }
    return true;
}

TEST_CASE(material_capacity_fallback)
{
    material_destruction *destruction = material_destruction_new();
    material_capacity_probe probe = { .mask = capacity_split_shape(), .available = true };
    material_host host = capacity_host(&probe);
    material_destruction_queue_damage(
        destruction,
        (material_damage){ .target = CAPACITY_TEST_SOURCE,
                           .point = { 0, 0 },
                           .radius = .1f,
                           .energy = 1 }
    );
    unsigned revision = probe.mask.revision;
    material_destruction_step(destruction, &host, 0);
    REQUIRE(probe.mask.revision == revision + 2); /* Detached, then overflow restored. */
    REQUIRE(probe.mask.count == 20 && probe.created == 0);
    REQUIRE(probe.damage_removed == 1 && probe.damage_dust == 1);
    revision = probe.mask.revision;
    material_destruction_step(destruction, &host, 0);
    REQUIRE(probe.mask.revision == revision);
    REQUIRE(probe.mask.count == 20 && probe.created == 0);
    /* Both retry queue buffers are warm; repeated blocked retries retain storage. */
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    material_destruction_step(destruction, &host, 0);
    material_destruction_step(destruction, &host, 0);
    xalloc_stats_get(&after);
    REQUIRE(before.calls == after.calls && before.requested_bytes == after.requested_bytes);
    REQUIRE(probe.mask.revision == revision && !probe.created);
    material_destruction_step(destruction, &host, 1);
    REQUIRE(probe.mask.revision == revision + 1);
    REQUIRE(probe.mask.count == 10 && probe.created == 1 && probe.created_cells == 10);
    REQUIRE(probe.mask.count + probe.created_cells == 20);
    material_mask_free(&probe.mask);
    material_destruction_free(destruction);

    destruction = material_destruction_new();
    probe = (material_capacity_probe){ .mask = capacity_split_shape(),
                                       .available = true };
    host = capacity_host(&probe);
    material_destruction_queue_damage(
        destruction,
        (material_damage){ .target = CAPACITY_TEST_SOURCE,
                           .point = { 0, 0 },
                           .radius = .1f,
                           .energy = 1 }
    );
    material_destruction_step(destruction, &host, 0);
    probe.available = false;
    material_destruction_step(destruction, &host, 0);
    probe.available = true;
    material_destruction_step(destruction, &host, 1);
    REQUIRE(probe.mask.count == 20 && probe.created == 0);
    material_mask_free(&probe.mask);
    material_destruction_free(destruction);

    destruction = material_destruction_new();
    probe = (material_capacity_probe){ .mask = capacity_split_shape(),
                                       .retain_chips = true,
                                       .available = true,
                                       .reenter = true };
    host = capacity_host(&probe);
    material_destruction_queue_damage(
        destruction,
        (material_damage){ .target = CAPACITY_TEST_SOURCE,
                           .point = { 0, 0 },
                           .radius = .1f,
                           .energy = 1 }
    );
    material_destruction_step(destruction, &host, 1);
    REQUIRE(probe.created == 1 && probe.created_cells == 10);
    REQUIRE(probe.mask.count == 10 && !material_mask_contains(&probe.mask, 0, 0));
    REQUIRE(probe.damage_dust == 1);
    REQUIRE(probe.mask.count + probe.created_cells + probe.damage_dust == 21);
    material_destruction_step(destruction, &host, 1);
    REQUIRE(probe.mask.count == 10 && probe.created == 1);
    material_mask_free(&probe.mask);
    material_destruction_free(destruction);

    destruction = material_destruction_new();
    probe = (material_capacity_probe){ .mask = material_mask_new(5, 5, 1, false),
                                       .available = true };
    host = capacity_host(&probe);
    material_destruction_queue_release(destruction, CAPACITY_TEST_SOURCE);
    material_destruction_step(destruction, &host, 0);
    REQUIRE(!probe.destroyed && !probe.created && probe.mask.count == 25);
    material_destruction_step(destruction, &host, 1);
    REQUIRE(probe.destroyed == 1 && probe.created == 1 && probe.created_cells == 25);
    REQUIRE(!probe.mask.count);
    material_destruction_step(destruction, &host, 1);
    REQUIRE(probe.destroyed == 1 && probe.created == 1);
    material_mask_free(&probe.mask);
    material_destruction_free(destruction);
    return true;
}

TEST_CASE(rotated_material_damage_rounding)
{
    material_capacity_probe probe = {
        .mask = material_mask_new(128, 128, 2, false),
        .position = { 0, material_float_from_bits(0xc3160000) },
        .angle = material_float_from_bits(0x3df5c28f),
        .available = true
    };
    material_host host = capacity_host(&probe);
    material_destruction *destruction = material_destruction_new();
    material_destruction_queue_damage(
        destruction,
        (material_damage){ .target = CAPACITY_TEST_SOURCE,
                           .point = { material_float_from_bits(0xc2aa2660),
                                      material_float_from_bits(0xc2fcd739) },
                           .radius = 4,
                           .energy = 1 }
    );
    material_destruction_step(destruction, &host, 1);
    int index = 80 * probe.mask.width + 22;
    REQUIRE(probe.damage_removed > 0 && !probe.mask.cells[index]);
    /* Pin the recorded rotated hit's distance on the native/WASM toolchains. */
    REQUIRE(material_float_bits(probe.mask.distance[index]) == 0x40210707);
    material_destruction_free(destruction);
    material_mask_free(&probe.mask);
    return true;
}

TEST_CASE(fragment_glow_rounding)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ecs_entity fragment = { 0 };
    for (int slot = 1; slot <= 179; slot++) {
        fragment = ecs_world_create_entity(world);
        REQUIRE(fragment.slot == (uint32_t)slot);
    }
    material_test_component(
        world,
        fragment,
        &COMPONENT_FragmentGlow,
        &(FragmentGlow){ .duration = 3.5f, .flicker = .35f }
    );
    material_test_component(world, fragment, &COMPONENT_Material, &(Material){ .emission_multiplier = .5f });
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);

    materials materials = { .world = world };
    asetlen(materials.records, (int)fragment.slot + 1);
    memset(materials.records, 0, acount(materials.records) * sizeof *materials.records);
    materials.records[fragment.slot] = (material_record){ .entity = fragment,
                                                          .fragment = true,
                                                          .glow_multiplier = .5f };
    for (int i = 0; i < 17; i++) {
        material_ecs_fade_fragments(&materials, 1.f / 60);
    }
    REQUIRE(
        material_float_bits(Material_get_const(world, fragment)->emission_multiplier) == 0x3e953602
    );
    afree(materials.records);
    ecs_world_free(world);
    return true;
}

TEST_CASE(small_hull_release)
{
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    preview_world *live = preview_world_new(world);
    ecs_entity config = ecs_world_create_entity(world),
               slab = ecs_world_create_entity(world);
    material_test_component(
        world,
        config,
        &COMPONENT_MaterialDestructionWorld,
        &(MaterialDestructionWorld){ 8 }
    );
    material_test_component(
        world,
        config,
        &COMPONENT_MaterialFluidWorld,
        &(MaterialFluidWorld){ 64, 32, 10, 100 }
    );
    material_test_component(world, slab, &COMPONENT_Transform, &(Transform){ 0 });
    material_test_component(
        world,
        slab,
        &COMPONENT_Collider,
        &(Collider){ .shape = COLLIDER_BOX, .width = 20, .height = 20, .category = 8, .mask = 15 }
    );
    material_test_component(
        world,
        slab,
        &COMPONENT_Destructible,
        &(Destructible){ .cell_size = 1, .min_fragment_cells = 3, .release_fraction = .3f }
    );
    material_test_component(world, slab, &COMPONENT_Health, &(Health){ 3, 3 });
    material_test_component(world, slab, &COMPONENT_Projectile, &(Projectile){ 0 });
    REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    REQUIRE(materials_mask(live->runtime->materials, slab)->count == 400);
    materials_damage(live->runtime->materials, slab, (fluid_vec){ 10, 0 }, 9, 1, 0, (material_impact){ 0 });
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    REQUIRE(materials_mask(live->runtime->materials, slab)->count > 240);
    REQUIRE(Health_get_const(world, slab)->current == 3);
    materials_damage(live->runtime->materials, slab, (fluid_vec){ -10, 0 }, 12, 1, 0, (material_impact){ 0 });
    REQUIRE(physics_tick(live->runtime->physics, 0) == 0);
    REQUIRE(!materials_mask(live->runtime->materials, slab));
    ecs_query query;
    ecs_entity entity;
    ecs_component_id body = COMPONENT_Body.id;
    int fragments = 0;
    REQUIRE(ecs_query_begin(world, &query, &body, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        REQUIRE(materials_is_fragment(live->runtime->materials, entity));
        REQUIRE(!Health_get_const(world, entity));
        REQUIRE(!Projectile_get_const(world, entity));
        REQUIRE(Destructible_get_const(world, entity)->release_fraction == 0);
        fragments++;
    }
    ecs_query_end(&query);
    REQUIRE(fragments > 0);
    preview_world_free(live);
    ecs_world_free(world);
    return true;
}

void test_material_mask_damage()
{
    RUN_TEST_CASE(material_mask_workspace);
    RUN_TEST_CASE(destruction);
    RUN_TEST_CASE(contours);
    RUN_TEST_CASE(material_damage_boundary_rounding);
    RUN_TEST_CASE(material_callback_queues);
    RUN_TEST_CASE(material_capacity_fallback);
    RUN_TEST_CASE(rotated_material_damage_rounding);
    RUN_TEST_CASE(fragment_glow_rounding);
    RUN_TEST_CASE(small_hull_release);
}
