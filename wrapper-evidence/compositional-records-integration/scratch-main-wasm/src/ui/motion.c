/* Springs, and presence for data-if (see motion.h).
 *
 * Presence: when a data-if binding would hide an element that has the
 * presence attribute, the element keeps its display, inline, so the binding
 * still reads it as hidden, and gets data-ending-style until its
 * transitions finish; then it hides. When the binding shows it again, it
 * shows with data-starting-style and no transition for one frame, and the
 * next frame transitions away from it. Showing it while it leaves turns it
 * back from where it is. */
#include "ui/motion.h"

#include "support/array.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Springs ---------- */

spring_params spring_for(double visual_duration, double bounce)
{
    double root = 2 * M_PI / (visual_duration * 1.2);
    double stiffness = root * root;
    return (spring_params){
        .stiffness = stiffness,
        .damping = 2 * math_clamp(1 - bounce, 0.05, 1.0) * sqrt(stiffness),
        .mass = 1,
    };
}

static spring underdamped(spring s, double w0, double zeta, double dt)
{
    double a = zeta * w0, wd = w0 * sqrt(1 - zeta * zeta);
    double e = exp(-a * dt), c = cos(wd * dt), n = sin(wd * dt);
    return (spring){
        e * (s.x * c + (s.v + a * s.x) / wd * n),
        e * (s.v * c - (a * s.v + w0 * w0 * s.x) / wd * n),
    };
}

static spring critically_damped(spring s, double w0, double dt)
{
    double e = exp(-w0 * dt);
    return (spring){
        e * (s.x + (s.v + w0 * s.x) * dt),
        e * (s.v - w0 * (s.v + w0 * s.x) * dt),
    };
}

static spring overdamped(spring s, double w0, double zeta, double dt)
{
    double root = sqrt(zeta * zeta - 1);
    double r1 = -w0 * (zeta - root), r2 = -w0 * (zeta + root);
    double c2 = (s.v - r1 * s.x) / (r2 - r1), c1 = s.x - c2;
    double e1 = exp(r1 * dt), e2 = exp(r2 * dt);
    return (spring){ c1 * e1 + c2 * e2, r1 * c1 * e1 + r2 * c2 * e2 };
}

/* Exact solution of m x'' + c x' + k x = 0 over dt. */
static spring spring_solve(spring s, spring_params p, double dt)
{
    double w0 = sqrt(p.stiffness / p.mass);
    double zeta = p.damping / (2 * sqrt(p.stiffness * p.mass));
    if (zeta < 1) {
        return underdamped(s, w0, zeta, dt);
    }
    if (zeta == 1) {
        return critically_damped(s, w0, dt);
    }
    return overdamped(s, w0, zeta, dt);
}

static bool spring_resting(spring s)
{
    return fabs(s.x) < 0.05 && fabs(s.v) < 0.5;
}

void spring_step(spring *s, spring_params p, double dt)
{
    if (dt <= 0) {
        return;
    }
    *s = spring_solve(*s, p, dt);
    if (spring_resting(*s)) {
        *s = (spring){ 0 };
    }
}

/* ---------- Queries ---------- */

bool motion_reduced(const rml_element *element)
{
    return rml_context_theme_active(rml_element_context(element), "reduced-motion");
}

double motion_transition_time(const rml_element *element)
{
    int count = rml_element_transitions(element, NULL, 0);
    if (!count) {
        return 0;
    }
    rml_transition *list = xmalloc((size_t)count * sizeof *list);
    rml_element_transitions(element, list, count);
    double longest = 0;
    for (int i = 0; i < count; i++) {
        longest = math_max((double)(list[i].duration + list[i].delay), longest);
    }
    free(list);
    return longest;
}

/* ---------- Presence ---------- */

typedef enum presence_phase
{
    PRESENCE_SETTLE, /* its inline transition: none goes at the next settle */
    PRESENCE_EXIT,   /* shown with data-ending-style until `until` */
} presence_phase;

typedef struct presence
{
    rml_element_ref *ref;
    rml_context *context;
    presence_phase phase;
    double until; /* negative until its exit transitions are known */
} presence;

/* Elements between states, in every context. */
static presence *presences;

static int presence_find(const rml_element *element)
{
    for (int i = 0; i < acount(presences); i++) {
        if (rml_element_ref_get(presences[i].ref) == element) {
            return i;
        }
    }
    return -1;
}

static void presence_drop(int i)
{
    rml_element_ref_destroy(presences[i].ref);
    adel(presences, i);
}

static void presence_track(rml_element *element, presence_phase phase)
{
    int i = presence_find(element);
    if (i < 0) {
        apush(
            presences,
            ((presence){ .ref = rml_element_ref_create(element),
                         .context = rml_element_context(element) })
        );
        i = acount(presences) - 1;
    }
    presences[i].phase = phase;
    presences[i].until = -1;
}

/* Styles without transitions until the next settle. */
static void hold_transitions(rml_element *element)
{
    rml_element_set_property(element, "transition", "none");
    presence_track(element, PRESENCE_SETTLE);
}

/* It leaves shown, with its display inline so data-if reads it as hidden. */
static void presence_exit(rml_element *element)
{
    char display[32];
    rml_element_property(element, "display", display, sizeof display);
    rml_element_set_property(element, "display", display);
    rml_element_remove_attribute(element, "data-starting-style");
    rml_element_remove_property(element, "transition");
    rml_element_set_attribute(element, "data-ending-style", "");
    rml_element_set_property(element, "pointer-events", "none");
    presence_track(element, PRESENCE_EXIT);
}

static void presence_enter(rml_element *element)
{
    int i = presence_find(element);
    bool leaving = i >= 0 && presences[i].phase == PRESENCE_EXIT;
    rml_element_remove_property(element, "display");
    if (leaving) {
        rml_element_remove_attribute(element, "data-ending-style");
        rml_element_remove_property(element, "pointer-events");
        presence_drop(i);
        return;
    }
    rml_element_set_attribute(element, "data-starting-style", "");
    hold_transitions(element);
}

static bool on_data_if(rml_element *element, bool show, bool first)
{
    if (first || !rml_element_has_attribute(element, "presence") ||
        motion_reduced(element)) {
        return false;
    }
    if (show) {
        presence_enter(element);
    } else {
        presence_exit(element);
    }
    rml_context_request_next_update(rml_element_context(element), 0);
    return true;
}

/* An exit whose transitions finished hides, and its styles settle without
 * transitions. */
static void presence_hide(rml_element *element)
{
    rml_element_set_property(element, "display", "none");
    rml_element_remove_attribute(element, "data-ending-style");
    rml_element_remove_property(element, "pointer-events");
    hold_transitions(element);
}

/* False once the element is done; an exit that hides settles at the next
 * call. */
static bool presence_step(presence *p, rml_element *element, double now)
{
    if (p->phase == PRESENCE_SETTLE) {
        rml_element_remove_attribute(element, "data-starting-style");
        rml_element_remove_property(element, "transition");
        return false;
    }
    if (p->until < 0) {
        p->until = now + motion_transition_time(element);
    }
    if (now < p->until) {
        rml_context_request_next_update(p->context, p->until - now);
        return true;
    }
    presence_hide(element);
    rml_context_request_next_update(p->context, 0);
    return true;
}

void presence_settle(rml_context *context)
{
    double now = rml_elapsed_time();
    for (int i = 0; i < acount(presences);) {
        rml_element *element = rml_element_ref_get(presences[i].ref);
        if (element && (presences[i].context != context ||
                        presence_step(&presences[i], element, now))) {
            i++;
            continue;
        }
        presence_drop(i);
    }
}

void motion_register()
{
    text_motion_register();
    rml_set_data_if_handler(on_data_if);
}
