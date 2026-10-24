#ifndef CHIBI_TEST_GRAPH_DISPATCH_H
#define CHIBI_TEST_GRAPH_DISPATCH_H
#include <stdbool.h>
/* Runs a recognized graph or geometry selector; false means unrecognized. */
bool test_graph_dispatch(const char *selector);
#endif
