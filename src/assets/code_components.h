#ifndef CHIBI_CODE_COMPONENTS_H
#define CHIBI_CODE_COMPONENTS_H

#include "ecs/ecs.h"
#include "graph/model/type.h"

/* Canonical source spellings borrow component names and format reflected fields. */
const char *code_component_spelling(const ecs_component_desc *component);
void code_component_field_spelling(const type_field *field, char *out, size_t size);
/* Exact reflected spelling or a unique short/case-insensitive source spelling. */
const ecs_component_desc *code_component_named(const char *name, bool *ambiguous);
const type_field *code_component_field(const type_desc *type, const char *name, bool *ambiguous);

#endif
