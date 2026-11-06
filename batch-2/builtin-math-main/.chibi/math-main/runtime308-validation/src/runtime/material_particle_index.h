#ifndef CHIBI_MATERIAL_PARTICLE_INDEX_H
#define CHIBI_MATERIAL_PARTICLE_INDEX_H

#include "runtime/material_fluid.h"

typedef struct material_particle_index material_particle_index;

material_particle_index *material_particle_index_new(float chunk_size);
void material_particle_index_free(material_particle_index *index);
void material_particle_index_clear(material_particle_index *index);
/* IDs match a contiguous particle array: add its next ID, or swap its last
 * particle into a removed ID. This index owns memberships, never particles. */
void material_particle_index_add(material_particle_index *index, int id, fluid_vec position);
void material_particle_index_move(material_particle_index *index, int id, fluid_vec position);
void material_particle_index_swap_remove(material_particle_index *index, int id, int last_id);
/* Replaces a caller-owned support/array buffer with sorted, unique IDs from
 * chunks overlapping any region. These are conservative chunk candidates. */
void material_particle_index_query(
    const material_particle_index *index,
    const fluid_region *regions,
    int region_count,
    int **ids
);

#endif
