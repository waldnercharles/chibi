#include "physics.h"

#include <assert.h>
#include <box2d/box2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec * 1e-6;
}
static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    int mode = atoi(argv[1]), n = atoi(argv[2]), frames = atoi(argv[3]);
    assert(n > 0 && n <= 20000 && frames > 0);
    PhysWorld *neon = NULL;
    b2WorldId box = { 0 };
    PhysBodyId *nid = calloc(n, sizeof *nid);
    b2BodyId *bid = calloc(n, sizeof *bid);
    double *samples = calloc(frames, sizeof *samples);
    assert(nid && bid && samples);
    if (!mode) {
        neon = phys_world_create((PhysWorldDef){ .initial_capacity = n,
                                                 .default_stiffness = 500,
                                                 .default_damping = 10 });
    } else {
        b2SetLengthUnitsPerMeter(32);
        b2WorldDef d = b2DefaultWorldDef();
        d.gravity = (b2Vec2){ 0 };
        box = b2CreateWorld(&d);
    }
    int columns = (int)ceilf(sqrtf(n));
    for (int i = 0; i < n; i++) {
        float x = ((i % columns) - columns * .5f) * 22,
              y = ((i / columns) - columns * .5f) * 22;
        if (!mode) {
            nid[i] = phys_body_create(
                neon,
                (PhysBodyDef){ .type = PHYS_BODY_DYNAMIC,
                               .pos = { x, y },
                               .radius = 10,
                               .mass = 1,
                               .category = 1,
                               .mask = 1,
                               .track_contacts = true }
            );
        } else {
            b2BodyDef d = b2DefaultBodyDef();
            d.type = b2_dynamicBody;
            d.position = (b2Vec2){ x, y };
            d.fixedRotation = true;
            bid[i] = b2CreateBody(box, &d);
            b2ShapeDef s = b2DefaultShapeDef();
            s.material.friction = 0;
            s.density = 1.0f / (3.14159265f * 100);
            s.enableContactEvents = true;
            b2Circle c = { .radius = 10 };
            b2CreateCircleShape(bid[i], &s, &c);
        }
    }
    for (int frame = -30; frame < frames; frame++) {
        for (int i = 0; i < n; i++) {
            float x, y;
            if (!mode) {
                PhysV2 p = phys_body_get_pos(neon, nid[i]);
                x = p.x;
                y = p.y;
            } else {
                b2Vec2 p = b2Body_GetPosition(bid[i]);
                x = p.x;
                y = p.y;
            }
            float d = sqrtf(x * x + y * y), k = d > 0 ? -55 / d : 0;
            if (!mode) {
                phys_body_set_vel(neon, nid[i], (PhysV2){ k * x, k * y });
            } else {
                b2Body_SetLinearVelocity(bid[i], (b2Vec2){ k * x, k * y });
            }
        }
        double start = now();
        if (!mode) {
            phys_world_step(neon, 1.0f / 60, NULL, 1);
        } else {
            b2World_Step(box, 1.0f / 60, 4);
        }
        double elapsed = now() - start;
        if (frame >= 0) {
            samples[frame] = elapsed;
        }
    }
    double radius = 0;
    for (int i = 0; i < n; i++) {
        float x, y;
        if (!mode) {
            PhysV2 p = phys_body_get_pos(neon, nid[i]);
            x = p.x;
            y = p.y;
        } else {
            b2Vec2 p = b2Body_GetPosition(bid[i]);
            x = p.x;
            y = p.y;
        }
        assert(isfinite(x) && isfinite(y));
        radius += sqrt((double)x * x + (double)y * y);
    }
    qsort(samples, frames, sizeof *samples, compare);
    printf(
        "backend=%s count=%d frames=%d workers=1 p50_ms=%.6f p95_ms=%.6f "
        "mean_radius=%.6f\n",
        mode ? "Box2D" : "Neon-Swarm",
        n,
        frames,
        samples[(frames - 1) / 2],
        samples[(frames - 1) * 95 / 100],
        radius / n
    );
    if (!mode) {
        phys_world_destroy(neon);
    } else {
        b2DestroyWorld(box);
    }
    free(samples);
    free(nid);
    free(bid);
}
