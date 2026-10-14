#pragma once
#include "runtime/ui/font.h"
/* Borrowed immutable compiled faces; no UI or renderer initialization. */
int ui_builtin_font_count();
ui_font ui_builtin_font_at(int index);
