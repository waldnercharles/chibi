#include "support/array.h"

_Static_assert(DYNA_COOKIE == UINT32_C(0x44594e41), "the DYNA cookie value");

int main(void)
{
    int *values = NULL;
    int *copy = NULL;
    for (int i = 0; i < 17; i++) {
        apush(values, i);
    }
    acopy(copy, values);
    int valid = alen(values) == 17 && acap(values) >= 17 &&
                alen(copy) == 17 && acap(copy) >= 17 &&
                DYNA_HDR(values)->cookie == UINT32_C(0x44594e41) &&
                DYNA_HDR(copy)->cookie == UINT32_C(0x44594e41);
    for (int i = 0; i < 17; i++) {
        valid = valid && values[i] == i && copy[i] == i;
    }
    copy[0] = -1;
    valid = valid && values[0] == 0;
    afree(copy);
    afree(values);
    return valid && copy == NULL && values == NULL ? 0 : 1;
}
