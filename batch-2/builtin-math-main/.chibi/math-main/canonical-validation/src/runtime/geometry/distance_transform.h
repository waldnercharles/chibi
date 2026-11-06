#ifndef CHIBI_DISTANCE_TRANSFORM_H
#define CHIBI_DISTANCE_TRANSFORM_H

/* Marks a cell without a seed; larger than any squared distance in a grid of
 * at most 4096 cells per side. */
#define DISTANCE_TRANSFORM_FAR 1e12

/* Exact squared Euclidean distance, in cells, from every cell of a row-major
 * grid to its nearest seed (Felzenszwalb and Huttenlocher). Seeds hold zero
 * and every other cell DISTANCE_TRANSFORM_FAR on entry. */
void distance_transform(double *squared, int width, int height);

#endif
