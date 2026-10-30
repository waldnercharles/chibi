/* How input feels everywhere in Studio and the native controls
 * (ui/controls/controls.h): one zoom rate for every zoomable canvas, one
 * scroll distance, one drag threshold, one double-click window and the
 * tooltip timing. chibi-studio's web/web_shell.html mirrors FEEL_PINCH_MAX_PX. */
#ifndef CHIBI_UI_INPUT_FEEL_H
#define CHIBI_UI_INPUT_FEEL_H

#include <math.h>

/* Wheels count SDL's notches: a mouse wheel's click is one, and a trackpad's
 * scroll is fractions of one, 100 px of it to a notch in a browser. Natural
 * scrolling arrives already applied, so no direction is inverted here. */

/* A notch zooms by 2^0.2, about 15% (xyflow, Blender), and a fraction of a
 * notch zooms by that fraction, so a trackpad zooms smoothly. */
#define FEEL_ZOOM_RATE 0.2f
/* One event counts at most five notches, so it never more than doubles or
 * halves a zoom. Each event is clamped alone: how many arrive in a frame never
 * changes the zoom they make. */
#define FEEL_ZOOM_EVENT_MAX 5.0f
/* A Ctrl wheel event under this many pixels is a browser's trackpad pinch;
 * a mouse with Ctrl held sends a notch of 100 px or a line. */
#define FEEL_PINCH_MAX_PX 50.0f
/* Window points a notch scrolls a list, the text editor or a page: what a
 * browser scrolls, so a trackpad moves content with the fingers. That is
 * about four compact rows. */
#define FEEL_SCROLL_POINTS 100.0f
/* What RmlUi 6.3 scrolls for one of its wheel units (UNIT_SCROLL_LENGTH). */
#define FEEL_RMLUI_SCROLL_DP 80.0f
/* dp a press travels before it drags instead of clicking: Scene objects,
 * graph nodes and wires, dragged rows and tiles, and scrubbed numbers. */
#define FEEL_DRAG_DP 4.0f
/* A second press within this time and RmlUi's 3 dp of the first is a
 * double-click: the operating systems' default, and RmlUi's. */
#define FEEL_DOUBLE_CLICK_SECONDS 0.5
#define FEEL_DOUBLE_CLICK_DP 3.0f
/* Tooltips open once the pointer rests 500 ms; within 400 ms of one closing,
 * the next opens at once (Base UI). */
#define FEEL_TOOLTIP_DELAY 0.5
#define FEEL_TOOLTIP_GROUP 0.4

/* The notches one wheel event zooms by. */
static inline float feel_zoom_notches(float notches)
{
    return fminf(fmaxf(notches, -FEEL_ZOOM_EVENT_MAX), FEEL_ZOOM_EVENT_MAX);
}

/* The magnification of `notches` summed from clamped events; a positive
 * count, the wheel turned away from the user, zooms in. */
static inline float feel_zoom_scale(float notches)
{
    return exp2f(FEEL_ZOOM_RATE * notches);
}

/* RmlUi wheel units that scroll `notches` notches in a context of `dp_ratio`
 * pixels a dp, in a window of `pixel_scale` pixels a point. */
static inline float feel_scroll_units(float notches, float dp_ratio, float pixel_scale)
{
    return notches * FEEL_SCROLL_POINTS * pixel_scale /
           (FEEL_RMLUI_SCROLL_DP * dp_ratio);
}

#endif
