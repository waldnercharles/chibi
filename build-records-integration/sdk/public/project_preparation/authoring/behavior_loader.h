#ifndef CHIBI_APP_BEHAVIOR_FIXTURES_H
#define CHIBI_APP_BEHAVIOR_FIXTURES_H

#include "authoring/preview.h"
#include "authoring/prototype_preview.h"

/* Prepare only native shader/model/field/event declarations. No executable
 * source is registered or run. */
int authoring_graph_fixtures_prepare(preview_world *, const char *content_root);

/* Loads the canonical behavior fixtures used by the shipped app and headless
 * authoring sessions. Native KDL render assets load in dependency order;
 * executable sources prepare as one complete collection. With a preview, its
 * live world, which must not be `world`, takes them too. Both preview worlds
 * stage before either publishes, preserving prior source generations on error.
 * Returns the number loaded, or -1 with failure details written to stderr. */
int app_behavior_fixtures_load(preview_world *world, prototype_preview *preview, const char *content_root);

#endif
