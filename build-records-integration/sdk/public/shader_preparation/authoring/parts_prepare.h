#ifndef CHIBI_PARTS_PREPARE_H
#define CHIBI_PARTS_PREPARE_H
#include "render/scene3d_parts.h"
bool authoring_parts_parse(const char *, scene3d_parts *, char *, size_t);
char *authoring_parts_write(const scene3d_parts *);
bool authoring_parts_prepare(const char *, char *, size_t);
#endif
