/* Motion for the Studio kit, after Motion (motion.dev) and Base UI:
 *
 * - Presence: a node marked with kit_presence mounts with data-starting-style
 *   and, when no longer emitted, stays with data-ending-style until the
 *   transitions RCSS declares for it finish. Styling stays in RCSS.
 * - Layout: a node marked with kit_layout animates changes to its position
 *   within its parent with a spring (FLIP), keeping velocity when retargeted.
 * - Springs: analytic damped oscillators, parameterized like Motion by visual
 *   duration and bounce. Reduced motion settles every animation immediately. */
#include "editor/rmlui/kit_internal.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace kit_impl {

SpringParams spring_params(double visual_duration, double bounce)
{
    double root = 2 * M_PI / (visual_duration * 1.2);
    SpringParams p;
    p.stiffness = root * root;
    p.damping = 2 * std::clamp(1 - bounce, 0.05, 1.0) * std::sqrt(p.stiffness);
    p.mass = 1;
    return p;
}

/* Exact solution of m x'' + c x' + k x = 0 over dt, so frame rate never changes the path. */
void spring_step(Spring &s, const SpringParams &p, double dt)
{
    if (dt <= 0) {
        return;
    }
    double w0 = std::sqrt(p.stiffness / p.mass);
    double zeta = p.damping / (2 * std::sqrt(p.stiffness * p.mass));
    double x0 = s.x, v0 = s.v;
    if (zeta < 1) {
        double a = zeta * w0, wd = w0 * std::sqrt(1 - zeta * zeta);
        double e = std::exp(-a * dt), c = std::cos(wd * dt), n = std::sin(wd * dt);
        s.x = e * (x0 * c + (v0 + a * x0) / wd * n);
        s.v = e * (v0 * c - (a * v0 + w0 * w0 * x0) / wd * n);
    } else if (zeta == 1) {
        double e = std::exp(-w0 * dt);
        s.x = e * (x0 + (v0 + w0 * x0) * dt);
        s.v = e * (v0 - w0 * (v0 + w0 * x0) * dt);
    } else {
        double root = std::sqrt(zeta * zeta - 1);
        double r1 = -w0 * (zeta - root), r2 = -w0 * (zeta + root);
        double c2 = (v0 - r1 * x0) / (r2 - r1), c1 = x0 - c2;
        double e1 = std::exp(r1 * dt), e2 = std::exp(r2 * dt);
        s.x = c1 * e1 + c2 * e2;
        s.v = r1 * c1 * e1 + r2 * c2 * e2;
    }
    if (spring_resting(s)) {
        s = Spring{};
    }
}

bool spring_resting(const Spring &s)
{
    return std::fabs(s.x) < 0.05 && std::fabs(s.v) < 0.5;
}

namespace {

const SpringParams kLayoutSpring = spring_params(0.35, 0);

/* The longest transition RCSS declares for the element, including delay. */
double exit_duration(Rml::Element *el)
{
    const Rml::TransitionList *list = el->GetComputedValues().transition();
    if (!list || list->none) {
        return 0;
    }
    double longest = 0;
    for (const Rml::Transition &t : list->transitions) {
        longest = std::max(longest, (double)(t.duration + t.delay));
    }
    return longest;
}

/* Drop exiting nodes whose elements are about to go with an ancestor. */
void release_exiting_within(kit *k, Rml::Element *ancestor)
{
    k->exiting.erase(
        std::remove_if(
            k->exiting.begin(),
            k->exiting.end(),
            [&](const Exiting &e) { return within(e.node->el, ancestor); }
        ),
        k->exiting.end()
    );
}

void remove_element(kit *k, Rml::Element *el)
{
    release_exiting_within(k, el);
    if (el->GetParentNode()) {
        el->GetParentNode()->RemoveChild(el);
    }
}

void apply_offset(Node *n)
{
    if (n->layout_x.x == 0 && n->layout_y.x == 0) {
        n->el->RemoveProperty("transform");
        return;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "translate(%.2fdp, %.2fdp)", n->layout_x.x, n->layout_y.x);
    n->el->SetProperty("transform", buffer);
}

kit_rect relative_rect(const kit *k, Rml::Element *el)
{
    Rml::Vector2f offset = el->GetRelativeOffset(Rml::BoxArea::Border);
    Rml::Vector2f size = el->GetBox().GetSize(Rml::BoxArea::Border);
    float s = dp(k);
    return { offset.x / s, offset.y / s, size.x / s, size.y / s };
}

} // namespace

void retire(kit *k, std::unique_ptr<Node> node)
{
    Node *n = node.get();
    unmap(k, n);
    Rml::Element *el = n->el;
    double duration = n->presence && !k->reduced_motion ? exit_duration(el) : 0;
    if (duration <= 0 || !el->GetParentNode()) {
        remove_element(k, el);
        return;
    }
    el->RemoveAttribute("data-starting-style");
    el->SetAttribute("data-ending-style", Rml::String());
    el->SetProperty("pointer-events", "none");
    k->exiting.push_back({ std::move(node), k->in.time + duration });
}

void presence(kit *k, Node *n)
{
    if (n->presence) {
        return;
    }
    n->presence = true;
    if (n->created == k->frame && !k->reduced_motion) {
        n->el->SetAttribute("data-starting-style", Rml::String());
        k->entering.push_back(n);
    }
}

void forget_motion(kit *k, Node *n)
{
    if (n->presence) {
        k->entering.erase(std::remove(k->entering.begin(), k->entering.end(), n), k->entering.end());
    }
    if (n->layout) {
        k->layout_nodes.erase(std::remove(k->layout_nodes.begin(), k->layout_nodes.end(), n), k->layout_nodes.end());
    }
    if (n->flow || n->morph) {
        k->text_motion.erase(std::remove(k->text_motion.begin(), k->text_motion.end(), n), k->text_motion.end());
    }
}

void motion_frame(kit *k)
{
    for (auto it = k->springs.begin(); it != k->springs.end();) {
        it = it->second.frame + 2 < k->frame ? k->springs.erase(it) : std::next(it);
    }
    double now = k->in.time;
    double dt = std::clamp(now - k->motion_time, 0.0, 0.1);
    k->motion_time = now;
    /* Starting styles apply for one styled frame, then transition away. */
    k->entering.erase(
        std::remove_if(
            k->entering.begin(),
            k->entering.end(),
            [&](Node *n) {
                if (n->created >= k->frame) {
                    return false;
                }
                n->el->RemoveAttribute("data-starting-style");
                return true;
            }
        ),
        k->entering.end()
    );
    for (size_t i = 0; i < k->exiting.size();) {
        if (k->exiting[i].until > now) {
            ++i;
            continue;
        }
        Rml::Element *el = k->exiting[i].node->el;
        k->exiting.erase(k->exiting.begin() + (long)i);
        remove_element(k, el);
    }
    if (k->layout_nodes.empty() && !k->layout_pass) {
        text_frame(k, dt);
        return;
    }
    /* FLIP: measure this frame's layout before it renders, offset each moved node
       back to where it was, then let the spring carry it home. */
    k->doc->UpdateDocument();
    k->layout_pass = false;
    text_frame(k, dt);
    for (Node *n : k->layout_nodes) {
        kit_rect r = relative_rect(k, n->el);
        if (n->layout_known && !k->reduced_motion) {
            n->layout_x.x += n->layout_rect.x - r.x;
            n->layout_y.x += n->layout_rect.y - r.y;
        }
        n->layout_rect = r;
        n->layout_known = true;
        if (k->reduced_motion) {
            n->layout_x = n->layout_y = Spring{};
        }
        spring_step(n->layout_x, kLayoutSpring, dt);
        spring_step(n->layout_y, kLayoutSpring, dt);
        apply_offset(n);
    }
}

} // namespace kit_impl

using namespace kit_impl;

extern "C" void kit_presence(kit *k)
{
    assert(k->last);
    presence(k, k->last);
}

extern "C" float kit_spring(kit *k, const char *key, float target, float duration)
{
    std::string path = (k->stack.empty() ? std::string() : k->stack.back()->path) + "/~" + key;
    auto found = k->springs.find(path);
    if (found == k->springs.end() || k->reduced_motion || duration <= 0) {
        SpringValue &v = k->springs[path];
        v = SpringValue{};
        v.target = target;
        v.time = k->in.time;
        v.frame = k->frame;
        return target;
    }
    SpringValue &v = found->second;
    if (v.duration != duration) {
        v.duration = duration;
        v.params = spring_params(duration, 0);
    }
    v.s.x += v.target - target;
    v.target = target;
    spring_step(v.s, v.params, std::clamp(k->in.time - v.time, 0.0, 0.1));
    v.time = k->in.time;
    v.frame = k->frame;
    return (float)(v.target + v.s.x);
}

extern "C" void kit_layout(kit *k)
{
    Node *n = k->last;
    assert(n);
    if (!n->layout) {
        n->layout = true;
        k->layout_nodes.push_back(n);
    }
}
