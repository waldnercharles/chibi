#include "runtime/material_debris.h"

#include "support/array.h"
#include "support/exponential.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

/* Per-second velocity and spin damping, bounce, and sliding friction. */
static const float DRAG = .45f, SPIN_DRAG = 1.2f, RESTITUTION = .45f, FRICTION = .3f;
/* A velocity change above SPLIT_SHOCK splits chunks at least SPLIT_RADIUS
 * across, up to SPLIT_GENERATIONS times. Chunks younger than SPLIT_AGE are
 * still leaving their crater and hold together. */
static const float SPLIT_SHOCK = 45, SPLIT_RADIUS = .9f, SPLIT_AGE = .1f;
static const int SPLIT_GENERATIONS = 2;
/* Longest time a chunk spends shrinking away; heat lost per second. */
static const float SHRINK = .5f, COOLING = 2.5f;

struct material_debris
{
    material_chunk *chunks;
    /* Scratch: pieces from this tick's splits, and chunks that expired. */
    material_chunk *pieces, *expired;
    int capacity;
};

static float hash_unit(unsigned seed, int salt)
{
    unsigned h = seed ^ (unsigned)salt * 0x9e3779b9u;
    h = (h ^ (h >> 15)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12)) * 0x297a2d39u;
    return ((h ^ (h >> 15)) & 0xffffff) / 16777216.0f;
}

void material_chunk_shape(material_chunk *chunk, unsigned seed)
{
    assert(chunk->radius > 0);
    chunk->count = 5 + (int)(hash_unit(seed, 1) * 3);
    float turn = 6.2831853f / chunk->count;
    float start = hash_unit(seed, 2) * turn;
    for (int k = 0; k < chunk->count; k++) {
        float angle = start + turn * (k + (hash_unit(seed, 10 + k) - .5f) * .6f);
        float reach = chunk->radius * (.62f + .38f * hash_unit(seed, 20 + k));
        chunk->points[2 * k] = cosf(angle) * reach;
        chunk->points[2 * k + 1] = sinf(angle) * reach;
    }
}

material_debris *material_debris_new(int capacity)
{
    assert(capacity > 0);
    material_debris *debris = xcalloc(1, sizeof *debris);
    debris->capacity = capacity;
    asetcap(debris->chunks, capacity);
    return debris;
}

void material_debris_free(material_debris *debris)
{
    if (!debris) {
        return;
    }
    afree(debris->chunks);
    afree(debris->pieces);
    afree(debris->expired);
    free(debris);
}

void material_debris_clear(material_debris *debris)
{
    aclear(debris->chunks);
    aclear(debris->expired);
}

int material_debris_capacity(const material_debris *debris)
{
    return debris->capacity;
}

void material_debris_add(material_debris *debris, material_chunk chunk)
{
    assert(chunk.count >= 3 && chunk.count <= MATERIAL_CHUNK_POINTS && chunk.radius > 0);
    assert(chunk.lifetime > 0);
    if (acount(debris->chunks) < debris->capacity) {
        apush(debris->chunks, chunk);
        return;
    }
    int oldest = 0;
    for (int i = 1; i < acount(debris->chunks); i++) {
        if (debris->chunks[i].age > debris->chunks[oldest].age) {
            oldest = i;
        }
    }
    debris->chunks[oldest] = chunk;
}

const material_chunk *material_debris_chunks(const material_debris *debris, int *count)
{
    *count = acount(debris->chunks);
    return debris->chunks;
}

const material_chunk *material_debris_expired(const material_debris *debris, int *count)
{
    *count = acount(debris->expired);
    return debris->expired;
}

float material_chunk_scale(const material_chunk *chunk)
{
    float shrink = fminf(SHRINK, chunk->lifetime * .4f);
    return fminf(1, fmaxf(0, (chunk->lifetime - chunk->age) / shrink));
}

static void kick(material_chunk *chunk, fluid_vec change)
{
    chunk->velocity.x += change.x;
    chunk->velocity.y += change.y;
    chunk->shock = fmaxf(chunk->shock, hypotf(change.x, change.y));
}

static void push(material_chunk *chunk, material_debris_blast blast)
{
    float dx = chunk->position.x - blast.position.x,
          dy = chunk->position.y - blast.position.y;
    float length = hypotf(dx, dy);
    if (length < blast.radius && length > 1e-3f) {
        float speed = blast.speed * (1 - length / blast.radius) / length;
        kick(chunk, (fluid_vec){ dx * speed, dy * speed });
    }
}

void material_debris_push(material_debris *debris, material_debris_blast blast)
{
    for (int i = 0; i < acount(debris->chunks); i++) {
        push(&debris->chunks[i], blast);
    }
}

static fluid_vec rotate(fluid_vec v, float angle)
{
    float c = cosf(angle), s = sinf(angle);
    return (fluid_vec){ c * v.x - s * v.y, s * v.x + c * v.y };
}

/* Bounce and slide off a surface with outward normal n at depth overlap. */
static void contact(material_chunk *chunk, fluid_vec n, float overlap, fluid_vec surface_velocity)
{
    chunk->position.x += n.x * overlap;
    chunk->position.y += n.y * overlap;
    fluid_vec v = { chunk->velocity.x - surface_velocity.x,
                    chunk->velocity.y - surface_velocity.y };
    float vn = v.x * n.x + v.y * n.y;
    if (vn >= 0) {
        return;
    }
    fluid_vec tangent = { v.x - vn * n.x, v.y - vn * n.y };
    chunk->shock = fmaxf(chunk->shock, -(1 + RESTITUTION) * vn);
    v.x -= (1 + RESTITUTION) * vn * n.x + FRICTION * tangent.x;
    v.y -= (1 + RESTITUTION) * vn * n.y + FRICTION * tangent.y;
    /* Sliding contact spins the chunk as if it rolled. */
    float roll = (n.x * tangent.y - n.y * tangent.x) / chunk->radius;
    chunk->spin += (roll - chunk->spin) * .5f;
    chunk->velocity = (fluid_vec){ v.x + surface_velocity.x,
                                   v.y + surface_velocity.y };
}

static void collide_bodies(
    material_chunk *chunk,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count
)
{
    for (int i = 0; i < count; i++) {
        fluid_obstacle o = obstacles[i];
        fluid_body b = bodies[o.body];
        if (b.inverse_mass <= 0) {
            continue;
        }
        fluid_vec local = rotate(
            (fluid_vec){ chunk->position.x - b.position.x,
                         chunk->position.y - b.position.y },
            -b.angle
        );
        local.x -= o.center.x;
        local.y -= o.center.y;
        fluid_vec n;
        float d;
        if (o.radius > 0) {
            float length = hypotf(local.x, local.y);
            d = length - o.radius;
            n = length > 1e-5f ? (fluid_vec){ local.x / length, local.y / length }
                               : (fluid_vec){ 1, 0 };
        } else {
            float qx = fabsf(local.x) - o.half_size.x,
                  qy = fabsf(local.y) - o.half_size.y;
            d = hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0);
            n = qx > qy ? (fluid_vec){ local.x < 0 ? -1.f : 1.f, 0 }
                        : (fluid_vec){ 0, local.y < 0 ? -1.f : 1.f };
        }
        if (d < chunk->radius) {
            contact(chunk, rotate(n, b.angle), chunk->radius - d, b.velocity);
        }
    }
}

static void collide_solid(material_chunk *chunk, material_debris_distance *distance, void *user)
{
    float d;
    if (!distance(user, chunk->position, &d) || d >= chunk->radius) {
        return;
    }
    const float h = 1;
    float px = d, nx = d, py = d, ny = d;
    fluid_vec p = chunk->position;
    int known = distance(user, (fluid_vec){ p.x + h, p.y }, &px) +
                distance(user, (fluid_vec){ p.x - h, p.y }, &nx) +
                distance(user, (fluid_vec){ p.x, p.y + h }, &py) +
                distance(user, (fluid_vec){ p.x, p.y - h }, &ny);
    fluid_vec n = { px - nx, py - ny };
    float length = hypotf(n.x, n.y);
    if (known < 4 || length < 1e-5f) {
        /* No usable gradient: back out along the direction of travel. */
        length = hypotf(chunk->velocity.x, chunk->velocity.y);
        if (length < 1e-5f) {
            return;
        }
        n = (fluid_vec){ -chunk->velocity.x, -chunk->velocity.y };
    }
    n.x /= length;
    n.y /= length;
    contact(chunk, n, fminf(chunk->radius - d, chunk->radius * 2), (fluid_vec){ 0 });
}

/* Two or three smaller pieces fly apart from where the chunk was; the pattern
 * of its material stays in place across them. */
static void split(material_debris *debris, const material_chunk *chunk)
{
    unsigned seed = (unsigned)(int)floorf(chunk->position.x * 16) * 0x27d4eb2du ^
                    (unsigned)(int)floorf(chunk->position.y * 16) * 0x165667b1u ^
                    (unsigned)chunk->generation * 0x9e3779b9u;
    int count = 2 + (hash_unit(seed, 0) > .5f);
    float remaining = chunk->lifetime - chunk->age;
    float c = cosf(chunk->angle), s = sinf(chunk->angle);
    float start = hash_unit(seed, 1) * 6.2831853f;
    for (int i = 0; i < count; i++) {
        float direction = start + 6.2831853f * i / count;
        fluid_vec local = { cosf(direction) * chunk->radius * .45f,
                            sinf(direction) * chunk->radius * .45f };
        fluid_vec out = { c * local.x - s * local.y, s * local.x + c * local.y };
        float speed = 12 + 18 * hash_unit(seed, 2 + i);
        float length = chunk->radius * .45f;
        material_chunk piece = {
            .position = { chunk->position.x + out.x, chunk->position.y + out.y },
            .velocity = { chunk->velocity.x + out.x / length * speed,
                          chunk->velocity.y + out.y / length * speed },
            .angle = chunk->angle,
            .spin = chunk->spin + (hash_unit(seed, 6 + i) - .5f) * 10,
            .radius = chunk->radius / sqrtf((float)count),
            .origin = { chunk->origin.x + local.x, chunk->origin.y + local.y },
            .source = chunk->source,
            .generation = chunk->generation + 1,
            .heat = chunk->heat,
            .lifetime = fmaxf(.4f, remaining * (.7f + .3f * hash_unit(seed, 10 + i))),
        };
        material_chunk_shape(&piece, seed ^ (unsigned)(i + 1) * 0x85ebca6bu);
        apush(debris->pieces, piece);
    }
}

void material_debris_tick(
    material_debris *debris,
    float delta,
    material_debris_distance *distance,
    void *user,
    const fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    const material_debris_blast *blasts,
    int blast_count
)
{
    assert(delta > 0 && body_count >= 0);
    (void)body_count;
    float drag = deterministic_expf(-DRAG * delta),
          spin_drag = deterministic_expf(-SPIN_DRAG * delta);
    float cooling = deterministic_expf(-COOLING * delta);
    aclear(debris->expired);
    aclear(debris->pieces);
    for (int i = acount(debris->chunks) - 1; i >= 0; i--) {
        material_chunk *chunk = &debris->chunks[i];
        chunk->age += delta;
        if (chunk->age >= chunk->lifetime) {
            apush(debris->expired, *chunk);
            adel(debris->chunks, i);
            continue;
        }
        for (int b = 0; b < blast_count; b++) {
            push(chunk, blasts[b]);
        }
        chunk->velocity.x *= drag;
        chunk->velocity.y *= drag;
        chunk->spin *= spin_drag;
        chunk->heat *= cooling;
        chunk->position.x += chunk->velocity.x * delta;
        chunk->position.y += chunk->velocity.y * delta;
        chunk->angle += chunk->spin * delta;
        collide_bodies(chunk, bodies, obstacles, obstacle_count);
        if (distance) {
            collide_solid(chunk, distance, user);
        }
        if (chunk->shock > SPLIT_SHOCK && chunk->radius >= SPLIT_RADIUS &&
            chunk->generation < SPLIT_GENERATIONS && chunk->age > SPLIT_AGE) {
            split(debris, chunk);
            adel(debris->chunks, i);
            continue;
        }
        chunk->shock = 0;
    }
    for (int i = 0; i < acount(debris->pieces); i++) {
        material_debris_add(debris, debris->pieces[i]);
    }
}
