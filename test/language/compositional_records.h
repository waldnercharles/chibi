#ifndef CHIBI_TEST_COMPOSITIONAL_RECORDS_H
#define CHIBI_TEST_COMPOSITIONAL_RECORDS_H

#include <stdbool.h>
#include <stddef.h>

void test_language_compositional_records();
extern const char COMPOSITIONAL_RECORD_ALIAS_REJECTION[];
bool compositional_records_depth_source(char *, size_t, int count, bool arrays);
bool compositional_records_fanout_source(char *, size_t);

#endif
