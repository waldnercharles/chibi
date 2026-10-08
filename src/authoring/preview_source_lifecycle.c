#include "authoring/preview_source_internal.h"
#include "support/array.h"

#include <assert.h>
#include <stdlib.h>

bool game_preview_sources_staged(const game_preview_world *world)
{
    return world && world->source_state && world->source_state->staging;
}
void game_preview_sources_free(game_preview_world *world)
{
    game_preview_source_state *state = world->source_state;
    if (!state) {
        return;
    }
    assert(!state->staging);
    if (state->working != state->applied) {
        authoring_sources_free(state->working);
    }
    authoring_sources_free(state->applied);
    authoring_language_catalog_free(state->native);
    for (int i = 0; i < acount(state->edits); i++) {
        free((char *)state->edits[i].path);
        free((char *)state->edits[i].text);
        free((char *)state->edits[i].identity);
    }
    afree(state->edits);
    free(state->root);
    free(state);
    world->source_state = NULL;
}
