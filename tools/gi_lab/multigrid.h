#pragma once

#include "platform/cute.h"

/* Experimental residual-correction solver for the analytic optical fixtures. */
typedef struct gi_multigrid gi_multigrid;

gi_multigrid *gi_multigrid_create(int width, int height, int directions);
void gi_multigrid_destroy(gi_multigrid *solver);
CF_Texture gi_multigrid_solve(
    gi_multigrid *solver,
    const int *grid,
    const int *budget,
    const float *scene,
    int cycles,
    int smooth
);
size_t gi_multigrid_bytes(const gi_multigrid *solver);
int gi_multigrid_levels(const gi_multigrid *solver);
