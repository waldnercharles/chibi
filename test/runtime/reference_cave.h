#pragma once

/* An independent hand-written cave, the field CaveField.chibi must reproduce:
 * signed distances on a width by height grid, solid where negative, with its
 * air connected. The caller frees the grid. */
float *reference_cave_generate(unsigned seed, int width, int height, float cell_size, float radius, float roughness);
