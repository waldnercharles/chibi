#ifndef CHIBI_NATIVE_PROTOTYPE_H
#define CHIBI_NATIVE_PROTOTYPE_H

#include "assets/prototype.h"

/* Concrete engine authoring policy: visual/physics component creation values
 * and inherited VisualMotion placement. Detached values retain precedence.
 * Temporary default rows are copied synchronously into the prototype. */
int game_prototype_add_native_component(game_prototype *, const ecs_component_desc *);
av_value *game_prototype_native_defaults_value(const game_prototype_registry *, const ecs_component_desc *);

#endif
