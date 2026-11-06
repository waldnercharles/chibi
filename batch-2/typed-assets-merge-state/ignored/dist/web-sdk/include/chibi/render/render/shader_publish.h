#ifndef CHIBI_SHADER_PUBLISH_H
#define CHIBI_SHADER_PUBLISH_H
#include "runtime/player.h"
/* Copied player hooks. The content root is borrowed during stage; it resolves
 * only explicitly declared external typed artifacts. NULL permits embedded
 * resources. No GPU upload or authored-source parsing occurs during stage.
 * Publication selects the current resident collection. Replaced collections
 * remain owned until their players retire. Catalog clear requires all players
 * to have retired; resource and rendering calls are thread confined. */
player_resource_host render_prepared_resources(const char *content_root);
#endif
