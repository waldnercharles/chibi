#pragma once
/* Static solid material placed in the world, indexed for matter queries.
 * Each solid borrows a mask, positioned at its center and rotated by angle.
 * Overlapping solids form their union. Distances come from the masks' signed
 * samples, so they share the contour the renderer draws. */
#include "runtime/material_fluid.h"
#include "runtime/material_mask.h"

#include <stdint.h>

/* Distance reported where no solid is known. */
#define MATERIAL_SOLID_FAR 1e9f

typedef struct
{
    const material_mask *mask;
    fluid_vec position;
    float angle;
    /* Caller identity, never interpreted here. */
    uint64_t owner;
} material_solid;

typedef struct
{
    /* Negative inside; MATERIAL_SOLID_FAR where no solid is known. */
    float distance;
    /* Outward unit direction of increasing distance; zero where unknown. */
    fluid_vec normal;
    /* Index of the nearest solid, or -1. */
    int solid;
} material_solid_sample;

material_solids *material_solids_new();
void material_solids_free(material_solids *solids);
/* Copies the placements and rebuilds the index. Masks stay borrowed until the
 * next call; a mask edit needs no rebuild while its placement is unchanged. */
void material_solids_set(material_solids *solids, const material_solid *items, int count);
const material_solid *material_solids_items(const material_solids *solids, int *count);
material_solid_sample material_solids_sample(const material_solids *solids, fluid_vec point);
/* True when the open segment from a to b passes through solid material. */
bool material_solids_blocked(const material_solids *solids, fluid_vec a, fluid_vec b);
/* The solid whose occupied cell contains point, or -1. Writes the point in
 * that solid's local mask coordinates. */
int material_solids_find(const material_solids *solids, fluid_vec point, fluid_vec *local);
