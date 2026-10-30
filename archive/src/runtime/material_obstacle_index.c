#include "runtime/material_obstacle_index.h"

#include "support/array.h"
#include "support/cos_sin.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>

enum
{
    OBSTACLE_CELL_LIMIT = 256,
    OBSTACLE_QUERY_CELL_LIMIT = 64
};

typedef struct
{
    fluid_vec low, high;
} obstacle_bounds;

typedef struct
{
    int x, y, obstacle, next;
} obstacle_reference;

struct material_obstacle_index
{
    const fluid_body *bodies;
    const fluid_obstacle *obstacles;
    int count;
    float cell_size;
    int *heads, *large;
    obstacle_reference *references;
    obstacle_bounds *bounds;
};

static fluid_vec sub(fluid_vec a, fluid_vec b)
{
    return (fluid_vec){ a.x - b.x, a.y - b.y };
}

static float dot(fluid_vec a, fluid_vec b)
{
    return a.x * b.x + a.y * b.y;
}

static fluid_vec rotate(fluid_vec p, float c, float s)
{
    return (fluid_vec){ c * p.x - s * p.y, s * p.x + c * p.y };
}

static obstacle_bounds segment_bounds(fluid_vec a, fluid_vec b)
{
    return (obstacle_bounds){ { fminf(a.x, b.x), fminf(a.y, b.y) },
                              { fmaxf(a.x, b.x), fmaxf(a.y, b.y) } };
}

static obstacle_bounds body_bounds(const fluid_body *body, fluid_obstacle obstacle)
{
    float reach = fabsf(obstacle.center.x) + fabsf(obstacle.center.y) +
                  (obstacle.radius > 0
                       ? obstacle.radius
                       : obstacle.half_size.x + obstacle.half_size.y);
    return (obstacle_bounds){ { body->position.x - reach, body->position.y - reach },
                              { body->position.x + reach, body->position.y + reach } };
}

static obstacle_bounds tight_bounds(const fluid_body *body, fluid_obstacle obstacle)
{
    cos_sin rotation = body->angle != 0 ? cos_sin_compute(body->angle)
                                        : (cos_sin){ .c = 1, .s = 0 };
    double c = rotation.c, s = rotation.s, norm = c * c + s * s;
    /* Invert the exact predicate's rotation, including its rounded norm. */
    double x = (c * obstacle.center.x - s * obstacle.center.y) / norm,
           y = (s * obstacle.center.x + c * obstacle.center.y) / norm;
    double ex = obstacle.radius > 0 ? obstacle.radius / sqrt(norm)
                                    : (fabs(c) * obstacle.half_size.x +
                                       fabs(s) * obstacle.half_size.y) /
                                          norm;
    double ey = obstacle.radius > 0 ? ex
                                    : (fabs(s) * obstacle.half_size.x +
                                       fabs(c) * obstacle.half_size.y) /
                                          norm;
    /* Cover cancellation and float rounding in the local-space predicate. */
    double px = 8 * FLT_EPSILON * (fabs(body->position.x) + fabs(x) + ex + 1),
           py = 8 * FLT_EPSILON * (fabs(body->position.y) + fabs(y) + ey + 1);
    x += body->position.x;
    y += body->position.y;
    return (obstacle_bounds){ { nextafterf((float)(x - ex - px), -INFINITY),
                                nextafterf((float)(y - ey - py), -INFINITY) },
                              { nextafterf((float)(x + ex + px), INFINITY),
                                nextafterf((float)(y + ey + py), INFINITY) } };
}

static bool overlaps(obstacle_bounds a, obstacle_bounds b)
{
    return a.high.x >= b.low.x && a.low.x <= b.high.x && a.high.y >= b.low.y &&
           a.low.y <= b.high.y;
}

static bool segment_box(fluid_vec a, fluid_vec b, fluid_vec half)
{
    fluid_vec d = sub(b, a);
    float first = .001f, last = .999f;
    float origin[] = { a.x, a.y }, delta[] = { d.x, d.y },
          extent[] = { half.x, half.y };
    for (int axis = 0; axis < 2; axis++) {
        if (fabsf(delta[axis]) < 1e-6f) {
            if (fabsf(origin[axis]) > extent[axis]) {
                return false;
            }
            continue;
        }
        float l = (-extent[axis] - origin[axis]) / delta[axis];
        float r = (extent[axis] - origin[axis]) / delta[axis];
        first = fmaxf(first, fminf(l, r));
        last = fminf(last, fmaxf(l, r));
        if (first > last) {
            return false;
        }
    }
    return true;
}

static bool obstacle_hit(
    const fluid_body *bodies,
    fluid_obstacle obstacle,
    fluid_vec a,
    fluid_vec b,
    int ignore_body,
    bool hot_solids
)
{
    const fluid_body *body = &bodies[obstacle.body];
    if (obstacle.body == ignore_body || (!hot_solids && body->heat > 0)) {
        return false;
    }
    float c = 1, s = 0;
    if (body->angle != 0) {
        cos_sin rotation = cos_sin_compute(body->angle);
        c = rotation.c;
        s = rotation.s;
    }
    fluid_vec from = sub(rotate(sub(a, body->position), c, -s), obstacle.center);
    fluid_vec to = sub(rotate(sub(b, body->position), c, -s), obstacle.center);
    if (obstacle.radius <= 0) {
        return segment_box(from, to, obstacle.half_size);
    }
    fluid_vec d = sub(to, from);
    float t = fmaxf(.001f, fminf(.999f, -dot(from, d) / fmaxf(dot(d, d), 1e-8f)));
    fluid_vec nearest = { from.x + d.x * t, from.y + d.y * t };
    return dot(nearest, nearest) < obstacle.radius * obstacle.radius;
}

bool material_obstacle_blocked(
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count,
    fluid_vec from,
    fluid_vec to,
    int ignore_body,
    bool hot_solids
)
{
    obstacle_bounds query = segment_bounds(from, to);
    for (int i = 0; i < count; i++) {
        if (overlaps(body_bounds(&bodies[obstacles[i].body], obstacles[i]), query) &&
            obstacle_hit(bodies, obstacles[i], from, to, ignore_body, hot_solids)) {
            return true;
        }
    }
    return false;
}

material_obstacle_index *material_obstacle_index_new()
{
    return xcalloc(1, sizeof(material_obstacle_index));
}

void material_obstacle_index_free(material_obstacle_index *index)
{
    assert(index);
    afree(index->heads);
    afree(index->large);
    afree(index->references);
    afree(index->bounds);
    free(index);
}

/* Coordinates outside integer cells and large ranges use the exact fallback. */
static bool cell_range(obstacle_bounds bounds, float size, int limit, int cells[4])
{
    double values[] = { floor((double)bounds.low.x / size),
                        floor((double)bounds.low.y / size),
                        floor((double)bounds.high.x / size),
                        floor((double)bounds.high.y / size) };
    for (int i = 0; i < 4; i++) {
        if (!isfinite(values[i]) || values[i] <= INT_MIN || values[i] >= INT_MAX) {
            return false;
        }
        cells[i] = (int)values[i];
    }
    return (values[2] - values[0] + 1) * (values[3] - values[1] + 1) <= limit;
}

static unsigned bucket(const material_obstacle_index *index, int x, int y)
{
    return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) &
           (unsigned)(acount(index->heads) - 1);
}

void material_obstacle_index_build(
    material_obstacle_index *index,
    const fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int count,
    float size
)
{
    assert(index && body_count >= 0 && count >= 0 && isfinite(size) && size > 0);
    (void)body_count;
    index->bodies = bodies;
    index->obstacles = obstacles;
    index->count = count;
    index->cell_size = size;
    aclear(index->large);
    aclear(index->references);
    asetlen(index->bounds, count);
    size_t references = 0;
    for (int i = 0; i < count; i++) {
        assert(obstacles[i].body >= 0 && obstacles[i].body < body_count);
        (void)body_count;
        index->bounds[i] = tight_bounds(&bodies[obstacles[i].body], obstacles[i]);
        int cells[4];
        if (cell_range(index->bounds[i], size, OBSTACLE_CELL_LIMIT, cells)) {
            references += (size_t)(cells[2] - cells[0] + 1) *
                          (cells[3] - cells[1] + 1);
        }
    }
    int buckets = 16;
    while ((size_t)buckets < references && buckets < 1 << 20) {
        buckets *= 2;
    }
    asetlen(index->heads, buckets);
    memset(index->heads, -1, (size_t)buckets * sizeof *index->heads);
    for (int i = 0; i < count; i++) {
        int cells[4];
        if (!cell_range(index->bounds[i], size, OBSTACLE_CELL_LIMIT, cells)) {
            apush(index->large, i);
            continue;
        }
        for (int y = cells[1]; y <= cells[3]; y++) {
            for (int x = cells[0]; x <= cells[2]; x++) {
                unsigned h = bucket(index, x, y);
                apush(index->references, ((obstacle_reference){ x, y, i, index->heads[h] }));
                index->heads[h] = acount(index->references) - 1;
            }
        }
    }
}

static bool indexed_hit(
    const material_obstacle_index *index,
    int obstacle,
    obstacle_bounds query,
    fluid_vec from,
    fluid_vec to,
    int ignore_body,
    bool hot_solids
)
{
    return overlaps(index->bounds[obstacle], query) &&
           obstacle_hit(index->bodies, index->obstacles[obstacle], from, to, ignore_body, hot_solids);
}

bool material_obstacle_index_blocked(
    const material_obstacle_index *index,
    fluid_vec from,
    fluid_vec to,
    int ignore_body,
    bool hot_solids
)
{
    assert(index && index->cell_size > 0);
    obstacle_bounds query = segment_bounds(from, to);
    int cells[4];
    if (!cell_range(query, index->cell_size, OBSTACLE_QUERY_CELL_LIMIT, cells)) {
        return material_obstacle_blocked(index->bodies, index->obstacles, index->count, from, to, ignore_body, hot_solids);
    }
    for (int i = 0; i < acount(index->large); i++) {
        if (indexed_hit(index, index->large[i], query, from, to, ignore_body, hot_solids)) {
            return true;
        }
    }
    for (int y = cells[1]; y <= cells[3]; y++) {
        for (int x = cells[0]; x <= cells[2]; x++) {
            for (int i = index->heads[bucket(index, x, y)]; i >= 0;
                 i = index->references[i].next) {
                obstacle_reference r = index->references[i];
                if (r.x == x && r.y == y &&
                    indexed_hit(index, r.obstacle, query, from, to, ignore_body, hot_solids)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static int compare_obstacles(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

void material_obstacle_index_query(
    const material_obstacle_index *index,
    fluid_vec position,
    float radius,
    int **obstacles
)
{
    assert(index && index->cell_size > 0 && isfinite(radius) && radius >= 0);
    obstacle_bounds query = { { position.x - radius, position.y - radius },
                              { position.x + radius, position.y + radius } };
    aclear(*obstacles);
    int cells[4];
    if (!cell_range(query, index->cell_size, OBSTACLE_QUERY_CELL_LIMIT, cells)) {
        for (int i = 0; i < index->count; i++) {
            if (overlaps(index->bounds[i], query)) {
                apush(*obstacles, i);
            }
        }
        return;
    }
    for (int i = 0; i < acount(index->large); i++) {
        int obstacle = index->large[i];
        if (overlaps(index->bounds[obstacle], query)) {
            apush(*obstacles, obstacle);
        }
    }
    for (int y = cells[1]; y <= cells[3]; y++) {
        for (int x = cells[0]; x <= cells[2]; x++) {
            for (int i = index->heads[bucket(index, x, y)]; i >= 0;
                 i = index->references[i].next) {
                obstacle_reference r = index->references[i];
                if (r.x == x && r.y == y && overlaps(index->bounds[r.obstacle], query)) {
                    apush(*obstacles, r.obstacle);
                }
            }
        }
    }
    int count = acount(*obstacles);
    if (!count) {
        return;
    }
    qsort(*obstacles, (size_t)count, sizeof **obstacles, compare_obstacles);
    int unique = 1;
    for (int i = 1; i < count; i++) {
        if ((*obstacles)[i] != (*obstacles)[unique - 1]) {
            (*obstacles)[unique++] = (*obstacles)[i];
        }
    }
    asetlen(*obstacles, unique);
}
