/* Motion for the Studio kit, after Motion (motion.dev) and Base UI:
 *
 * - Presence: a node marked with kit_presence mounts with data-starting-style
 *   and, when no longer emitted, stays with data-ending-style until the
 *   transitions RCSS declares for it finish. Styling stays in RCSS.
 * - Layout: a node marked with kit_layout animates changes to its position
 *   within its parent with a spring (FLIP), keeping velocity when retargeted.
 * - Springs: analytic damped oscillators, parameterized like Motion by visual
 *   duration and bounce. Reduced motion settles every animation immediately. */
#include "editor/rmlui/kit_internal.h"
#include "support/math.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
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

/* Exact solution of m x'' + c x' + k x = 0 over dt, so frame rate never
 * changes the path. */
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

/* ---------- Presence ---------- */

/* The longest transition RCSS declares for the element, including delay. */
static double exit_duration(const rml_element *el)
{
    int count = rml_element_transitions(el, NULL, 0);
    if (!count) {
        return 0;
    }
    rml_transition *list = xmalloc((size_t)count * sizeof *list);
    rml_element_transitions(el, list, count);
    double longest = 0;
    for (int i = 0; i < count; i++) {
        longest = math_max((double)(list[i].duration + list[i].delay), longest);
    }
    free(list);
    return longest;
}

/* Drop exiting nodes whose elements are about to go with an ancestor. */
static void release_exiting_within(kit *k, rml_element *ancestor)
{
    int kept = 0;
    for (int i = 0; i < acount(k->exiting); i++) {
        exiting e = k->exiting[i];
        if (within(e.node->el, ancestor)) {
            node_free(e.node);
            continue;
        }
        k->exiting[kept++] = e;
    }
    asetlen(k->exiting, kept);
}

static void remove_element(kit *k, rml_element *el)
{
    release_exiting_within(k, el);
    rml_element *parent = rml_element_parent(el);
    if (parent) {
        rml_element_remove_child(parent, el);
    }
}

void retire(kit *k, node *n)
{
    unmap(k, n);
    rml_element *el = n->el;
    double duration = n->presence && !k->reduced_motion ? exit_duration(el) : 0;
    if (duration <= 0 || !rml_element_parent(el)) {
        remove_element(k, el);
        node_free(n);
        return;
    }
    rml_element_remove_attribute(el, "data-starting-style");
    rml_element_set_attribute(el, "data-ending-style", "");
    rml_element_set_property(el, "pointer-events", "none");
    apush(k->exiting, (exiting){ n, k->in.time + duration });
}

void presence(kit *k, node *n)
{
    if (n->presence) {
        return;
    }
    n->presence = true;
    if (n->created != k->frame || k->reduced_motion) {
        return;
    }
    rml_element_set_attribute(n->el, "data-starting-style", "");
    apush(k->entering, n);
}

void forget_motion(kit *k, node *n)
{
    if (n->presence) {
        nodes_remove(k->entering, n);
    }
    if (n->layout) {
        nodes_remove(k->layout_nodes, n);
    }
    if (n->flow || n->morph) {
        nodes_remove(k->text_motion, n);
    }
}

/* ---------- Frame ---------- */

static void expire_springs(kit *k)
{
    for (int i = 0; i < cf_map_size(k->springs);) {
        if (k->springs[i].frame + 2 >= k->frame) {
            i++;
            continue;
        }
        free(k->springs[i].key);
        uint64_t key = cf_map_keys(k->springs)[i];
        cf_map_del(k->springs, key);
    }
}

/* Starting styles apply for one styled frame, then transition away. */
static void settle_entering(kit *k)
{
    int kept = 0;
    for (int i = 0; i < acount(k->entering); i++) {
        node *n = k->entering[i];
        if (n->created >= k->frame) {
            k->entering[kept++] = n;
            continue;
        }
        rml_element_remove_attribute(n->el, "data-starting-style");
    }
    asetlen(k->entering, kept);
}

static void finish_exits(kit *k, double now)
{
    for (int i = 0; i < acount(k->exiting);) {
        if (k->exiting[i].until > now) {
            i++;
            continue;
        }
        node *n = k->exiting[i].node;
        int count = acount(k->exiting);
        memmove(
            &k->exiting[i],
            &k->exiting[i + 1],
            (size_t)(count - 1 - i) * sizeof *k->exiting
        );
        asetlen(k->exiting, count - 1);
        rml_element *el = n->el;
        node_free(n);
        remove_element(k, el);
    }
}

static kit_rect relative_rect(const kit *k, rml_element *el)
{
    rml_vec2 offset = rml_element_relative_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    float s = dp(k);
    return (kit_rect){ offset.x / s, offset.y / s, size.x / s, size.y / s };
}

static void apply_offset(node *n)
{
    if (n->layout_x.x == 0 && n->layout_y.x == 0) {
        rml_element_remove_property(n->el, "transform");
        return;
    }
    char buffer[64];
    snprintf(
        buffer,
        sizeof buffer,
        "translate(%.2fdp, %.2fdp)",
        n->layout_x.x,
        n->layout_y.x
    );
    rml_element_set_property(n->el, "transform", buffer);
}

/* Offsets a moved node back to where it was, then lets the spring carry it
 * home. */
static void layout_step(kit *k, node *n, spring_params p, double dt)
{
    kit_rect r = relative_rect(k, n->el);
    if (n->layout_known && !k->reduced_motion) {
        n->layout_x.x += n->layout_rect.x - r.x;
        n->layout_y.x += n->layout_rect.y - r.y;
    }
    n->layout_rect = r;
    n->layout_known = true;
    if (k->reduced_motion) {
        n->layout_x = n->layout_y = (spring){ 0 };
    }
    spring_step(&n->layout_x, p, dt);
    spring_step(&n->layout_y, p, dt);
    apply_offset(n);
}

void motion_frame(kit *k)
{
    expire_springs(k);
    double now = k->in.time;
    double dt = math_clamp(now - k->motion_time, 0.0, 0.1);
    k->motion_time = now;
    settle_entering(k);
    finish_exits(k, now);
    if (!acount(k->layout_nodes) && !k->layout_pass) {
        text_frame(k, dt);
        return;
    }
    /* FLIP: measure this frame's layout before it renders. */
    rml_document_update(k->doc);
    k->layout_pass = false;
    text_frame(k, dt);
    spring_params p = spring_for(0.35, 0);
    for (int i = 0; i < acount(k->layout_nodes); i++) {
        layout_step(k, k->layout_nodes[i], p, dt);
    }
}

/* ---------- API ---------- */

void kit_presence(kit *k)
{
    assert(k->last);
    presence(k, k->last);
}

/* Settles the spring at path on target; takes path. */
static void spring_reset(kit *k, spring_value *v, char *path, float target)
{
    spring_value settled = {
        .key = path,
        .target = target,
        .time = k->in.time,
        .frame = k->frame,
    };
    if (!v) {
        cf_map_set(k->springs, str_hash(path), settled);
        return;
    }
    free(v->key);
    *v = settled;
}

float kit_spring(kit *k, const char *key, float target, float duration)
{
    char *path = NULL;
    str_catf(&path, "%s/~%s", acount(k->stack) ? alast(k->stack)->path : "", key);
    spring_value *v = cf_map_get_ptr(k->springs, str_hash(path));
    assert(!v || str_eq(v->key, path));
    if (!v || k->reduced_motion || duration <= 0) {
        spring_reset(k, v, path, target);
        return target;
    }
    free(path);
    if (v->duration != duration) {
        v->duration = duration;
        v->params = spring_for(duration, 0);
    }
    v->s.x += v->target - target;
    v->target = target;
    spring_step(&v->s, v->params, math_clamp(k->in.time - v->time, 0.0, 0.1));
    v->time = k->in.time;
    v->frame = k->frame;
    return (float)(v->target + v->s.x);
}

void kit_layout(kit *k)
{
    node *n = k->last;
    assert(n);
    if (n->layout) {
        return;
    }
    n->layout = true;
    apush(k->layout_nodes, n);
}

/* ---------- Idle ---------- */

double kit_idle_time(const kit *k)
{
    return rml_context_next_update_delay(k->context);
}
