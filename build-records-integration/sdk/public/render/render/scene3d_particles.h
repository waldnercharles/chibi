#ifndef CHIBI_RENDER_SCENE3D_PARTICLES_H
#define CHIBI_RENDER_SCENE3D_PARTICLES_H

#include "render/scene3d.h"

enum
{
    SCENE3D_PARTICLE_FIELD_LIMIT = 1048576,
    SCENE3D_PARTICLE_FIELD_SOURCES = 128
};

typedef struct scene3d_particles scene3d_particles;
scene3d_particles *render_scene3d_particles_new();
void render_scene3d_particles_free(scene3d_particles *);
void render_scene3d_particles_reset(scene3d_particles *);
bool render_scene3d_particles_prepare(
    scene3d_particles *,
    ecs_world *,
    const ecs_entity *,
    int count,
    const struct engine *,
    char *error,
    size_t size
);
void render_scene3d_particles_draw(const scene3d_particles *, const render_scene3d_camera *);
int render_scene3d_particles_count(const scene3d_particles *);
int render_scene3d_particles_draws(const scene3d_particles *);

#endif
