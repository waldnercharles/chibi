#ifndef CHIBI_PARTS_PREPARE_H
#define CHIBI_PARTS_PREPARE_H
#include "render/arena_parts.h"
bool authoring_parts_parse(const char *, arena_parts *, char *, size_t);
char *authoring_parts_write(const arena_parts *);
bool authoring_parts_prepare(const char *, char *, size_t);
#endif
