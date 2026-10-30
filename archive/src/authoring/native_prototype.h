#ifndef CHIBI_NATIVE_PROTOTYPE_H
#define CHIBI_NATIVE_PROTOTYPE_H

#include "assets/prototype.h"

/* Concrete engine authoring policy: visual/physics component creation values.
 * Detached values retain precedence.
 * Temporary default rows are copied synchronously into the prototype. */
int prototype_add_native_component(prototype *, const ecs_component_desc *);
av_value *prototype_native_defaults_value(const prototype_registry *, const ecs_component_desc *);

#endif
