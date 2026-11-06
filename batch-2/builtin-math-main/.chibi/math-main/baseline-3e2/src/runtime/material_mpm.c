#include "runtime/material_mpm.h"

#include "support/array.h"
#include "support/cos_sin.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct
{
    int x, y, slot, references;
    double mass, velocity[2];
} mpm_node;

typedef struct
{
    int particle, nodes[9];
    double weights[9], affine[4];
    fluid_vec position, velocity;
    fluid_deformation deformation;
    bool valid;
} mpm_particle;

typedef struct
{
    double linear[2], angular;
} mpm_impulse;

struct material_mpm
{
    mpm_node *nodes;
    mpm_particle *work;
    int *slots, slot_capacity;
    int *starts, *cursor, *references;
    mpm_impulse *impulses;
};

material_mpm *material_mpm_new()
{
    return xcalloc(1, sizeof(material_mpm));
}

void material_mpm_clear(material_mpm *mpm)
{
    for (int i = 0; i < acount(mpm->nodes); i++) {
        mpm->slots[mpm->nodes[i].slot] = 0;
    }
    aclear(mpm->nodes);
    aclear(mpm->work);
    aclear(mpm->starts);
    aclear(mpm->cursor);
    aclear(mpm->references);
}

void material_mpm_free(material_mpm *mpm)
{
    afree(mpm->nodes);
    afree(mpm->work);
    afree(mpm->starts);
    afree(mpm->cursor);
    afree(mpm->references);
    afree(mpm->impulses);
    free(mpm->slots);
    free(mpm);
}

int material_mpm_node_count(const material_mpm *mpm)
{
    return acount(mpm->nodes);
}

static double determinant(const float *f)
{
    return (double)f[0] * f[3] - (double)f[1] * f[2];
}

static bool valid_particle(const fluid_particle *p)
{
    fluid_constitutive m = p->constitutive;
    if (!isfinite(p->mass) || p->mass <= 0 || !isfinite(m.density) ||
        m.density <= 0 || !isfinite(m.bulk_modulus) || m.bulk_modulus <= 0 ||
        !isfinite(m.shear_modulus) || m.shear_modulus < 0 ||
        !isfinite(p->position.x) || !isfinite(p->position.y) ||
        !isfinite(p->velocity.x) || !isfinite(p->velocity.y) ||
        !isfinite(p->deformation.volume) || p->deformation.volume <= 0) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!isfinite(p->deformation.f[i]) || !isfinite(p->deformation.affine[i])) {
            return false;
        }
    }
    return m.shear_modulus == 0 || determinant(p->deformation.f) > 0;
}

float material_mpm_timestep(
    fluid_mpm_settings settings,
    const fluid_particle *particles,
    const int *active,
    int count
)
{
    double h = settings.cell_size;
    if (!isfinite(h) || h <= 0 || !isfinite(settings.gravity.x) ||
        !isfinite(settings.gravity.y)) {
        return 0;
    }
    double step = FLT_MAX;
    double gravity = hypot(settings.gravity.x, settings.gravity.y);
    if (gravity > 0) {
        step = sqrt(.25 * h / gravity);
    }
    for (int n = 0; n < count; n++) {
        const fluid_particle *p = &particles[active[n]];
        if (p->kind == FLUID_GAS) {
            continue;
        }
        if (!valid_particle(p)) {
            return 0;
        }
        double j = p->deformation.volume;
        double strain = p->constitutive.shear_modulus > 0
                          ? fmax(1, j * j)
                          : fmax(1, 1 / (j * j * j));
        double affine = 0, norm = 0;
        for (int i = 0; i < 4; i++) {
            affine += (double)p->deformation.affine[i] * p->deformation.affine[i];
        }
        if (p->constitutive.shear_modulus > 0) {
            for (int i = 0; i < 4; i++) {
                norm += (double)p->deformation.f[i] * p->deformation.f[i];
            }
            strain = fmax(strain, norm);
        }
        double wave = sqrt(
            ((double)p->constitutive.bulk_modulus + 2 * p->constitutive.shear_modulus) *
            strain / p->constitutive.density
        );
        double speed = hypot(p->velocity.x, p->velocity.y) + sqrt(2 * affine) * h;
        step = fmin(step, .25 * h / (speed + wave));
        if (affine > 0) {
            step = fmin(step, .1 / sqrt(affine));
        }
    }
    return (float)step;
}

static unsigned bucket(const material_mpm *mpm, int x, int y)
{
    return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) &
           (mpm->slot_capacity - 1);
}

static int insert(material_mpm *mpm, int x, int y)
{
    unsigned at = bucket(mpm, x, y);
    while (mpm->slots[at]) {
        int i = mpm->slots[at] - 1;
        if (mpm->nodes[i].x == x && mpm->nodes[i].y == y) {
            mpm->nodes[i].references++;
            return i;
        }
        at = (at + 1) & (mpm->slot_capacity - 1);
    }
    int i = acount(mpm->nodes);
    apush(mpm->nodes, ((mpm_node){ .x = x, .y = y, .slot = (int)at, .references = 1 }));
    mpm->slots[at] = i + 1;
    return i;
}

/* Stress is P F^T. In two dimensions lambda = bulk - shear. Positive
 * determinant gives the rotation directly, without an SVD dependency. */
static bool particle_affine(mpm_particle *w, const fluid_particle *p, double h, double dt)
{
    fluid_constitutive m = p->constitutive;
    double j = p->deformation.volume;
    double stress[4] = { 0 };
    double lambda = m.bulk_modulus;
    if (m.shear_modulus > 0) {
        const float *f = p->deformation.f;
        j = determinant(f);
        double a = (double)f[0] + f[3], b = (double)f[2] - f[1];
        double length = hypot(a, b);
        if (length <= 0 || !isfinite(length)) {
            return false;
        }
        double r[4] = { a / length, -b / length, b / length, a / length };
        for (int row = 0; row < 2; row++) {
            for (int column = 0; column < 2; column++) {
                for (int k = 0; k < 2; k++) {
                    stress[row * 2 + column] += 2 * m.shear_modulus *
                                                ((double)f[row * 2 + k] -
                                                 r[row * 2 + k]) *
                                                f[column * 2 + k];
                }
            }
        }
        lambda -= m.shear_modulus;
    }
    /* Tait gamma=4 gives rest bulk K and increasing resistance to compression.
     * Kirchhoff stress is -J*p, with p=K/4*(J^-4-1). */
    double pressure = m.shear_modulus > 0 ? lambda * (j - 1) * j
                                          : .25 * lambda * (j - 1 / (j * j * j));
    stress[0] += pressure;
    stress[3] += pressure;
    double force = -4 * dt * ((double)p->mass / m.density) / (h * h);
    for (int i = 0; i < 4; i++) {
        w->affine[i] = p->mass * (double)p->deformation.affine[i] + force * stress[i];
        if (!isfinite(w->affine[i])) {
            return false;
        }
    }
    return true;
}

static bool seed(
    material_mpm *mpm,
    fluid_mpm_settings settings,
    double dt,
    const fluid_particle *particles,
    const int *active,
    int count
)
{
    material_mpm_clear(mpm);
    int eligible = 0;
    for (int n = 0; n < count; n++) {
        eligible += particles[active[n]].kind != FLUID_GAS;
    }
    int capacity = 32;
    while (capacity < eligible * 32) {
        capacity *= 2;
    }
    if (capacity > mpm->slot_capacity) {
        mpm->slot_capacity = capacity;
        mpm->slots = xrealloc(mpm->slots, (size_t)capacity * sizeof *mpm->slots);
        memset(mpm->slots, 0, (size_t)capacity * sizeof *mpm->slots);
    }
    double h = settings.cell_size;
    for (int n = 0; n < count; n++) {
        int id = active[n];
        const fluid_particle *p = &particles[id];
        if (p->kind == FLUID_GAS) {
            continue;
        }
        if (!valid_particle(p)) {
            return false;
        }
        double gx = p->position.x / h, gy = p->position.y / h;
        double x = floor(gx - .5), y = floor(gy - .5);
        if (x < INT_MIN || x > INT_MAX - 2 || y < INT_MIN || y > INT_MAX - 2) {
            return false;
        }
        mpm_particle work = { .particle = id };
        if (!particle_affine(&work, p, h, dt)) {
            return false;
        }
        double weights[2][3];
        double fraction[2] = { gx - x, gy - y };
        for (int axis = 0; axis < 2; axis++) {
            double f = fraction[axis];
            weights[axis][0] = .5 * (1.5 - f) * (1.5 - f);
            weights[axis][1] = .75 - (f - 1) * (f - 1);
            weights[axis][2] = .5 * (f - .5) * (f - .5);
        }
        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 3; column++) {
                int k = row * 3 + column;
                work.nodes[k] = insert(mpm, (int)x + column, (int)y + row);
                work.weights[k] = weights[0][column] * weights[1][row];
            }
        }
        apush(mpm->work, work);
    }
    int nodes = acount(mpm->nodes);
    asetlen(mpm->starts, nodes + 1);
    mpm->starts[0] = 0;
    for (int i = 0; i < nodes; i++) {
        mpm->starts[i + 1] = mpm->starts[i] + mpm->nodes[i].references;
    }
    acopy(mpm->cursor, mpm->starts);
    asetlen(mpm->references, acount(mpm->work) * 9);
    /* Iterating particles first fixes every node's contribution order. */
    for (int i = 0; i < acount(mpm->work); i++) {
        for (int k = 0; k < 9; k++) {
            int node = mpm->work[i].nodes[k];
            mpm->references[mpm->cursor[node]++] = i * 9 + k;
        }
    }
    return true;
}

typedef struct
{
    material_mpm *mpm;
    fluid_mpm_settings settings;
    fluid_particle *particles;
    double dt;
} mpm_step;

static void deposit(int start, int end, uint32_t, void *context)
{
    const mpm_step *step = context;
    material_mpm *mpm = step->mpm;
    double h = step->settings.cell_size;
    for (int i = start; i < end; i++) {
        mpm_node *node = &mpm->nodes[i];
        double mass = 0, momentum[2] = { 0 };
        for (int at = mpm->starts[i]; at < mpm->starts[i + 1]; at++) {
            int reference = mpm->references[at];
            const mpm_particle *work = &mpm->work[reference / 9];
            const fluid_particle *p = &step->particles[work->particle];
            double weight = work->weights[reference % 9];
            double dx = node->x * h - p->position.x,
                   dy = node->y * h - p->position.y;
            mass += weight * p->mass;
            momentum[0] += weight * (p->mass * (double)p->velocity.x +
                                     work->affine[0] * dx + work->affine[1] * dy);
            momentum[1] += weight * (p->mass * (double)p->velocity.y +
                                     work->affine[2] * dx + work->affine[3] * dy);
        }
        node->mass = mass;
        node->velocity[0] = mass > 0 ? momentum[0] / mass +
                                           step->dt * step->settings.gravity.x
                                     : 0;
        node->velocity[1] = mass > 0 ? momentum[1] / mass +
                                           step->dt * step->settings.gravity.y
                                     : 0;
    }
}

static int find_node(const material_mpm *mpm, int x, int y)
{
    unsigned at = bucket(mpm, x, y);
    while (mpm->slots[at]) {
        int i = mpm->slots[at] - 1;
        if (mpm->nodes[i].x == x && mpm->nodes[i].y == y) {
            return i;
        }
        at = (at + 1) & (mpm->slot_capacity - 1);
    }
    return -1;
}

static fluid_body collider_pose(fluid_body body, float t)
{
    if (!body.swept) {
        return body;
    }
    float angle = body.previous_angle +
                  remainderf(body.angle - body.previous_angle, 6.28318530718f) * t;
    cos_sin rotation = cos_sin_compute(angle - body.angle);
    float c = rotation.c, s = rotation.s;
    fluid_vec offset = body.center_offset;
    body.center_offset = (fluid_vec){ c * offset.x - s * offset.y,
                                      s * offset.x + c * offset.y };
    body.position.x = body.previous_position.x +
                      (body.position.x - body.previous_position.x) * t;
    body.position.y = body.previous_position.y +
                      (body.position.y - body.previous_position.y) * t;
    body.angle = angle;
    return body;
}

static void contact_node(
    material_mpm *mpm,
    int id,
    double h,
    fluid_body body,
    fluid_obstacle obstacle,
    double c,
    double s
)
{
    mpm_node *node = &mpm->nodes[id];
    if (node->mass <= 0) {
        return;
    }
    double px = node->x * h, py = node->y * h;
    double dx = px - body.position.x, dy = py - body.position.y;
    double x = c * dx + s * dy - obstacle.center.x;
    double y = -s * dx + c * dy - obstacle.center.y;
    double nx = 0, ny = 0;
    if (obstacle.radius > 0) {
        double distance = hypot(x, y);
        if (distance > obstacle.radius) {
            return;
        }
        nx = distance > 0 ? x / distance : 1;
        ny = distance > 0 ? y / distance : 0;
    } else {
        double depth_x = obstacle.half_size.x - fabs(x);
        double depth_y = obstacle.half_size.y - fabs(y);
        if (depth_x < 0 || depth_y < 0) {
            return;
        }
        if (depth_x < depth_y) {
            nx = x < 0 ? -1 : 1;
        } else {
            ny = y < 0 ? -1 : 1;
        }
    }
    double normal_x = c * nx - s * ny, normal_y = s * nx + c * ny;
    double rx = px - body.position.x - body.center_offset.x;
    double ry = py - body.position.y - body.center_offset.y;
    mpm_impulse *pending = &mpm->impulses[obstacle.body];
    double angular = body.angular_velocity +
                     (body.angular_impulse + pending->angular) * body.inverse_inertia;
    double vx = body.velocity.x +
                (body.impulse.x + pending->linear[0]) * body.inverse_mass -
                angular * ry;
    double vy = body.velocity.y +
                (body.impulse.y + pending->linear[1]) * body.inverse_mass +
                angular * rx;
    double closing = (vx - node->velocity[0]) * normal_x +
                     (vy - node->velocity[1]) * normal_y;
    if (closing <= 0) {
        return;
    }
    double lever = rx * normal_y - ry * normal_x;
    double impulse = closing / (1 / node->mass + body.inverse_mass +
                                lever * lever * body.inverse_inertia);
    node->velocity[0] += impulse * normal_x / node->mass;
    node->velocity[1] += impulse * normal_y / node->mass;
    pending->linear[0] -= impulse * normal_x;
    pending->linear[1] -= impulse * normal_y;
    pending->angular -= impulse * lever;
}

static bool grid_contacts(material_mpm *mpm, double h, fluid_mpm_colliders colliders)
{
    assert(colliders.body_count >= 0 && colliders.obstacle_count >= 0);
    if (!isfinite(colliders.sweep_end) || colliders.sweep_end < 0 ||
        colliders.sweep_end > 1) {
        return false;
    }
    asetlen(mpm->impulses, colliders.body_count);
    if (colliders.body_count) {
        memset(mpm->impulses, 0, (size_t)colliders.body_count * sizeof *mpm->impulses);
    }
    for (int i = 0; i < colliders.obstacle_count; i++) {
        fluid_obstacle obstacle = colliders.obstacles[i];
        assert(obstacle.body >= 0 && obstacle.body < colliders.body_count);
        fluid_body
            body = collider_pose(colliders.bodies[obstacle.body], colliders.sweep_end);
        /* Contacts push the body this one is a part of. */
        obstacle.body = material_fluid_response(colliders.bodies, obstacle.body);
        body.impulse = colliders.bodies[obstacle.body].impulse;
        body.angular_impulse = colliders.bodies[obstacle.body].angular_impulse;
        cos_sin rotation = cos_sin_compute(body.angle);
        double c = rotation.c, s = rotation.s;
        double center_x = body.position.x + c * obstacle.center.x -
                          s * obstacle.center.y;
        double center_y = body.position.y + s * obstacle.center.x +
                          c * obstacle.center.y;
        double ex = obstacle.radius > 0 ? obstacle.radius
                                        : fabs(c) * obstacle.half_size.x +
                                              fabs(s) * obstacle.half_size.y;
        double ey = obstacle.radius > 0 ? obstacle.radius
                                        : fabs(s) * obstacle.half_size.x +
                                              fabs(c) * obstacle.half_size.y;
        double low_x = ceil((center_x - ex) / h), high_x = floor((center_x + ex) / h);
        double low_y = ceil((center_y - ey) / h), high_y = floor((center_y + ey) / h);
        if (!isfinite(low_x) || !isfinite(high_x) || !isfinite(low_y) ||
            !isfinite(high_y)) {
            return false;
        }
        if (low_x > high_x || low_y > high_y) {
            continue;
        }
        double cells = (high_x - low_x + 1) * (high_y - low_y + 1);
        if (cells > acount(mpm->nodes) || low_x < INT_MIN ||
            high_x > INT_MAX - 1 || low_y < INT_MIN || high_y > INT_MAX - 1) {
            for (int n = 0; n < acount(mpm->nodes); n++) {
                contact_node(mpm, n, h, body, obstacle, c, s);
            }
            continue;
        }
        for (int y = (int)low_y; y <= (int)high_y; y++) {
            for (int x = (int)low_x; x <= (int)high_x; x++) {
                int node = find_node(mpm, x, y);
                if (node >= 0) {
                    contact_node(mpm, node, h, body, obstacle, c, s);
                }
            }
        }
    }
    for (int i = 0; i < colliders.body_count; i++) {
        fluid_body body = colliders.bodies[i];
        mpm_impulse pending = mpm->impulses[i];
        if (!isfinite(body.impulse.x + pending.linear[0]) ||
            fabs(body.impulse.x + pending.linear[0]) > FLT_MAX ||
            !isfinite(body.impulse.y + pending.linear[1]) ||
            fabs(body.impulse.y + pending.linear[1]) > FLT_MAX ||
            !isfinite(body.angular_impulse + pending.angular) ||
            fabs(body.angular_impulse + pending.angular) > FLT_MAX) {
            return false;
        }
    }
    return true;
}

static void gather(int start, int end, uint32_t, void *context)
{
    const mpm_step *step = context;
    material_mpm *mpm = step->mpm;
    double h = step->settings.cell_size;
    for (int i = start; i < end; i++) {
        mpm_particle *work = &mpm->work[i];
        const fluid_particle *p = &step->particles[work->particle];
        double velocity[2] = { 0 }, affine[4] = { 0 };
        for (int k = 0; k < 9; k++) {
            const mpm_node *node = &mpm->nodes[work->nodes[k]];
            double weight = work->weights[k];
            double dx = node->x * h - p->position.x,
                   dy = node->y * h - p->position.y;
            for (int axis = 0; axis < 2; axis++) {
                velocity[axis] += weight * node->velocity[axis];
                affine[axis * 2] += 4 * weight * node->velocity[axis] * dx / (h * h);
                affine[axis * 2 + 1] += 4 * weight * node->velocity[axis] * dy /
                                        (h * h);
            }
        }
        double update[4] = { 1 + step->dt * affine[0],
                             step->dt * affine[1],
                             step->dt * affine[2],
                             1 + step->dt * affine[3] };
        work->deformation = p->deformation;
        for (int k = 0; k < 4; k++) {
            work->deformation.affine[k] = (float)affine[k];
        }
        if (p->constitutive.shear_modulus == 0) {
            double j = p->deformation.volume *
                       (update[0] * update[3] - update[1] * update[2]);
            work->deformation.volume = (float)j;
            work->deformation.f[0] = work->deformation.f[3] = 1;
            work->deformation.f[1] = work->deformation.f[2] = 0;
        } else {
            const float *f = p->deformation.f;
            for (int row = 0; row < 2; row++) {
                for (int column = 0; column < 2; column++) {
                    work->deformation.f[row * 2 + column] = (float)(update[row * 2] *
                                                                        f[column] +
                                                                    update[row * 2 + 1] *
                                                                        f[2 + column]);
                }
            }
            work->deformation.volume = (float)determinant(work->deformation.f);
        }
        work->velocity = (fluid_vec){ (float)velocity[0], (float)velocity[1] };
        work->position = (fluid_vec){ (float)(p->position.x + step->dt * velocity[0]),
                                      (float)(p->position.y + step->dt * velocity[1]) };
        work->valid = isfinite(work->position.x) && isfinite(work->position.y) &&
                      isfinite(work->velocity.x) && isfinite(work->velocity.y) &&
                      isfinite(work->deformation.volume) &&
                      work->deformation.volume > 0;
        for (int k = 0; k < 4; k++) {
            work->valid &= isfinite(work->deformation.f[k]) &&
                           isfinite(work->deformation.affine[k]);
        }
        if (p->constitutive.shear_modulus > 0) {
            work->valid &= determinant(work->deformation.f) > 0;
        }
    }
}

static void run(const task_system *tasks, task_range *fn, int count, int particles, void *context)
{
    if (tasks->workers > 1 && particles >= 2048) {
        int grain = 256;
        if (count < 2048) {
            grain = count / (tasks->workers * 4);
            grain = grain < 1 ? 1 : grain;
        }
        void *task = tasks->enqueue(fn, count, grain, context, tasks->user);
        if (task) {
            tasks->finish(task, tasks->user);
        }
        return;
    }
    fn(0, count, 0, context);
}

bool material_mpm_step_colliders(
    material_mpm *mpm,
    fluid_mpm_settings settings,
    float dt,
    fluid_particle *particles,
    const int *active,
    int count,
    const task_system *tasks,
    fluid_mpm_colliders colliders
)
{
    assert(count >= 0 && count <= 1000000);
    if (!isfinite(dt) || dt <= 0 || !isfinite(settings.cell_size) ||
        settings.cell_size <= 0 || !isfinite(settings.gravity.x) ||
        !isfinite(settings.gravity.y)) {
        return false;
    }
    if (!seed(mpm, settings, dt, particles, active, count)) {
        return false;
    }
    task_system serial = { 0 };
    if (!tasks) {
        tasks = &serial;
    }
    mpm_step step = { mpm, settings, particles, dt };
    run(tasks, deposit, acount(mpm->nodes), acount(mpm->work), &step);
    if (!grid_contacts(mpm, settings.cell_size, colliders)) {
        return false;
    }
    run(tasks, gather, acount(mpm->work), acount(mpm->work), &step);
    for (int i = 0; i < acount(mpm->work); i++) {
        if (!mpm->work[i].valid) {
            return false;
        }
    }
    for (int i = 0; i < acount(mpm->work); i++) {
        const mpm_particle *work = &mpm->work[i];
        fluid_particle *p = &particles[work->particle];
        p->position = work->position;
        p->velocity = work->velocity;
        p->deformation = work->deformation;
    }
    for (int i = 0; i < colliders.body_count; i++) {
        colliders.bodies[i].impulse.x += mpm->impulses[i].linear[0];
        colliders.bodies[i].impulse.y += mpm->impulses[i].linear[1];
        colliders.bodies[i].angular_impulse += mpm->impulses[i].angular;
    }
    return true;
}

bool material_mpm_step(
    material_mpm *mpm,
    fluid_mpm_settings settings,
    float dt,
    fluid_particle *particles,
    const int *active,
    int count,
    const task_system *tasks
)
{
    return material_mpm_step_colliders(
        mpm,
        settings,
        dt,
        particles,
        active,
        count,
        tasks,
        (fluid_mpm_colliders){ 0 }
    );
}
