#include "runtime/geometry/material_surface_mesh.h"

#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/material_terrain.h"
#include "runtime/materials.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <string.h>

static MaterialSurface3D options()
{
    return (MaterialSurface3D){ .enabled = true,
                                .elevation = 12,
                                .depth = 8,
                                .bevel_width = 2,
                                .relief = 1,
                                .relief_scale = 7,
                                .spacing = 1,
                                .seed = 52 };
}
static bool build(const material_mask *mask, MaterialFrame frame, render_mesh_asset *mesh)
{
    char error[256] = { 0 };
    return material_surface_mesh_build(material_mask_occupancy(mask), options(), frame, mesh, error, sizeof error);
}
static bool plane_height(void *context, CF_V2 point, float *height)
{
    (void)context;
    *height = .02f * point.x - .03f * point.y;
    return true;
}
static bool triangle_contains_origin(CF_V3 a, CF_V3 b, CF_V3 c)
{
    float area = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
    if (fabsf(area) < 1e-8f) {
        return false;
    }
    float u = (b.x * c.z - b.z * c.x) / area;
    float v = (c.x * a.z - c.z * a.x) / area;
    float w = (a.x * b.z - a.z * b.x) / area;
    return u >= 0 && v >= 0 && w >= 0;
}
static bool valid_vertices(const render_mesh_asset *mesh)
{
    for (int i = 0; i < acount(mesh->vertices); i++) {
        render_mesh_surface_vertex v = mesh->vertices[i];
        float n = v.normal.x * v.normal.x + v.normal.y * v.normal.y +
                  v.normal.z * v.normal.z;
        if (!isfinite(v.position.x) || !isfinite(v.position.y) ||
            !isfinite(v.position.z) || !isfinite(n) || fabsf(n - 1) > 1e-4f ||
            !isfinite(v.part_position.x) || !isfinite(v.part_position.y) ||
            !isfinite(v.part_position.z)) {
            return false;
        }
    }
    return true;
}
static bool relief_and_cut_are_actual_geometry()
{
    material_mask mask = material_mask_new(12, 12, 1, false);
    render_mesh_asset intact = { 0 }, cut = { 0 };
    REQUIRE(build(&mask, (MaterialFrame){ 0 }, &intact));
    REQUIRE(valid_vertices(&intact));
    REQUIRE(intact.max.y - intact.min.y > 1);
    int bottom_vertices = 0;
    for (int i = 0; i < acount(intact.vertices); i++) {
        render_mesh_surface_vertex v = intact.vertices[i];
        if (v.normal.y == 0 && v.position.y == options().elevation - options().depth) {
            REQUIRE(v.part_position.y == v.position.y);
            bottom_vertices++;
        }
    }
    REQUIRE(bottom_vertices > 0);
    material_mask_carve(&mask, 0, 0, 2.5f);
    REQUIRE(build(&mask, (MaterialFrame){ 0 }, &cut));
    REQUIRE(valid_vertices(&cut));
    for (int i = 0; i < acount(cut.vertices); i++) {
        CF_V3 p = cut.vertices[i].position;
        REQUIRE(p.x * p.x + p.z * p.z > 3);
    }
    for (int i = 0; i < acount(cut.indices); i += 3) {
        REQUIRE(!triangle_contains_origin(
            cut.vertices[cut.indices[i]].position,
            cut.vertices[cut.indices[i + 1]].position,
            cut.vertices[cut.indices[i + 2]].position
        ));
    }
    REQUIRE(acount(cut.indices) != acount(intact.indices));
    render_mesh_asset_free(&intact);
    render_mesh_asset_free(&cut);
    material_mask_free(&mask);
    return true;
}
/* Every top seam vertex must agree in both directions; one matching corner
 * cannot hide a mismatched contour, height or normal elsewhere on the seam. */
static bool matching_seam(const render_mesh_asset meshes[2], const float origins[2], float seam, bool intact)
{
    for (int side = 0; side < 2; side++) {
        REQUIRE(valid_vertices(&meshes[side]));
        int matches = 0;
        for (int i = 0; i < acount(meshes[side].vertices); i++) {
            render_mesh_surface_vertex a = meshes[side].vertices[i];
            if (fabsf(a.position.x + origins[side] - seam) > 1e-5f) {
                continue;
            }
            if (intact && fabsf(a.position.z) < 1.99f) {
                REQUIRE(a.normal.y > 0);
            }
            if (a.normal.y <= 0) {
                continue;
            }
            bool matched = false;
            for (int k = 0; k < acount(meshes[1 - side].vertices); k++) {
                render_mesh_surface_vertex b = meshes[1 - side].vertices[k];
                if (b.normal.y <= 0 ||
                    fabsf(b.position.x + origins[1 - side] - seam) > 1e-5f ||
                    fabsf(a.position.z - b.position.z) > 1e-5f) {
                    continue;
                }
                REQUIRE(fabsf(a.position.y - b.position.y) < 1e-5f);
                REQUIRE(fabsf(a.normal.x - b.normal.x) < .001f);
                REQUIRE(fabsf(a.normal.y - b.normal.y) < .001f);
                REQUIRE(fabsf(a.normal.z - b.normal.z) < .001f);
                REQUIRE(fabsf(a.part_position.x - b.part_position.x) < 1e-5f);
                REQUIRE(fabsf(a.part_position.z - b.part_position.z) < 1e-5f);
                matched = true;
                break;
            }
            REQUIRE(matched);
            matches++;
        }
        REQUIRE(matches > 0);
    }
    return true;
}
static bool neighbor_has_matching_height_and_no_wall()
{
    float field[130 * 4];
    for (int i = 0; i < 130 * 4; i++) {
        field[i] = -10;
    }
    material_terrain_chunk *chunks = material_terrain_chunks(
        field,
        (field_grid){ .width = 130, .height = 4, .cell_size = 1 }
    );
    REQUIRE(acount(chunks) == 2);
    material_terrain_view views[2] = { { &chunks[0].mask, chunks[0].x, chunks[0].y },
                                       { &chunks[1].mask, chunks[1].x, chunks[1].y } };
    float origins[2] = { chunks[0].x, chunks[1].x };
    float seam = chunks[0].x + chunks[0].mask.width * .5f;
    material_surface_height height = { plane_height, NULL };
    char error[256];
    for (int cut = 0; cut < 2; cut++) {
        if (cut) {
            for (int i = 0; i < 2; i++) {
                material_mask_carve(&chunks[i].mask, seam - chunks[i].x, 0, 1.25f);
            }
        }
        material_terrain_sync_views(views, 2, 4, 0);
        render_mesh_asset meshes[2] = { 0 };
        for (int j = 0; j < 2; j++) {
            REQUIRE(material_surface_mesh_build_height(
                material_mask_occupancy(&chunks[j].mask),
                options(),
                (MaterialFrame){ chunks[j].x, chunks[j].y, 0 },
                &height,
                &meshes[j],
                error,
                sizeof error
            ));
        }
        REQUIRE(matching_seam(meshes, origins, seam, !cut));
        for (int i = 0; i < 2; i++) {
            render_mesh_asset_free(&meshes[i]);
        }
    }
    material_terrain_chunks_free(chunks);
    return true;
}
static bool nonfinite_vertices_are_rejected()
{
    material_mask mask = material_mask_new(4, 4, 1, false);
    render_mesh_asset mesh = { 0 };
    REQUIRE(build(&mask, (MaterialFrame){ 0 }, &mesh));
    REQUIRE(valid_vertices(&mesh));
    render_mesh_surface_vertex original = mesh.vertices[0];
    mesh.vertices[0].normal.x = NAN;
    REQUIRE(!valid_vertices(&mesh));
    mesh.vertices[0] = original;
    mesh.vertices[0].position.x = NAN;
    REQUIRE(!valid_vertices(&mesh));
    mesh.vertices[0] = original;
    mesh.vertices[0].normal.z = INFINITY;
    REQUIRE(!valid_vertices(&mesh));
    render_mesh_asset_free(&mesh);
    material_mask_free(&mask);
    return true;
}
static bool authored_height_plane_uses_retained_frame_and_normals()
{
    material_mask mask = material_mask_new(12, 12, 1, false);
    render_mesh_asset mesh = { 0 };
    MaterialSurface3D style = options();
    style.bevel_width = 0;
    MaterialFrame frame = { 30, 10, .2f };
    material_surface_height height = { plane_height, NULL };
    char error[256];
    REQUIRE(material_surface_mesh_build_height(
        material_mask_occupancy(&mask),
        style,
        frame,
        &height,
        &mesh,
        error,
        sizeof error
    ));
    float dx = .02f * cosf(frame.radians) - .03f * sinf(frame.radians);
    float dz = -.02f * sinf(frame.radians) - .03f * cosf(frame.radians);
    float length = sqrtf(dx * dx + dz * dz + 1);
    int top_vertices = 0;
    for (int i = 0; i < acount(mesh.vertices); i++) {
        render_mesh_surface_vertex v = mesh.vertices[i];
        if (v.normal.y <= 0) {
            continue;
        }
        float expected;
        REQUIRE(plane_height(NULL, (CF_V2){ v.part_position.x, v.part_position.z }, &expected));
        REQUIRE(fabsf(v.position.y - style.elevation - expected) < 1e-5f);
        REQUIRE(fabsf(v.normal.x + dx / length) < .0002f);
        REQUIRE(fabsf(v.normal.z + dz / length) < .0002f);
        top_vertices++;
    }
    REQUIRE(top_vertices > 0);
    material_mask_carve(&mask, 0, 0, 2.5f);
    render_mesh_asset_free(&mesh);
    REQUIRE(material_surface_mesh_build_height(
        material_mask_occupancy(&mask),
        style,
        frame,
        &height,
        &mesh,
        error,
        sizeof error
    ));
    for (int i = 0; i < acount(mesh.indices); i += 3) {
        REQUIRE(!triangle_contains_origin(
            mesh.vertices[mesh.indices[i]].position,
            mesh.vertices[mesh.indices[i + 1]].position,
            mesh.vertices[mesh.indices[i + 2]].position
        ));
    }
    REQUIRE(valid_vertices(&mesh));
    render_mesh_asset_free(&mesh);
    material_mask_free(&mask);
    return true;
}
typedef struct
{
    float value;
    int calls;
    bool fail;
} bad_height;
static bool constant_height(void *context, CF_V2 point, float *height)
{
    (void)point;
    *height = *(const float *)context;
    return true;
}
static bool authored_displacement_stays_inside_depth_envelope()
{
    material_mask mask = material_mask_new(12, 12, 1, false);
    material_mask_carve(&mask, 0, 0, 2.5f);
    float values[2] = { -7.5f, 7.5f };
    char error[256];
    for (int j = 0; j < 2; j++) {
        render_mesh_asset mesh = { 0 };
        material_surface_height height = { constant_height, &values[j] };
        REQUIRE(material_surface_mesh_build_height(
            material_mask_occupancy(&mask),
            options(),
            (MaterialFrame){ 0 },
            &height,
            &mesh,
            error,
            sizeof error
        ));
        REQUIRE(valid_vertices(&mesh));
        REQUIRE(mesh.min.y >= options().elevation - options().depth);
        REQUIRE(mesh.max.y < options().elevation + options().depth);
        int top_vertices = 0;
        for (int i = 0; i < acount(mesh.vertices); i++) {
            render_mesh_surface_vertex v = mesh.vertices[i];
            if (v.normal.y <= 0) {
                continue;
            }
            REQUIRE(v.position.y > options().elevation - options().depth);
            top_vertices++;
        }
        REQUIRE(top_vertices > 0);
        render_mesh_asset_free(&mesh);
    }
    material_mask_free(&mask);
    return true;
}
static bool rejected_height(void *context, CF_V2 point, float *height)
{
    (void)point;
    bad_height *bad = context;
    if (++bad->calls < 12) {
        *height = 0;
        return true;
    }
    *height = bad->value;
    return !bad->fail;
}
static bool height_sampling_failures_preserve_previous_output()
{
    material_mask mask = material_mask_new(4, 4, 1, false);
    render_mesh_asset mesh = { 0 };
    REQUIRE(build(&mask, (MaterialFrame){ 0 }, &mesh));
    render_mesh_asset previous = mesh;
    char error[256];
    bad_height cases[] = { { NAN, 0, false },
                           { INFINITY, 0, false },
                           { 8, 0, false },
                           { -8, 0, false },
                           { 0, 0, true } };
    for (int i = 0; i < 5; i++) {
        material_surface_height height = { rejected_height, &cases[i] };
        REQUIRE(!material_surface_mesh_build_height(
            material_mask_occupancy(&mask),
            options(),
            (MaterialFrame){ 0 },
            &height,
            &mesh,
            error,
            sizeof error
        ));
        REQUIRE(strstr(error, "height sampler") != NULL);
        REQUIRE(!memcmp(&mesh, &previous, sizeof mesh));
    }
    render_mesh_asset_free(&mesh);
    material_mask_free(&mask);
    return true;
}
static bool stable_frame_and_rejected_options()
{
    material_mask mask = material_mask_new(4, 4, 1, false);
    render_mesh_asset a = { 0 }, b = { 0 };
    char error[256];
    REQUIRE(build(&mask, (MaterialFrame){ 30, 10, .2f }, &a));
    REQUIRE(build(&mask, (MaterialFrame){ 30, 10, .2f }, &b));
    REQUIRE(acount(a.vertices) == acount(b.vertices));
    for (int i = 0; i < acount(a.vertices); i++) {
        render_mesh_surface_vertex v = a.vertices[i];
        float c = cosf(.2f), s = sinf(.2f);
        REQUIRE(
            fabsf(v.part_position.x - (30 + c * v.position.x - s * v.position.z)) < 1e-5f
        );
        REQUIRE(
            fabsf(v.part_position.z - (10 + s * v.position.x + c * v.position.z)) < 1e-5f
        );
        REQUIRE(v.part_position.y == v.position.y);
        REQUIRE(v.part_from_object[0][0] == c);
        REQUIRE(v.part_from_object[0][2] == -s);
        REQUIRE(v.part_from_object[2][0] == s);
        REQUIRE(v.part_from_object[2][2] == c);
        REQUIRE(v.part_from_object[1][1] == 1);
    }
    REQUIRE(
        !memcmp(a.vertices, b.vertices, (size_t)acount(a.vertices) * sizeof *a.vertices)
    );
    MaterialSurface3D bad = options();
    bad.spacing = 0;
    render_mesh_asset rejected = { 0 };
    REQUIRE(!material_surface_mesh_build(
        material_mask_occupancy(&mask),
        bad,
        (MaterialFrame){ 0 },
        &rejected,
        error,
        sizeof error
    ));
    REQUIRE(!rejected.vertices && !rejected.indices);
    render_mesh_asset_free(&a);
    render_mesh_asset_free(&b);
    material_mask_free(&mask);
    return true;
}
static ecs_world *world;
static entities *entity_service;
static materials *runtime;
static void cleanup_runtime()
{
    if (runtime) {
        materials_free(runtime);
    }
    if (entity_service) {
        entities_free(entity_service);
    }
    if (world) {
        ecs_world_free(world);
    }
    runtime = NULL;
    entity_service = NULL;
    world = NULL;
}
static bool prepare_runtime()
{
    world = ecs_world_new();
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        REQUIRE(ecs_world_register_component(world, REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    entity_service = entities_new(world, NULL);
    runtime = materials_new(world, entity_service, NULL);
    return true;
}
static bool set_component(ecs_entity entity, const ecs_component_desc *component, const void *value)
{
    if (ecs_component_has(world, entity, component->id)) {
        pin_value declared = { .type = PIN_STRUCT, .type_name = component->type->name };
        pin_value replacement = declared;
        replacement.ptr = (void *)value;
        REQUIRE(
            ecs_component_write_value(world, entity, component->id, 0, declared, replacement) == ECS_OK
        );
    } else {
        REQUIRE(ecs_world_record_add(world, entity, component->id, value, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
    }
    return true;
}
static bool set_shader(ecs_entity entity, Shader shader)
{
    if (!Shader_get_const(world, entity)) {
        REQUIRE(set_component(entity, &COMPONENT_Shader, &shader));
    }
    REQUIRE(
        ecs_component_write_value(
            world,
            entity,
            COMPONENT_Shader.id,
            0,
            (pin_value){ .type = PIN_STRUCT, .type_name = "Shader" },
            (pin_value){ .type = PIN_STRUCT, .type_name = "Shader", .ptr = &shader }
        ) == ECS_OK
    );
    return true;
}
static bool shader_matches(ecs_entity entity, const Shader *expected)
{
    const Shader *actual = Shader_get_const(world, entity);
    if (!expected) {
        REQUIRE(!actual);
        return true;
    }
    REQUIRE(actual);
    REQUIRE(!strcmp(
        actual->material ? actual->material : "",
        expected->material ? expected->material : ""
    ));
    REQUIRE(
        !strcmp(actual->shape ? actual->shape : "", expected->shape ? expected->shape : "")
    );
    REQUIRE(!strcmp(
        actual->parameters ? actual->parameters : "",
        expected->parameters ? expected->parameters : ""
    ));
    REQUIRE(
        actual->bounds_x == expected->bounds_x && actual->bounds_y == expected->bounds_y
    );
    return true;
}
static bool frame_matches(ecs_entity entity, MaterialFrame expected)
{
    const MaterialFrame *frame = MaterialFrame_get_const(world, entity);
    REQUIRE(frame);
    REQUIRE(fabsf(frame->x - expected.x) < 1e-5f);
    REQUIRE(fabsf(frame->y - expected.y) < 1e-5f);
    REQUIRE(frame->radians == expected.radians);
    return true;
}
static bool retained_plane(ecs_entity entity, MaterialFrame expected)
{
    REQUIRE(frame_matches(entity, expected));
    const MaterialSurface3D *style = MaterialSurface3D_get_const(world, entity);
    REQUIRE(style && style->enabled);
    const material_mask *mask = materials_mask(runtime, entity);
    REQUIRE(mask && mask->count);
    render_mesh_asset mesh = { 0 };
    material_surface_height height = { plane_height, NULL };
    char error[256];
    REQUIRE(material_surface_mesh_build_height(
        material_mask_occupancy(mask),
        *style,
        *MaterialFrame_get_const(world, entity),
        &height,
        &mesh,
        error,
        sizeof error
    ));
    REQUIRE(valid_vertices(&mesh));
    int tops = 0;
    float c = cosf(expected.radians), s = sinf(expected.radians);
    for (int i = 0; i < acount(mesh.vertices); i++) {
        render_mesh_surface_vertex v = mesh.vertices[i];
        if (v.normal.y <= 0) {
            continue;
        }
        float x = expected.x + c * v.position.x - s * v.position.z;
        float y = expected.y + s * v.position.x + c * v.position.z;
        REQUIRE(fabsf(v.part_position.x - x) < 1e-5f);
        REQUIRE(fabsf(v.part_position.z - y) < 1e-5f);
        REQUIRE(fabsf(v.position.y - style->elevation - (.02f * x - .03f * y)) < 1e-5f);
        tops++;
    }
    REQUIRE(tops > 0);
    render_mesh_asset_free(&mesh);
    return true;
}
static bool fracture_retains_initial_surface_coordinates()
{
    for (int scenario = 0; scenario < 3; scenario++) {
        bool explicit_frame = scenario == 1, disabled = scenario == 2;
        REQUIRE(prepare_runtime());
        ecs_entity config = ecs_world_create_entity(world);
        REQUIRE(set_component(
            config,
            &COMPONENT_MaterialDestructionWorld,
            &(MaterialDestructionWorld){ .fragment_limit = 8 }
        ));
        ecs_entity slab = ecs_world_create_entity(world);
        REQUIRE(set_component(slab, &COMPONENT_Transform, &(Transform){ 30, -12 }));
        REQUIRE(set_component(slab, &COMPONENT_Orientation, &(Orientation){ .4f }));
        REQUIRE(set_component(
            slab,
            &COMPONENT_Collider,
            &(Collider){ .shape = COLLIDER_BOX, .width = 12, .height = 4 }
        ));
        REQUIRE(set_component(
            slab,
            &COMPONENT_Destructible,
            &(Destructible){ .cell_size = 1, .min_fragment_cells = 1 }
        ));
        MaterialSurface3D style = options();
        style.bevel_width = 0;
        style.enabled = !disabled;
        REQUIRE(set_component(slab, &COMPONENT_MaterialSurface3D, &style));
        MaterialFrame expected = explicit_frame ? (MaterialFrame){ -5, 7, 1.1f }
                                                : (MaterialFrame){ 0 };
        if (explicit_frame) {
            REQUIRE(set_component(slab, &COMPONENT_MaterialFrame, &expected));
        }
        Shader binding = { .material = "OriginalRock",
                           .parameters = "OriginalParameters" };
        REQUIRE(set_shader(slab, binding));
        REQUIRE(!materials_prepare(runtime));
        if (disabled) {
            REQUIRE(!MaterialFrame_get_const(world, slab));
        } else {
            REQUIRE(retained_plane(slab, expected));
        }
        REQUIRE(set_component(slab, &COMPONENT_Transform, &(Transform){ 130, 80 }));
        REQUIRE(set_component(slab, &COMPONENT_Orientation, &(Orientation){ .9f }));
        REQUIRE(!materials_prepare(runtime));
        if (disabled) {
            REQUIRE(!MaterialFrame_get_const(world, slab));
        } else {
            REQUIRE(retained_plane(slab, expected));
        }
        materials_damage(runtime, slab, (fluid_vec){ 130, 80 }, 2.5f, 1, 0, (material_impact){ 0 });
        REQUIRE(!materials_prepare(runtime));
        ecs_query query;
        ecs_entity entity, fragments[8];
        int count = 0;
        ecs_component_id id = COMPONENT_Body.id;
        REQUIRE(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
        while (ecs_query_next(&query, &entity)) {
            if (materials_is_fragment(runtime, entity) && count < 8) {
                fragments[count++] = entity;
            }
        }
        ecs_query_end(&query);
        REQUIRE(count > 0);
        REQUIRE(
            set_shader(slab, (Shader){ .material = "ReplacementRock", .parameters = "NewParameters" })
        );
        for (int i = 0; i < count; i++) {
            REQUIRE(shader_matches(fragments[i], &binding));
        }
        if (disabled) {
            style.enabled = true;
            REQUIRE(set_component(slab, &COMPONENT_MaterialSurface3D, &style));
            for (int i = 0; i < count; i++) {
                REQUIRE(frame_matches(fragments[i], expected));
                REQUIRE(!MaterialSurface3D_get_const(world, fragments[i])->enabled);
                REQUIRE(set_component(fragments[i], &COMPONENT_MaterialSurface3D, &style));
            }
            REQUIRE(!materials_prepare(runtime));
        }
        REQUIRE(retained_plane(slab, expected));
        for (int i = 0; i < count; i++) {
            REQUIRE(retained_plane(fragments[i], expected));
            REQUIRE(
                set_component(fragments[i], &COMPONENT_Transform, &(Transform){ -90, 160 })
            );
            REQUIRE(
                set_component(fragments[i], &COMPONENT_Orientation, &(Orientation){ -1.2f })
            );
        }
        REQUIRE(!materials_prepare(runtime));
        for (int i = 0; i < count; i++) {
            REQUIRE(retained_plane(fragments[i], expected));
        }
        cleanup_runtime();
    }
    return true;
}
static bool terrain_surface_edits_preserve_cut_masks()
{
    REQUIRE(prepare_runtime());
    char error[256];
    field_ir ir = { .op = FIELD_CONSTANT, .value = -10 };
    field_program *program = field_program_new(&ir, 1, 0, NULL, 0, error, sizeof error);
    REQUIRE(program && material_field_register(materials_fields(runtime), "Rock", program));
    ecs_entity source = ecs_world_create_entity(world);
    REQUIRE(set_component(source, &COMPONENT_Transform, &(Transform){ 30, 10 }));
    REQUIRE(set_component(
        source,
        &COMPONENT_MaterialTerrain,
        &(MaterialTerrain){ .width = 256, .height = 8, .cell_size = 1, .field = "Rock", .min_fragment_cells = 1 }
    ));
    REQUIRE(!materials_prepare(runtime));
    ecs_entity children[2], entity;
    ecs_query query;
    ecs_component_id id = COMPONENT_Destructible.id;
    int count = 0;
    REQUIRE(ecs_query_begin(world, &query, &id, 1) == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        if (materials_mask(runtime, entity) && count < 2) {
            children[count++] = entity;
        }
    }
    ecs_query_end(&query);
    REQUIRE(count == 2);
    materials_damage(runtime, source, (fluid_vec){ 30, 10 }, 1.4f, 1, 0, (material_impact){ 0 });
    REQUIRE(!materials_prepare(runtime));
    const material_mask *masks[2];
    uint64_t incarnations[2];
    unsigned char cells[2][128 * 8];
    float distances[2][128 * 8];
    int counts[2];
    for (int i = 0; i < 2; i++) {
        masks[i] = materials_mask(runtime, children[i]);
        REQUIRE(masks[i] && masks[i]->count < 128 * 8 && masks[i]->count > 0);
        REQUIRE(!MaterialSurface3D_get_const(world, children[i]));
        incarnations[i] = masks[i]->incarnation;
        counts[i] = masks[i]->count;
        memcpy(cells[i], masks[i]->cells, sizeof cells[i]);
        memcpy(distances[i], masks[i]->distance, sizeof distances[i]);
    }
    MaterialSurface3D style = options();
    MaterialFrame frame = { -5, 7, .3f };
    Shader shader = { .material = "RockA",
                      .shape = "ShapeA",
                      .parameters = "ValuesA",
                      .bounds_x = 2,
                      .bounds_y = 3 };
    bool previous_shader = false;
    Shader previous_binding = { 0 };
    /* Add, edit, disable, remove only Frame, re-enable, then remove both. */
    for (int step = 0; step < 6; step++) {
        if (step == 1) {
            style.elevation = 20;
            style.depth = 12;
            style.material_height = true;
            style.seed++;
            frame = (MaterialFrame){ 12, -3, -.4f };
        }
        if (step == 2) {
            style.enabled = false;
        }
        if (step == 4) {
            style.enabled = true;
        }
        if (step < 5) {
            REQUIRE(set_component(source, &COMPONENT_MaterialSurface3D, &style));
        }
        if (step < 3) {
            REQUIRE(set_component(source, &COMPONENT_MaterialFrame, &frame));
        }
        if (step == 3 || step == 5) {
            if (MaterialFrame_get_const(world, source)) {
                REQUIRE(
                    ecs_world_record_remove(
                        world,
                        source,
                        COMPONENT_MaterialFrame.id,
                        0
                    ) == ECS_OK
                );
            }
            if (step == 5) {
                REQUIRE(
                    ecs_world_record_remove(
                        world,
                        source,
                        COMPONENT_MaterialSurface3D.id,
                        0
                    ) == ECS_OK
                );
            }
            REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        }
        bool has_shader = step != 2 && step < 5;
        if (step == 1) {
            shader = (Shader){ .material = "RockB",
                               .shape = "ShapeB",
                               .parameters = "ValuesB",
                               .bounds_x = 4,
                               .bounds_y = 5 };
        }
        if (step == 3) {
            shader.parameters = "ValuesC";
        }
        if (has_shader) {
            REQUIRE(set_shader(source, shader));
        } else {
            REQUIRE(ecs_world_record_remove(world, source, COMPONENT_Shader.id, 0) == ECS_OK);
            REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        }
        /* Source managed strings have been replaced/freed. Resident copies
         * must remain valid until the next synchronization boundary. */
        for (int i = 0; i < 2; i++) {
            REQUIRE(shader_matches(children[i], previous_shader ? &previous_binding : NULL));
        }
        REQUIRE(!materials_prepare(runtime));
        previous_shader = has_shader;
        previous_binding = shader;
        for (int i = 0; i < 2; i++) {
            REQUIRE(shader_matches(children[i], has_shader ? &shader : NULL));
            const material_mask *mask = materials_mask(runtime, children[i]);
            REQUIRE(
                mask == masks[i] && mask->incarnation == incarnations[i] &&
                mask->count == counts[i]
            );
            REQUIRE(!memcmp(mask->cells, cells[i], sizeof cells[i]));
            REQUIRE(!memcmp(mask->distance, distances[i], sizeof distances[i]));
            const MaterialSurface3D *child = MaterialSurface3D_get_const(world, children[i]);
            if (step == 5) {
                REQUIRE(!child && !MaterialFrame_get_const(world, children[i]));
                continue;
            }
            REQUIRE(
                child && child->enabled == style.enabled &&
                child->elevation == style.elevation && child->depth == style.depth &&
                child->seed == style.seed && child->material_height == style.material_height
            );
            const Transform *position = Transform_get_const(world, children[i]);
            float offset = position->x - 30;
            MaterialFrame expected = step < 3 ? frame : (MaterialFrame){ 30, 10, 0 };
            expected.x += cosf(expected.radians) * offset;
            expected.y += sinf(expected.radians) * offset;
            REQUIRE(frame_matches(children[i], expected));
        }
        if (step < 5 && style.enabled) {
            render_mesh_asset meshes[2] = { 0 };
            float origins[2];
            material_surface_height height = { plane_height, NULL };
            for (int i = 0; i < 2; i++) {
                origins[i] = Transform_get_const(world, children[i])->x - 30;
                REQUIRE(material_surface_mesh_build_height(
                    material_mask_occupancy(materials_mask(runtime, children[i])),
                    *MaterialSurface3D_get_const(world, children[i]),
                    *MaterialFrame_get_const(world, children[i]),
                    &height,
                    &meshes[i],
                    error,
                    sizeof error
                ));
            }
            REQUIRE(matching_seam(meshes, origins, 0, false));
            for (int i = 0; i < 2; i++) {
                render_mesh_asset_free(&meshes[i]);
            }
        }
    }
    return true;
}
void test_tool_material_surface_mesh()
{
    RUN_TEST_CASE(relief_and_cut_are_actual_geometry);
    RUN_TEST_CASE(nonfinite_vertices_are_rejected);
    RUN_TEST_CASE(neighbor_has_matching_height_and_no_wall);
    RUN_TEST_CASE(stable_frame_and_rejected_options);
    RUN_TEST_CASE(authored_height_plane_uses_retained_frame_and_normals);
    RUN_TEST_CASE(height_sampling_failures_preserve_previous_output);
    RUN_TEST_CASE(authored_displacement_stays_inside_depth_envelope);
    pu_setup(NULL, cleanup_runtime);
    RUN_TEST_CASE(fracture_retains_initial_surface_coordinates);
    RUN_TEST_CASE(terrain_surface_edits_preserve_cut_masks);
    pu_clear_setup();
}
