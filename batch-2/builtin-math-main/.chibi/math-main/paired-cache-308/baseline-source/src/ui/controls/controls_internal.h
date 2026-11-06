#ifndef CHIBI_UI_CONTROLS_INTERNAL_H
#define CHIBI_UI_CONTROLS_INTERNAL_H

/* Helpers the native controls share; not part of controls.h. */

#include "ui/controls/controls.h"

#include <stddef.h>

/* Symbols-font glyphs: chevrons left and right. */
#define GLYPH_LEFT "\xee\xaa\xb5"
#define GLYPH_RIGHT "\xee\xaa\xb6"

/* A child element with classes `cls`, appended. */
rml_element *control_part(rml_element *parent, const char *tag, const char *cls);
/* A span with class "icon" holding one glyph. */
rml_element *control_icon(rml_element *parent, const char *glyph);
/* Replaces the element's content with one text node, laid out as usual; does
 * nothing when it holds that text already. */
void control_text(rml_element *el, const char *text);
/* Sets or removes a boolean attribute, touching the element only on change. */
void control_flag(rml_element *el, const char *name, bool on);

/* Attribute reads. A boolean attribute is on when present, unless "false";
 * a number reads `fallback` when absent or not a number. */
bool control_bool(const rml_element *el, const char *name);
double control_number(const rml_element *el, const char *name, double fallback);
/* "" when absent; at most size - 1 bytes. */
void control_string(const rml_element *el, const char *name, char *out, size_t size);
/* The text the element's text nodes hold, in order; at most size - 1 bytes. */
void control_text_of(const rml_element *el, char *out, size_t size);

/* The context's pixels per dp. */
float control_dp(const rml_element *el);
bool control_within(const rml_element *el, const rml_element *ancestor);
/* Asks the context to update now, as an input that changed what shows does. */
void control_redraw(rml_element *el);

/* Dispatches `type` (input or change) with `value` written "%.17g". */
void control_dispatch_number(rml_element *el, const char *type, double value, bool canceled);

/* Registration, one per element. */
void number_field_register();
void color_field_register();
void splitter_register();
void live_text_register();
void choice_list_register();
void tab_list_register();
void line_chart_register();

#endif
