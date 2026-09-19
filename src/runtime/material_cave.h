#ifndef CHIBI_MATERIAL_CAVE_H
#define CHIBI_MATERIAL_CAVE_H

/* Seeded density-field cave with a connected trunk and looping side passages.
 * Returns an owned row-major signed field (negative inside solid). Coordinates are centered locally. */
float *material_cave_generate(unsigned seed, int width, int height, float cell_size, float radius, float roughness);

#endif
