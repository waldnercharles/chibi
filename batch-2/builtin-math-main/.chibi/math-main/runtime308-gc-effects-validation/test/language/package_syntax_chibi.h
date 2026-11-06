#ifndef CHIBI_TEST_PACKAGE_SYNTAX_CHIBI_H
#define CHIBI_TEST_PACKAGE_SYNTAX_CHIBI_H

#include "authoring/source_package_syntax.h"

bool package_syntax_chibi_compare(
    const script_syntax *,
    const source_package_syntax *,
    const script_diagnostic *,
    const char *path,
    const char *directory,
    int file
);
void package_syntax_chibi_finish();
bool package_syntax_chibi_complete();
void test_language_package_syntax_chibi();
#endif
