#include "interface.h"

float add_floats(float a, float b)
{
    return a + b;
}

const char *split_values(float a, double *sum, float b, double *difference)
{
    if (b == 0) {
        return "split_values needs a nonzero second input";
    }
    *sum = (double)a + b;
    *difference = (double)a - b;
    return 0;
}
