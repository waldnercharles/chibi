#define PICO_UNIT_IMPLEMENTATION
#include "third_party/pico_headers/pico_unit.h"

void test_host_process();
void test_host_state_file();

int main()
{
    test_host_process();
    test_host_state_file();
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
