#ifndef CHIBI_UI_CONTROLS_H
#define CHIBI_UI_CONTROLS_H

/* Native form controls for every RmlUi document, the editor's and games', as
 * browsers ship <input>. Each is a C element over rml_element_hooks that
 * builds its own parts, takes its own pointer and keys, and follows the web's
 * events: `input` while a gesture runs and `change` once when it commits,
 * both bubbling with ev.value (text) and ev.canceled. Attributes in, events
 * out: bind values with data-attr-value. A boolean attribute is on when
 * present, unless its value is "false". Each reflects its state into data-*
 * attributes the theme styles (data-editing, data-scrubbing, data-dragging,
 * data-mixed, data-readonly, data-disabled, data-invalid, data-open).
 *
 * - <number-field value min max step precision integer float slider unit
 *   addon mixed readonly disabled invalid/>: Base UI's NumberField and
 *   Blender's. Drag past FEEL_DRAG_DP to scrub (Alt fine, Shift coarse,
 *   Ctrl snaps), click to type a value or an expression
 *   (support/number_expr.h), arrow keys and the held steppers step, Enter
 *   or Tab commits and Escape cancels. Both bounds with `slider` scrub
 *   across the width and draw a fill. `float` keeps values 32-bit floats.
 *   Its parts are .fill, .mark, .part.addon, .part.value, .part.unit and
 *   .step.dec/.inc; the value repaints without laying out its document.
 * - <color-field value="#rrggbbaa" mixed readonly disabled/>: a swatch that
 *   opens its own picker popover (plane, hue, RGBA hex and Opacity), as
 *   <input type=color> opens the system's: `input` while it is open,
 *   `change` once when it closes.
 * - <splitter axis="x|y" min max value invert/>: drag to resize; the value
 *   moves with the pointer along `axis`, against it with `invert`.
 * - <live-text value/>: one line of text whose glyphs change without
 *   laying out its document, for readouts.
 * - <choice-list>: Base UI's Combobox list and cmdk's filter. Its
 *   [role=option] descendants (value, data-label or .label, data-keywords)
 *   filter as its first <input> is typed in, hiding the rest and emptied
 *   [role=group]s with data-filtered and marking data-empty; Up, Down, Home,
 *   End and the pointer move data-highlighted, and Enter or a click chooses,
 *   sending change with the option's value. Shown again, it clears its
 *   search and highlights the current option (aria-selected or
 *   data-current). A `virtualized` one's app filters it (choice_list.c).
 * - <tab-list>: authored [role=tab] buttons with aria-selected and optional
 *   [role=presentation] rows holding tab actions. Unmodified Left, Right,
 *   Home and End focus and activate enabled tabs; arrows wrap. Horizontal
 *   or vertical wheel movement scrolls horizontally. Selection, contents
 *   and geometry changes reveal the selected row, while manual scrolling
 *   stays put. Middle-click a row activates its enabled [data-tab-close]
 *   button. The strip reflects data-overflow; authored CSS supplies flex
 *   layout, overflow-x: auto and hidden scrollbar styling.
 * - <line-chart series="name ..." window unit precision reference min max
 *   fill live/>: Liveline's live line chart with Recharts' ticks. It draws
 *   the series C published under the names `series` lists
 *   (line_chart_publish), the first in its `color` unless the series names
 *   one, and the grid in its border-color, over the last `window` seconds
 *   (30) up to the newest time its series hold. The range follows the data
 *   with a margin and eases toward it, or is min to max; `reference` draws
 *   a dashed line such as a frame budget, `fill` the area under the first
 *   series, and `live` pulses its newest point. Its parts are .chart-tick,
 *   .chart-badge (the newest value, with `precision` and `unit`) and
 *   .chart-hover (the value under the pointer). Publishing redraws the
 *   charts that name the series and lays out nothing.
 *
 * C owners (the kit) read a control's state instead of listening. */

#include "ui/rml.h"

#include <stdbool.h>
#include <stdint.h>

/* Registers the elements; the backend does once RmlUi starts. */
void controls_register();

/* The value the last input or change set, once: true when one did since the
 * last call, with *committed true when a change did. */
bool number_field_take(rml_element *field, double *value, bool *committed);
/* Scrubbing, stepping or typing. */
bool number_field_active(const rml_element *field);

bool color_field_take(rml_element *field, uint32_t *rgba, bool *committed);
/* Its picker is open. */
bool color_field_open(const rml_element *field);

/* One line of text in `label` that changes without laying out its
 * document. The first text, a change of size or style, and text wider than
 * the label (which then ellipsizes) lay out as usual. line_text_set sizes the
 * label to the text; line_text_set_right keeps it right-aligned in the
 * label's fixed width. */
void line_text_set(rml_element *label, const char *text);
void line_text_set_right(rml_element *label, const char *text);

/* Samples a <line-chart> draws: time-ascending seconds and their values. */
typedef struct line_chart_series
{
    const float *times;
    const float *values;
    int count;
    uint32_t color; /* 0xRRGGBBAA; 0 draws in the chart's color */
    double now;     /* the time at the chart's right edge */
} line_chart_series;

/* Copies `series` under `name` for the charts that name it, which redraw
 * when it changed; NULL forgets the name. */
void line_chart_publish(const char *name, const line_chart_series *series);

/* Moves focus `step` tab stops from `from` among `root`'s enabled, visible
 * ones in document order, wrapping when `wrap`, outside `skip` when set;
 * false when there is none there. */
bool focus_step(rml_element *root, rml_element *from, int step, bool wrap, rml_element *skip);

#endif
