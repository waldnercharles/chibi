#ifndef CHIBI_ASSET_CODE_H
#define CHIBI_ASSET_CODE_H

#include "assets/asset_source.h"
#include "assets/prototype.h"
#include "editor/ui/kit.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct asset_code asset_code;
/* An editor session owns exact source text and a last valid overlay, shared by
 * loaders and live preview. Invalid typing never replaces that overlay. */
asset_code *asset_code_open(const char *path, char *error, size_t size);
void asset_code_free(asset_code *code);
/* Follow ownership when the workspace restores this source's preview overlay. */
void asset_code_adopt_overlay(asset_code *code);
/* Native checkpoint state; protocol conversion belongs to the workspace boundary. */
av_value *asset_code_capture(asset_code *code);
bool asset_code_has_state(const asset_code *code, const av_value *state);
asset_code *asset_code_restore(const av_value *state, const game_prototype_registry *registry);
const char *asset_code_path(const asset_code *code);
bool asset_code_dirty(const asset_code *code);
const char *asset_code_error(const asset_code *code);
kit_code_editor *asset_code_editor(asset_code *code);
void asset_code_set_registry(asset_code *code, const game_prototype_registry *registry);
void asset_code_set_names(asset_code *code, const char *const *names, int count);
/* Check external/visual saves and update the editor with a single text edit. */
void asset_code_refresh(asset_code *code);
/* Render the existing code primitive with shared KDL language intelligence. */
kit_code_result asset_code_draw(
    asset_code *code,
    kit *k,
    const char *key,
    const kit_code_desc *preferences,
    bool live
);
/* Saves exactly the valid text shown. False on diagnostics or disk conflict. */
bool asset_code_save(asset_code *code, char *error, size_t size);

#endif
