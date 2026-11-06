#ifndef CHIBI_UI_MOTION_H
#define CHIBI_UI_MOTION_H

/* Motion for every RmlUi document, the editor's and games': springs after
 * Motion (motion.dev), and the native elements authored UI uses for the
 * kit's motion primitives, as Unity's UI Toolkit ships built-in controls:
 *
 * - <number-flow value="1234" precision="1" grouping="" prefix="$"
 *   suffix="%"/>: NumberFlow. Digits roll to each new value; bind it with
 *   data-attr-value="expression".
 * - <morph-text>{{ expression }}</morph-text>: Torph. When its text
 *   changes, kept characters (or words) slide, removed ones fade and new
 *   ones fade in.
 * - presence on a data-if element: Motion's AnimatePresence. It shows with
 *   data-starting-style for one frame and leaves with data-ending-style
 *   until the transitions RCSS declares for it finish, as the web's
 *   @starting-style and exit animations do.
 *
 * Each follows the context's reduced-motion theme (@media (theme:
 * reduced-motion)) and changes at once while hidden. */

#include "ui/rml.h"

#include <stdbool.h>

/* A spring's state along one axis, as displacement from its target. */
typedef struct spring
{
    double x, v;
} spring;

typedef struct spring_params
{
    double stiffness, damping, mass;
} spring_params;

/* Motion's visual duration and bounce as physics. */
spring_params spring_for(double visual_duration, double bounce);
/* Steps the spring exactly over dt seconds, so frame rate never changes its
 * path; it rests at 0 once close. */
void spring_step(spring *s, spring_params p, double dt);

/* Whether the element's context has Reduce Motion on. */
bool motion_reduced(const rml_element *element);
/* The longest transition RCSS declares for the element, with its delay. */
double motion_transition_time(const rml_element *element);

/* Registers the elements and presence; the backend does once RmlUi starts. */
void motion_register();
void text_motion_register(); /* <number-flow> and <morph-text> */
/* Presence's work once `context` has updated: starting styles give way to
 * transitions, and exits whose transitions finished hide. */
void presence_settle(rml_context *context);
/* Sets a <morph-text>'s text, as its content does. */
void morph_text_set(rml_element *element, const char *text);

#endif
