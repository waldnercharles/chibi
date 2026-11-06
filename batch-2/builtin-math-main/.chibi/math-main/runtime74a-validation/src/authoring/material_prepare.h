#ifndef CHIBI_AUTHORING_MATERIAL_PREPARE_H
#define CHIBI_AUTHORING_MATERIAL_PREPARE_H
#include "assets/prototype.h"
#include "runtime/material_field.h"
struct entities;
/* Parse procedural terrain argument text before registering/spawning authored
 * objects. The runtime field catalog owns only copied typed argument records. */
bool authoring_prototype_parameters(struct entities *, const prototype *, char *error, size_t size);
bool authoring_prototype_parameters_fields(material_fields *, const prototype *, char *, size_t);
#endif
