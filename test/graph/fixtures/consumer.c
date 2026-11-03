#include "graph_fixture/fixture.h"

int main(void)
{
    return graph_fixture_first() + graph_fixture_second() + graph_fixture_third() == 42
             ? 0
             : 1;
}
