#include "runtime/geometry/distance_transform.h"

#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>

/* Lower envelope intersection of the parabolas rooted at cells q and r. */
static double crossing(const double *f, int stride, int q, int r)
{
    return (f[q * stride] + (double)q * q - f[r * stride] - (double)r * r) /
           (2.0 * (q - r));
}

/* One strided line in place. d holds n values, v n roots, z n+1 bounds. */
static void line(double *f, int n, int stride, double *d, int *v, double *z)
{
    int k = 0;
    v[0] = 0;
    z[0] = -HUGE_VAL;
    z[1] = HUGE_VAL;
    for (int q = 1; q < n; q++) {
        double s = crossing(f, stride, q, v[k]);
        while (s <= z[k]) {
            k--;
            s = crossing(f, stride, q, v[k]);
        }
        v[++k] = q;
        z[k] = s;
        z[k + 1] = HUGE_VAL;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) {
            k++;
        }
        d[q] = (double)(q - v[k]) * (q - v[k]) + f[v[k] * stride];
    }
    for (int q = 0; q < n; q++) {
        f[q * stride] = d[q];
    }
}

void distance_transform(double *squared, int width, int height)
{
    int longest = width > height ? width : height;
    double *work = xmalloc(((size_t)longest * 2 + 1) * sizeof *work);
    int *roots = xmalloc((size_t)longest * sizeof *roots);
    for (int x = 0; x < width; x++) {
        line(squared + x, height, width, work, roots, work + longest);
    }
    for (int y = 0; y < height; y++) {
        line(squared + (size_t)y * width, width, 1, work, roots, work + longest);
    }
    free(roots);
    free(work);
}
