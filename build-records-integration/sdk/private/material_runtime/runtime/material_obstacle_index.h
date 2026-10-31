#pragma once

#include "runtime/material_fluid.h"

typedef struct material_obstacle_index material_obstacle_index;

material_obstacle_index *material_obstacle_index_new();
void material_obstacle_index_free(material_obstacle_index *index);
/* Borrowed immutable snapshots remain valid until the next build. Build before
 * worker queries; queries allocate nothing and share no mutable scratch. */
void material_obstacle_index_build(
    material_obstacle_index *index,
    const fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    float cell_size
);
bool material_obstacle_index_blocked(
    const material_obstacle_index *index,
    fluid_vec from,
    fluid_vec to,
    int ignore_body,
    bool hot_solids
);
/* Exact reference predicate, also useful for callers without a tick index.
 * hot_solids=false preserves ignition's exclusion of heat-emitting bodies. */
bool material_obstacle_blocked(
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    fluid_vec from,
    fluid_vec to,
    int ignore_body,
    bool hot_solids
);

/* Local broad-phase candidates in original obstacle order. Caller owns and
 * reuses the array; the index remains immutable during queries. */
void material_obstacle_index_query(
    const material_obstacle_index *index,
    fluid_vec position,
    float radius,
    int **obstacles
);
