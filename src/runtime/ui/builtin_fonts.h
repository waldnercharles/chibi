#pragma once
#include "runtime/ui/font.h"
/* Borrowed immutable compiled faces; no UI or renderer initialization. */
int game_ui_builtin_font_count();
game_ui_font game_ui_builtin_font_at(int index);
