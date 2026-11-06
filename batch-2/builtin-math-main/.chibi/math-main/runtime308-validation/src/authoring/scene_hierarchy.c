#include "authoring/scene_hierarchy.h"

#include "assets/scene_document.h"
#include "support/array.h"
#include "support/cos_sin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Relative matrix residual allowed after reconstructing a local TRS. */
#define TRANSFORM_EPSILON 0.00001f

static int fail(char *error, size_t size, const char *reason)
{
    if (size) {
        snprintf(error, size, "%s", reason);
    }
    return -1;
}

static bool same(ecs_entity a, ecs_entity b)
{
    return ecs_entity_to_u64(a) == ecs_entity_to_u64(b);
}

static int find(const authoring_scene_node *nodes, int count, ecs_entity entity)
{
    if (!entity.slot && !entity.generation) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (same(nodes[i].entity, entity)) {
            return i;
        }
    }
    return -2;
}

static bool finite_3d(CF_V3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool finite_transform(const authoring_scene_node *node)
{
    if (node->has_transform_2d &&
        (!isfinite(node->position.x) || !isfinite(node->position.y))) {
        return false;
    }
    if (node->has_orientation && !isfinite(node->orientation.radians)) {
        return false;
    }
    if (!node->has_transform_3d) {
        return true;
    }
    const Transform3D *t = &node->transform_3d;
    return finite_3d(t->position) && finite_3d(t->scale) &&
           isfinite(t->rotation.x) && isfinite(t->rotation.y) &&
           isfinite(t->rotation.z) && isfinite(t->rotation.w);
}

static int validate(const authoring_scene_node *nodes, int count, bool ordered, char *error, size_t size)
{
    if (count < 0 || (count && !nodes) || (size && !error)) {
        return -1;
    }
    if (size) {
        error[0] = '\0';
    }
    for (int i = 0; i < count; i++) {
        const authoring_scene_node *node = &nodes[i];
        if (!node->entity.slot || node->parent < -1 || node->parent >= count ||
            (ordered && node->parent >= i)) {
            return fail(error, size, "Objects need valid identities and parents before their children.");
        }
        if (scene_entity_validate_name(node->name, false, error, size)) {
            return -1;
        }
        for (int j = 0; j < i; j++) {
            if (same(node->entity, nodes[j].entity)) {
                return fail(error, size, "An object occurs more than once in the scene.");
            }
            if (node->parent == nodes[j].parent &&
                scene_entity_validate_name(
                    node->name,
                    !strcmp(node->name, nodes[j].name),
                    error,
                    size
                )) {
                return -1;
            }
        }
        int depth = 0;
        for (int at = i; at >= 0; at = nodes[at].parent) {
            if (nodes[at].parent < -1 || nodes[at].parent >= count ||
                ++depth > SCENE_DEPTH_MAX) {
                return fail(error, size, "This move would exceed the scene hierarchy depth limit or create a cycle.");
            }
        }
        if (!finite_transform(node)) {
            return fail(error, size, "Object transforms must contain finite values.");
        }
    }
    return 0;
}

int authoring_scene_validate(const authoring_scene_node *nodes, int count, char *error, size_t size)
{
    return validate(nodes, count, true, error, size);
}

static int transform_parent(const authoring_scene_node *nodes, int index, bool spatial)
{
    int parent = nodes[index].parent;
    while (parent >= 0 && !(spatial ? nodes[parent].has_transform_3d
                                    : nodes[parent].has_transform_2d)) {
        parent = nodes[parent].parent;
    }
    return parent;
}

static GlobalTransform2D world_2d(const authoring_scene_node *nodes, int index)
{
    if (index < 0) {
        return (GlobalTransform2D){ 0 };
    }
    const authoring_scene_node *node = &nodes[index];
    GlobalTransform2D parent = world_2d(nodes, transform_parent(nodes, index, false));
    cos_sin turn = cos_sin_compute(parent.radians);
    return (GlobalTransform2D){
        .position = { parent.position.x + turn.c * node->position.x -
                          turn.s * node->position.y,
                      parent.position.y + turn.s * node->position.x +
                          turn.c * node->position.y },
        .radians = parent.radians +
                   (node->has_orientation ? node->orientation.radians : 0),
    };
}

static CF_M4x4 world_3d(const authoring_scene_node *nodes, int index)
{
    if (index < 0) {
        return cf_m4_identity();
    }
    const Transform3D *t = &nodes[index].transform_3d;
    CF_Quat q = t->rotation;
    q = q.x || q.y || q.z || q.w ? cf_quat_norm(q) : cf_quat_identity();
    CF_M4x4 local = cf_m4_from_trs(t->position, q, t->scale);
    return cf_mul_m4(world_3d(nodes, transform_parent(nodes, index, true)), local);
}

static CF_V3 column(CF_M4x4 m, int index)
{
    return (CF_V3){ m.elements[index * 4],
                    m.elements[index * 4 + 1],
                    m.elements[index * 4 + 2] };
}

static bool nonsingular(CF_M4x4 matrix)
{
    CF_V3 axes[3];
    for (int i = 0; i < 3; i++) {
        CF_V3 axis = column(matrix, i);
        float length = cf_len_v3(axis);
        if (!isfinite(length) || !length) {
            return false;
        }
        axes[i] = cf_div_v3_f(axis, length);
    }
    return fabsf(cf_dot_v3(cf_cross_v3(axes[0], axes[1]), axes[2])) > TRANSFORM_EPSILON;
}

static bool matches(CF_M4x4 a, CF_M4x4 b)
{
    for (int col = 0; col < 4; col++) {
        CF_V3 v = column(a, col);
        float magnitude = fmaxf(fmaxf(fabsf(v.x), fabsf(v.y)), fmaxf(fabsf(v.z), 1));
        for (int row = 0; row < 4; row++) {
            int at = col * 4 + row;
            if (!isfinite(a.elements[at]) || !isfinite(b.elements[at]) ||
                fabsf(a.elements[at] - b.elements[at]) > TRANSFORM_EPSILON * magnitude) {
                return false;
            }
        }
    }
    return true;
}

static int preserve_3d(authoring_scene_node *node, CF_M4x4 world, CF_M4x4 parent, char *error, size_t size)
{
    if (!nonsingular(parent)) {
        return fail(error, size, "Cannot preserve world placement below a singular 3D transform.");
    }
    CF_M4x4 inverse = cf_m4_invert(parent);
    CF_M4x4 local = cf_mul_m4(inverse, world);
    Transform3D *t = &node->transform_3d;
    if (!matches(cf_mul_m4(parent, inverse), cf_m4_identity()) || !nonsingular(local)) {
        return fail(error, size, "Cannot preserve a singular or numerically unstable 3D placement.");
    }
    cf_m4_decompose(local, &t->position, &t->rotation, &t->scale);
    CF_M4x4 reconstructed = cf_m4_from_trs(t->position, t->rotation, t->scale);
    if (!matches(local, reconstructed)) {
        return fail(error, size, "Preserving world placement would require 3D shear.");
    }
    if (!matches(world, cf_mul_m4(parent, reconstructed))) {
        return fail(error, size, "Cannot preserve a numerically unstable 3D placement.");
    }
    return 0;
}

static int preserve_2d(
    authoring_scene_node *node,
    GlobalTransform2D world,
    GlobalTransform2D parent,
    char *error,
    size_t size
)
{
    cos_sin turn = cos_sin_compute(parent.radians);
    CF_V2 delta = cf_sub_v2(world.position, parent.position);
    node->position = (CF_V2){ turn.c * delta.x + turn.s * delta.y,
                              -turn.s * delta.x + turn.c * delta.y };
    node->orientation.radians = world.radians - parent.radians;
    node->has_orientation = node->has_orientation || node->orientation.radians != 0;
    float x = parent.position.x + turn.c * node->position.x -
              turn.s * node->position.y;
    float y = parent.position.y + turn.s * node->position.x +
              turn.c * node->position.y;
    float angle = parent.radians + node->orientation.radians;
    if (!isfinite(x) || !isfinite(y) || !isfinite(angle) ||
        fabsf(x - world.position.x) >
            TRANSFORM_EPSILON * fmaxf(fabsf(world.position.x), 1) ||
        fabsf(y - world.position.y) >
            TRANSFORM_EPSILON * fmaxf(fabsf(world.position.y), 1) ||
        fabsf(angle - world.radians) >
            TRANSFORM_EPSILON * fmaxf(fabsf(world.radians), 1)) {
        return fail(error, size, "Cannot preserve a non-finite or numerically unstable 2D placement.");
    }
    return 0;
}

/* Every world pose stays unchanged, so a new transform ancestor's old world
 * pose is also its final world pose. Only changed transform-parent edges need
 * compensation. This includes transforms below a moved transformless group. */
static int preserve_world(
    authoring_scene_node *next,
    const authoring_scene_node *old,
    int count,
    char *error,
    size_t size
)
{
    for (int i = 0; i < count; i++) {
        authoring_scene_node *node = &next[i];
        int was = find(old, count, node->entity);
        for (int spatial = 0; spatial < 2; spatial++) {
            if (!(spatial ? node->has_transform_3d : node->has_transform_2d)) {
                continue;
            }
            int parent = transform_parent(next, i, spatial);
            int old_parent = parent < 0 ? -1 : find(old, count, next[parent].entity);
            if (old_parent == transform_parent(old, was, spatial)) {
                continue;
            }
            if (spatial) {
                if (preserve_3d(node, world_3d(old, was), world_3d(old, old_parent), error, size)) {
                    return -1;
                }
                continue;
            }
            if (preserve_2d(node, world_2d(old, was), world_2d(old, old_parent), error, size)) {
                return -1;
            }
        }
    }
    return authoring_scene_validate(next, count, error, size);
}

typedef struct
{
    const authoring_scene_node *nodes;
    const bool *moved;
    int count, destination, before;
    authoring_scene_node *result;
} move_writer;

static void write_children(move_writer *writer, int parent, int result_parent);

static void write_node(move_writer *writer, int index, int parent)
{
    authoring_scene_node node = writer->nodes[index];
    node.parent = parent;
    int at = acount(writer->result);
    apush(writer->result, node);
    write_children(writer, index, at);
}

static void write_moved(move_writer *writer, int parent)
{
    for (int i = 0; i < writer->count; i++) {
        if (writer->moved[i]) {
            write_node(writer, i, parent);
        }
    }
}

static void write_children(move_writer *writer, int parent, int result_parent)
{
    for (int i = 0; i < writer->count; i++) {
        if (writer->nodes[i].parent != parent || writer->moved[i]) {
            continue;
        }
        if (parent == writer->destination && i == writer->before) {
            write_moved(writer, result_parent);
        }
        write_node(writer, i, result_parent);
    }
    if (parent == writer->destination && writer->before == -1) {
        write_moved(writer, result_parent);
    }
}

int authoring_scene_move(
    authoring_scene_node *nodes,
    int count,
    const ecs_entity *selected,
    int selected_count,
    ecs_entity parent,
    ecs_entity before,
    bool keep_world,
    char *error,
    size_t size
)
{
    if (authoring_scene_validate(nodes, count, error, size)) {
        return -1;
    }
    if (!count || selected_count <= 0 || !selected) {
        return fail(error, size, "Select at least one object to move.");
    }
    int destination = find(nodes, count, parent), anchor = find(nodes, count, before);
    if (destination < -1 || anchor < -1 ||
        (anchor >= 0 && nodes[anchor].parent != destination)) {
        return fail(error, size, "The destination or insertion sibling is no longer valid.");
    }
    bool *moved = NULL;
    asetlen(moved, count);
    memset(moved, 0, (size_t)count * sizeof *moved);
    int result = -1;
    authoring_scene_node *candidate = NULL;
    for (int i = 0; i < selected_count; i++) {
        int at = find(nodes, count, selected[i]);
        if (at < 0) {
            fail(error, size, "A selected object is no longer in the scene.");
            goto done;
        }
        moved[at] = true;
    }
    /* Reverse order keeps each selected ancestor marked until all of its
     * descendants have been removed from the move roots. */
    for (int i = count - 1; i >= 0; i--) {
        for (int at = nodes[i].parent; moved[i] && at >= 0; at = nodes[at].parent) {
            if (moved[at]) {
                moved[i] = false;
            }
        }
    }
    for (int at = destination; at >= 0; at = nodes[at].parent) {
        if (moved[at]) {
            fail(error, size, "An object cannot be moved below itself or one of its descendants.");
            goto done;
        }
    }
    if (anchor >= 0 && moved[anchor]) {
        fail(error, size, "Choose an insertion sibling outside the moved objects.");
        goto done;
    }
    asetlen(candidate, count);
    memcpy(candidate, nodes, (size_t)count * sizeof *candidate);
    for (int i = 0; i < count; i++) {
        if (moved[i]) {
            candidate[i].parent = destination;
        }
    }
    if (validate(candidate, count, false, error, size)) {
        goto done;
    }
    move_writer writer = { .nodes = nodes,
                           .moved = moved,
                           .count = count,
                           .destination = destination,
                           .before = anchor };
    write_children(&writer, -1, -1);
    afree(candidate);
    candidate = writer.result;
    if (keep_world && preserve_world(candidate, nodes, count, error, size)) {
        goto done;
    }
    memcpy(nodes, candidate, (size_t)count * sizeof *nodes);
    result = 0;
done:
    afree(candidate);
    afree(moved);
    return result;
}
