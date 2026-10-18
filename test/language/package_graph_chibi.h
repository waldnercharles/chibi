#ifndef CHIBI_TEST_PACKAGE_GRAPH_CHIBI_H
#define CHIBI_TEST_PACKAGE_GRAPH_CHIBI_H

#include "authoring/source_packages.h"

bool package_graph_chibi_compare(
    const source_package_file *,
    int,
    const source_package_import *,
    int,
    const source_packages *,
    const source_package_error *
);
void package_graph_chibi_finish();

#endif
