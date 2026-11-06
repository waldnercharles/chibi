#include "authoring/scene_hierarchy.h"

#include "assets/scene_document.h"
#include "authoring/scene_snapshot.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <string.h>

#define QUARTER_TURN 1.5707963267948966f

static ecs_entity entity(int index)
{
    return (ecs_entity){ .slot = (uint32_t)index, .generation = 7 };
}

static authoring_scene_node object(int index, const char *name, int parent)
{
    return (authoring_scene_node){ .entity = entity(index), .name = name, .parent = parent };
}

static bool near(float a, float b)
{
    return fabsf(a - b) < .0001f;
}

static int move(
    authoring_scene_node *nodes,
    int count,
    const int *indices,
    int selected_count,
    int parent,
    int before,
    bool keep_world,
    char *error
)
{
    ecs_entity *selected = NULL;
    for (int i = 0; i < selected_count; i++) {
        apush(selected, entity(indices[i]));
    }
    int result = authoring_scene_move(
        nodes,
        count,
        selected,
        selected_count,
        parent ? entity(parent) : (ecs_entity){ 0 },
        before ? entity(before) : (ecs_entity){ 0 },
        keep_world,
        error,
        256
    );
    afree(selected);
    return result;
}

TEST_CASE(move_groups_topmost_selection_in_scene_order)
{
    authoring_scene_node nodes[] = {
        object(1, "A", -1),       object(2, "Inside", 0), object(3, "B", -1),
        object(4, "Existing", 2), object(5, "C", -1),     object(6, "D", -1)
    };
    char error[256];
    int selected[] = { 5, 2, 1, 1 };
    REQUIRE(move(nodes, 6, selected, 4, 3, 4, true, error) == 0);
    const int order[] = { 3, 1, 2, 5, 4, 6 };
    const int parents[] = { -1, 0, 1, 0, 0, -1 };
    for (int i = 0; i < 6; i++) {
        REQUIRE(ecs_entity_to_u64(nodes[i].entity) == ecs_entity_to_u64(entity(order[i])));
        REQUIRE(nodes[i].parent == parents[i]);
    }
    REQUIRE(authoring_scene_validate(nodes, 6, error, sizeof error) == 0);
    REQUIRE(move(nodes, 6, (int[]){ 6 }, 1, 0, 3, false, error) == 0);
    REQUIRE(nodes[0].entity.slot == 6 && nodes[1].entity.slot == 3);
    REQUIRE(move(nodes, 6, (int[]){ 1, 5 }, 2, 0, 0, false, error) == 0);
    REQUIRE(nodes[3].entity.slot == 1 && nodes[3].parent == -1);
    REQUIRE(nodes[4].entity.slot == 2 && nodes[4].parent == 3);
    REQUIRE(nodes[5].entity.slot == 5 && nodes[5].parent == -1);
    return true;
}

TEST_CASE(move_rejects_invalid_destinations_without_mutation)
{
    authoring_scene_node original[] = { object(1, "A", -1),
                                        object(2, "Child", 0),
                                        object(3, "B", -1),
                                        object(4, "Child", 2) };
    const struct
    {
        int selected, parent, before;
        const char *reason;
    } cases[] = {
        { 1, 2, 0, "descendants" },  { 1, 1, 0, "itself" },
        { 2, 3, 0, "named Child" },  { 1, 0, 1, "insertion sibling" },
        { 2, 0, 4, "destination" },  { 99, 0, 0, "selected object" },
        { 1, 99, 0, "destination" }, { 1, 0, 99, "destination" },
    };
    char error[256];
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        authoring_scene_node nodes[4];
        memcpy(nodes, original, sizeof nodes);
        REQUIRE(
            move(nodes, 4, &cases[i].selected, 1, cases[i].parent, cases[i].before, true, error) ==
            -1
        );
        REQUIRE(strstr(error, cases[i].reason));
        REQUIRE(!memcmp(nodes, original, sizeof nodes));
    }
    ecs_entity stale = { .slot = 1, .generation = 6 };
    REQUIRE(
        authoring_scene_move(original, 4, &stale, 1, (ecs_entity){ 0 }, (ecs_entity){ 0 }, true, error, sizeof error) ==
        -1
    );
    REQUIRE(strstr(error, "selected object"));
    REQUIRE(move(original, 4, NULL, 0, 0, 0, true, error) == -1);
    return true;
}

TEST_CASE(snapshot_validation_bounds_depth_names_and_identity)
{
    authoring_scene_node *nodes = NULL;
    for (int i = 0; i < SCENE_DEPTH_MAX; i++) {
        apush(nodes, object(i + 1, "Nested", i - 1));
    }
    apush(nodes, object(SCENE_DEPTH_MAX + 1, "Extra", -1));
    char error[256];
    REQUIRE(authoring_scene_validate(nodes, acount(nodes), error, sizeof error) == 0);
    REQUIRE(
        move(nodes, acount(nodes), (int[]){ SCENE_DEPTH_MAX + 1 }, 1, SCENE_DEPTH_MAX, 0, false, error) ==
        -1
    );
    REQUIRE(strstr(error, "depth"));
    REQUIRE(nodes[SCENE_DEPTH_MAX].parent == -1);
    nodes[1].entity = nodes[0].entity;
    REQUIRE(authoring_scene_validate(nodes, acount(nodes), error, sizeof error) == -1);
    nodes[1].entity = entity(2);
    nodes[0].parent = 1;
    REQUIRE(authoring_scene_validate(nodes, acount(nodes), error, sizeof error) == -1);
    nodes[0].parent = -1;
    nodes[0].name = "Not.A.Name";
    REQUIRE(authoring_scene_validate(nodes, acount(nodes), error, sizeof error) == -1);
    nodes[0].name = "Nested";
    nodes[0].has_transform_2d = true;
    nodes[0].position.x = NAN;
    REQUIRE(authoring_scene_validate(nodes, acount(nodes), error, sizeof error) == -1);
    afree(nodes);
    REQUIRE(authoring_scene_validate(NULL, 0, error, sizeof error) == 0);
    return true;
}

TEST_CASE(keep_world_compensates_transformless_2d_groups)
{
    authoring_scene_node nodes[] = { object(1, "Source", -1),
                                     object(2, "Group", 0),
                                     object(3, "Child", 1),
                                     object(4, "Grandchild", 2),
                                     object(5, "Target", -1) };
    nodes[0].has_transform_2d = nodes[0].has_orientation = true;
    nodes[0].position = (CF_V2){ 10, 20 };
    nodes[0].orientation.radians = QUARTER_TURN;
    nodes[2].has_transform_2d = nodes[3].has_transform_2d = nodes[4].has_transform_2d = true;
    nodes[2].position = (CF_V2){ 3, 4 };
    nodes[3].position = (CF_V2){ 1, 2 };
    nodes[4].position = (CF_V2){ 100, 0 };
    char error[256];
    REQUIRE(move(nodes, 5, (int[]){ 2, 3 }, 2, 5, 0, true, error) == 0);
    REQUIRE(nodes[2].entity.slot == 2 && !nodes[2].has_transform_2d);
    REQUIRE(
        nodes[3].entity.slot == 3 && near(nodes[3].position.x, -94) &&
        near(nodes[3].position.y, 23)
    );
    REQUIRE(nodes[3].has_orientation && near(nodes[3].orientation.radians, QUARTER_TURN));
    REQUIRE(nodes[4].position.x == 1 && nodes[4].position.y == 2 && !nodes[4].has_orientation);
    REQUIRE(move(nodes, 5, (int[]){ 2 }, 1, 1, 0, true, error) == 0);
    REQUIRE(
        nodes[2].entity.slot == 3 && near(nodes[2].position.x, 3) &&
        near(nodes[2].position.y, 4)
    );
    REQUIRE(near(nodes[2].orientation.radians, 0));
    return true;
}

static Transform3D pose(CF_V3 position, CF_V3 scale)
{
    return (Transform3D){ .position = position, .scale = scale, .rotation = { .w = 1 } };
}

static bool matrix_near(CF_M4x4 a, CF_M4x4 b)
{
    for (int i = 0; i < 16; i++) {
        if (!near(a.elements[i], b.elements[i])) {
            return false;
        }
    }
    return true;
}

static CF_M4x4 matrix(Transform3D t)
{
    return cf_m4_from_trs(t.position, t.rotation, t.scale);
}

TEST_CASE(keep_world_preserves_mirrored_3d_placement)
{
    authoring_scene_node nodes[] = { object(1, "Source", -1),
                                     object(2, "Group", 0),
                                     object(3, "Mesh", 1),
                                     object(4, "Target", -1) };
    nodes[0].has_transform_3d = nodes[2].has_transform_3d = nodes[3].has_transform_3d = true;
    nodes[0].transform_3d = pose((CF_V3){ 10, 20, 30 }, (CF_V3){ 2, 2, 2 });
    nodes[0].transform_3d.rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, QUARTER_TURN);
    nodes[2].transform_3d = pose((CF_V3){ 3, 4, 5 }, (CF_V3){ -1, 1, 1 });
    nodes[3].transform_3d = pose((CF_V3){ 2, 2, 2 }, (CF_V3){ 2, 2, 2 });
    CF_M4x4 before = cf_mul_m4(matrix(nodes[0].transform_3d), matrix(nodes[2].transform_3d));
    char error[256];
    REQUIRE(move(nodes, 4, (int[]){ 2 }, 1, 4, 0, true, error) == 0);
    REQUIRE(nodes[3].entity.slot == 3 && nodes[2].entity.slot == 2);
    REQUIRE(near(nodes[3].transform_3d.position.x, 0));
    REQUIRE(near(nodes[3].transform_3d.position.y, 12));
    REQUIRE(near(nodes[3].transform_3d.position.z, 19));
    REQUIRE(nodes[3].transform_3d.scale.x < 0);
    REQUIRE(matrix_near(
        before,
        cf_mul_m4(matrix(nodes[1].transform_3d), matrix(nodes[3].transform_3d))
    ));
    REQUIRE(move(nodes, 4, (int[]){ 2 }, 1, 0, 0, true, error) == 0);
    REQUIRE(matrix_near(before, matrix(nodes[3].transform_3d)));
    return true;
}

TEST_CASE(keep_world_rejects_singular_and_sheared_3d_atomically)
{
    authoring_scene_node original[] = { object(1, "A", -1),
                                        object(2, "B", -1),
                                        object(3, "Target", -1) };
    for (int i = 0; i < 3; i++) {
        original[i].has_transform_2d = original[i].has_transform_3d = true;
        original[i].position = (CF_V2){ (float)i, 5 };
        original[i].transform_3d = pose((CF_V3){ (float)i, 3, 4 }, (CF_V3){ 1, 1, 1 });
    }
    original[1].transform_3d.rotation = cf_quat_from_axis_angle((CF_V3){ 0, 0, 1 }, QUARTER_TURN * .5f);
    char error[256];
    for (int singular = 0; singular < 2; singular++) {
        original[2].transform_3d.scale.x = singular ? 0 : 2;
        authoring_scene_node nodes[3];
        memcpy(nodes, original, sizeof nodes);
        REQUIRE(move(nodes, 3, (int[]){ 1, 2 }, 2, 3, 0, true, error) == -1);
        REQUIRE(strstr(error, singular ? "singular" : "shear"));
        REQUIRE(!memcmp(nodes, original, sizeof nodes));
        REQUIRE(move(nodes, 3, (int[]){ 1, 2 }, 2, 3, 0, false, error) == 0);
        REQUIRE(!memcmp(&nodes[1].transform_3d, &original[0].transform_3d, sizeof(Transform3D)));
        REQUIRE(!memcmp(&nodes[2].transform_3d, &original[1].transform_3d, sizeof(Transform3D)));
        REQUIRE(nodes[1].position.x == original[0].position.x);
    }
    original[0].transform_3d.scale.x = 0;
    original[2].transform_3d.scale.x = 1;
    authoring_scene_node nodes[3];
    memcpy(nodes, original, sizeof nodes);
    REQUIRE(move(nodes, 3, (int[]){ 1 }, 1, 3, 0, true, error) == -1);
    REQUIRE(strstr(error, "singular"));
    REQUIRE(!memcmp(nodes, original, sizeof nodes));
    return true;
}

TEST_CASE(keep_world_rejects_finite_compensation_that_loses_position)
{
    authoring_scene_node original[] = { object(1, "Small", -1), object(2, "Far", -1) };
    const float positions[][2] = { { 1, 1e20f }, { 1e20f, 1e30f } };
    char error[256];
    for (int scenario = 0; scenario < 2; scenario++) {
        for (int i = 0; i < 2; i++) {
            float x = positions[scenario][i];
            original[i].position = (CF_V2){ x, 0 };
            original[i].transform_3d = pose((CF_V3){ x, 0, 0 }, (CF_V3){ 1, 1, 1 });
        }
        for (int spatial = 0; spatial < 2; spatial++) {
            authoring_scene_node nodes[2];
            for (int i = 0; i < 2; i++) {
                original[i].has_transform_2d = !spatial;
                original[i].has_transform_3d = spatial;
            }
            memcpy(nodes, original, sizeof nodes);
            REQUIRE(move(nodes, 2, (int[]){ 1 }, 1, 2, 0, true, error) == -1);
            REQUIRE(!memcmp(nodes, original, sizeof nodes));
        }
    }
    return true;
}

TEST_CASE(reorder_keeps_local_components_exactly_even_when_singular)
{
    authoring_scene_node nodes[] = { object(1, "Parent", -1),
                                     object(2, "A", 0),
                                     object(3, "B", 0) };
    for (int i = 0; i < 3; i++) {
        nodes[i].has_transform_3d = true;
        nodes[i].transform_3d = pose((CF_V3){ (float)i, 2, 3 }, (CF_V3){ 0, 0, 0 });
    }
    Transform3D a = nodes[1].transform_3d, b = nodes[2].transform_3d;
    char error[256];
    REQUIRE(move(nodes, 3, (int[]){ 3 }, 1, 1, 2, true, error) == 0);
    REQUIRE(nodes[1].entity.slot == 3 && nodes[2].entity.slot == 2);
    REQUIRE(!memcmp(&nodes[1].transform_3d, &b, sizeof b));
    REQUIRE(!memcmp(&nodes[2].transform_3d, &a, sizeof a));
    return true;
}

TEST_CASE(moved_order_and_paths_round_trip_through_scene_document)
{
    authoring_scene_node nodes[] = { object(1, "Old", -1),
                                     object(2, "Group", 0),
                                     object(3, "Leaf", 1),
                                     object(4, "New", -1),
                                     object(5, "First", 3) };
    char error[256];
    REQUIRE(move(nodes, 5, (int[]){ 2 }, 1, 4, 5, false, error) == 0);
    scene_document *document = scene_document_new("Hierarchy");
    for (int i = 0; i < 5; i++) {
        REQUIRE(
            scene_document_add_entity(document, nodes[i].parent, nodes[i].name, "Object") == 0
        );
    }
    char *text = scene_document_save(document, (ecs_component_catalog){ 0 });
    scene_document *loaded = scene_document_load(text, error, sizeof error);
    REQUIRE(loaded && acount(loaded->entities) == 5);
    REQUIRE(scene_document_entity_index(loaded, "Old.Group") == -1);
    REQUIRE(scene_document_entity_index(loaded, "New.Group.Leaf") == 3);
    REQUIRE(!strcmp(loaded->entities[4].name, "First"));
    char *path = scene_document_entity_path(loaded, 3);
    REQUIRE(!strcmp(path, "New.Group.Leaf"));
    free(path);
    free(text);
    scene_document_free(loaded);
    scene_document_free(document);
    return true;
}

static authoring_scene_owner *snapshot_owners[3];
static authoring_scene_snapshot *snapshots[3];
static authoring_scene_node *producer;
static char *producer_names[4];

static void free_producer()
{
    for (int i = 0; i < 4; i++) {
        free(producer_names[i]);
        producer_names[i] = NULL;
    }
    free(producer);
    producer = NULL;
}

static void free_snapshots()
{
    for (int i = 0; i < 3; i++) {
        if (snapshots[i]) {
            authoring_scene_snapshot_free(snapshots[i]);
            snapshots[i] = NULL;
        }
        if (snapshot_owners[i]) {
            authoring_scene_owner_free(snapshot_owners[i]);
            snapshot_owners[i] = NULL;
        }
    }
    free_producer();
}

static void make_producer()
{
    producer = xcalloc(2, sizeof *producer);
    producer_names[0] = xstrdup("Root");
    producer_names[1] = xstrdup("Child");
    producer[0] = object(1, producer_names[0], -1);
    producer[1] = object(2, producer_names[1], 0);
    producer[1].has_transform_2d = true;
    producer[1].position = (CF_V2){ 3, 4 };
}

TEST_CASE(owned_snapshot_preserves_order_identity_and_local_data)
{
    authoring_scene_node nodes[] = { object(1, "Root", -1),
                                     object(2, "Child", 0),
                                     object(3, "Grandchild", 1),
                                     object(4, "Other", -1) };
    nodes[0].entity = (ecs_entity){ .slot = 0xfedcba98u, .generation = 0x81234567u };
    nodes[1].entity = (ecs_entity){ .slot = 0xfedcba98u, .generation = 0x81234568u };
    nodes[0].has_transform_2d = nodes[0].has_orientation = true;
    nodes[0].position = (CF_V2){ 1, 2 };
    nodes[0].orientation.radians = QUARTER_TURN;
    nodes[2].has_transform_3d = true;
    nodes[2].transform_3d = pose((CF_V3){ 3, 4, 5 }, (CF_V3){ -2, 2, 2 });
    authoring_scene_owner *owner = snapshot_owners[0] = authoring_scene_owner_new();
    char error[256];
    snapshots[0] = authoring_scene_snapshot_new(owner, nodes, 4, error, sizeof error);
    REQUIRE(snapshots[0]);
    REQUIRE(authoring_scene_snapshot_current(snapshots[0], owner));
    REQUIRE(!authoring_scene_snapshot_current(snapshots[0], NULL));
    int count = -1;
    const authoring_scene_node *copy = authoring_scene_snapshot_nodes(snapshots[0], &count);
    REQUIRE(count == 4 && copy != nodes);
    for (int i = 0; i < count; i++) {
        REQUIRE(ecs_entity_to_u64(copy[i].entity) == ecs_entity_to_u64(nodes[i].entity));
        REQUIRE(copy[i].parent == nodes[i].parent);
        REQUIRE(copy[i].name != nodes[i].name && !strcmp(copy[i].name, nodes[i].name));
        REQUIRE(copy[i].has_transform_2d == nodes[i].has_transform_2d);
        REQUIRE(copy[i].has_orientation == nodes[i].has_orientation);
        REQUIRE(copy[i].has_transform_3d == nodes[i].has_transform_3d);
        REQUIRE(!memcmp(&copy[i].position, &nodes[i].position, sizeof(Transform)));
        REQUIRE(!memcmp(&copy[i].orientation, &nodes[i].orientation, sizeof(Orientation)));
        REQUIRE(!memcmp(&copy[i].transform_3d, &nodes[i].transform_3d, sizeof(Transform3D)));
    }
    snapshots[1] = authoring_scene_snapshot_new(owner, NULL, 0, error, sizeof error);
    REQUIRE(snapshots[1] && authoring_scene_snapshot_current(snapshots[1], owner));
    REQUIRE(authoring_scene_snapshot_nodes(snapshots[1], &count) == NULL && count == 0);
    return true;
}

TEST_CASE(owned_snapshot_survives_mutation_and_rejects_stale_generation)
{
    make_producer();
    authoring_scene_owner *owner = snapshot_owners[0] = authoring_scene_owner_new();
    char error[256];
    snapshots[0] = authoring_scene_snapshot_new(owner, producer, 2, error, sizeof error);
    REQUIRE(snapshots[0]);
    int count;
    const authoring_scene_node *old = authoring_scene_snapshot_nodes(snapshots[0], &count);
    authoring_scene_node expected[2];
    memcpy(expected, old, sizeof expected);
    authoring_scene_owner_changed(owner);
    strcpy(producer_names[0], "Next");
    strcpy(producer_names[1], "Moved");
    producer[1].entity = (ecs_entity){ .slot = 0xffffffffu, .generation = 0xfedcba98u };
    producer[1].parent = -1;
    producer[1].position = (CF_V2){ 7, 8 };
    REQUIRE(!authoring_scene_snapshot_current(snapshots[0], owner));
    REQUIRE(!memcmp(old, expected, sizeof expected));
    REQUIRE(!strcmp(old[0].name, "Root") && !strcmp(old[1].name, "Child"));
    snapshots[1] = authoring_scene_snapshot_new(owner, producer, 2, error, sizeof error);
    REQUIRE(snapshots[1] && authoring_scene_snapshot_current(snapshots[1], owner));
    const authoring_scene_node *next = authoring_scene_snapshot_nodes(snapshots[1], &count);
    REQUIRE(count == 2 && next != old && next != producer);
    REQUIRE(!strcmp(next[0].name, "Next") && !strcmp(next[1].name, "Moved"));
    REQUIRE(next[1].parent == -1);
    REQUIRE(ecs_entity_to_u64(next[1].entity) == ecs_entity_to_u64(producer[1].entity));
    REQUIRE(next[1].position.x == 7 && next[1].position.y == 8);
    REQUIRE(!authoring_scene_snapshot_current(snapshots[0], owner));
    return true;
}

TEST_CASE(owned_snapshot_outlives_producer_and_releases_in_either_order)
{
    for (int first = 0; first < 2; first++) {
        make_producer();
        authoring_scene_owner *owner = snapshot_owners[0] = authoring_scene_owner_new();
        char error[256];
        for (int i = 0; i < 2; i++) {
            snapshots[i] = authoring_scene_snapshot_new(owner, producer, 2, error, sizeof error);
            REQUIRE(snapshots[i]);
        }
        authoring_scene_owner_free(owner);
        snapshot_owners[0] = NULL;
        free_producer();
        int count;
        const authoring_scene_node *a = authoring_scene_snapshot_nodes(snapshots[0], &count);
        REQUIRE(count == 2 && !strcmp(a[1].name, "Child"));
        const authoring_scene_node *b = authoring_scene_snapshot_nodes(snapshots[1], &count);
        REQUIRE(count == 2 && a != b && a[1].name != b[1].name);
        REQUIRE(!strcmp(b[0].name, "Root"));
        REQUIRE(!authoring_scene_snapshot_current(snapshots[0], owner));
        REQUIRE(!authoring_scene_snapshot_current(snapshots[1], owner));
        authoring_scene_snapshot_free(snapshots[first]);
        snapshots[first] = NULL;
        const authoring_scene_node
            *remaining = authoring_scene_snapshot_nodes(snapshots[1 - first], &count);
        REQUIRE(count == 2 && remaining[1].parent == 0);
        REQUIRE(ecs_entity_to_u64(remaining[1].entity) == ecs_entity_to_u64(entity(2)));
        REQUIRE(!strcmp(remaining[1].name, "Child") && remaining[1].position.x == 3);
        REQUIRE(!authoring_scene_snapshot_current(snapshots[1 - first], owner));
        authoring_scene_snapshot_free(snapshots[1 - first]);
        snapshots[1 - first] = NULL;
        /* owner is no longer a valid identity after the last snapshot release. */
    }
    return true;
}

TEST_CASE(owned_snapshot_rejects_other_and_replacement_owners)
{
    authoring_scene_node node = object(1, "Root", -1);
    char error[256];
    for (int i = 0; i < 2; i++) {
        snapshot_owners[i] = authoring_scene_owner_new();
        snapshots[i] = authoring_scene_snapshot_new(snapshot_owners[i], &node, 1, error, sizeof error);
        REQUIRE(snapshots[i]);
        REQUIRE(authoring_scene_snapshot_current(snapshots[i], snapshot_owners[i]));
    }
    REQUIRE(!authoring_scene_snapshot_current(snapshots[0], snapshot_owners[1]));
    REQUIRE(!authoring_scene_snapshot_current(snapshots[1], snapshot_owners[0]));
    authoring_scene_owner *old = snapshot_owners[0];
    authoring_scene_owner_free(old);
    snapshot_owners[0] = NULL;
    snapshot_owners[2] = authoring_scene_owner_new();
    REQUIRE(old != snapshot_owners[2]);
    snapshots[2] = authoring_scene_snapshot_new(snapshot_owners[2], &node, 1, error, sizeof error);
    REQUIRE(snapshots[2]);
    REQUIRE(!authoring_scene_snapshot_current(snapshots[0], old));
    REQUIRE(!authoring_scene_snapshot_current(snapshots[0], snapshot_owners[2]));
    REQUIRE(authoring_scene_snapshot_current(snapshots[2], snapshot_owners[2]));
    return true;
}

TEST_CASE(owned_snapshot_rejects_invalid_capture_without_invalidating_prior_data)
{
    authoring_scene_node nodes[] = { object(1, "Root", -1), object(2, "Child", 0) };
    authoring_scene_owner *owner = snapshot_owners[0] = authoring_scene_owner_new();
    char error[256];
    snapshots[0] = authoring_scene_snapshot_new(owner, nodes, 2, error, sizeof error);
    REQUIRE(snapshots[0]);
    int count;
    const authoring_scene_node *old = authoring_scene_snapshot_nodes(snapshots[0], &count);
    authoring_scene_node expected[2];
    memcpy(expected, old, sizeof expected);
    for (int invalid = 0; invalid < 3; invalid++) {
        nodes[1] = object(2, "Child", 0);
        switch (invalid) {
            case 0: nodes[1].parent = 1; break;
            case 1: nodes[1].entity = nodes[0].entity; break;
            case 2: nodes[1].name = "Not.A.Name"; break;
        }
        snapshots[1] = authoring_scene_snapshot_new(owner, nodes, 2, error, sizeof error);
        REQUIRE(snapshots[1] == NULL && error[0]);
        REQUIRE(authoring_scene_snapshot_current(snapshots[0], owner));
        REQUIRE(!memcmp(old, expected, sizeof expected));
        REQUIRE(!strcmp(old[0].name, "Root") && !strcmp(old[1].name, "Child"));
    }
    return true;
}

void test_scene_hierarchy()
{
    RUN_TEST_CASE(move_groups_topmost_selection_in_scene_order);
    RUN_TEST_CASE(move_rejects_invalid_destinations_without_mutation);
    RUN_TEST_CASE(snapshot_validation_bounds_depth_names_and_identity);
    RUN_TEST_CASE(keep_world_compensates_transformless_2d_groups);
    RUN_TEST_CASE(keep_world_preserves_mirrored_3d_placement);
    RUN_TEST_CASE(keep_world_rejects_singular_and_sheared_3d_atomically);
    RUN_TEST_CASE(keep_world_rejects_finite_compensation_that_loses_position);
    RUN_TEST_CASE(reorder_keeps_local_components_exactly_even_when_singular);
    RUN_TEST_CASE(moved_order_and_paths_round_trip_through_scene_document);
    pu_setup(NULL, free_snapshots);
    RUN_TEST_CASE(owned_snapshot_preserves_order_identity_and_local_data);
    RUN_TEST_CASE(owned_snapshot_survives_mutation_and_rejects_stale_generation);
    RUN_TEST_CASE(owned_snapshot_outlives_producer_and_releases_in_either_order);
    RUN_TEST_CASE(owned_snapshot_rejects_other_and_replacement_owners);
    RUN_TEST_CASE(owned_snapshot_rejects_invalid_capture_without_invalidating_prior_data);
    pu_clear_setup();
}
