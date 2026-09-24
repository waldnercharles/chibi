/* Toasts after Sonner: a bottom-right stack of at most three visible toasts that
 * live four seconds. Collapsed, older toasts sit behind the front one, lifted by
 * the gap and scaled down 5% per step; hovering expands the stack to full
 * heights and pauses every timer. A drag to the right dismisses past 45 dp or
 * 0.11 dp/ms. Each toast may carry one action, such as Undo. Motion uses the
 * kit's springs, so a toast retargets from wherever it is. */
#include "editor/rmlui/kit_internal.hpp"

#include "editor/ui/kit_icons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kit_impl {

namespace {

constexpr int kVisible = 3;
constexpr double kLifetime = 4.0;
constexpr float kGap = 14;
constexpr float kSwipeDistance = 45;
constexpr float kSwipeVelocity = 0.11f * 1000; /* dp per second */
const SpringParams kToastSpring = spring_params(0.4, 0);

uint32_t type_icon(kit_toast_type type)
{
    switch (type) {
        case KIT_TOAST_SUCCESS: return KIT_ICON_PASS;
        case KIT_TOAST_INFO: return KIT_ICON_INFO;
        case KIT_TOAST_WARNING: return KIT_ICON_WARNING;
        case KIT_TOAST_ERROR: return KIT_ICON_ERROR;
        case KIT_TOAST_LOADING: return KIT_ICON_LOADING;
        default: return 0;
    }
}

const char *type_name(kit_toast_type type)
{
    static const char *const names[] = { "default", "success", "info", "warning", "error", "loading" };
    return names[type];
}

/* Springs track displacement from a target; move the target, keep the path. */
void retarget(Spring &s, double &target, double next)
{
    s.x += target - next;
    target = next;
}

void step(Spring &s, double dt, kit *k)
{
    if (k->reduced_motion) {
        s = Spring{};
        return;
    }
    spring_step(s, kToastSpring, dt);
}

} // namespace

void toasts_frame(kit *k)
{
    double now = k->in.time;
    double dt = std::clamp(now - k->toast_time, 0.0, 0.1);
    k->toast_time = now;
    for (auto it = k->toast_actions.begin(); it != k->toast_actions.end();) {
        it = k->frame - it->second > 2 ? k->toast_actions.erase(it) : std::next(it);
    }
    if (k->toasts.empty()) {
        return;
    }
    Node *root = emit_in(k, k->layer, "@toaster", Kind::Box, "section", "toaster");
    kit_rect area = element_rect(k, root->el);
    /* The pointer expands the stack while it is over the stack's current extent. */
    float top = 0;
    int shown = 0;
    for (const Toast &t : k->toasts) {
        if (!t.removing && shown < kVisible) {
            top = std::min(top, (float)(t.y_target - t.height));
            ++shown;
        }
    }
    bool over = k->in.mouse_x >= area.x && k->in.mouse_x <= area.x + area.w && k->in.mouse_y <= area.y &&
                k->in.mouse_y >= area.y + top - kGap;
    bool swiping = false;
    for (const Toast &t : k->toasts) {
        swiping |= t.swiping;
    }
    k->toasts_expanded = shown > 1 && (over || swiping);
    if (shown <= 1) {
        k->toasts_expanded = false;
    }
    k->stack.push_back(k->layer); /* emitted after the root closed; keep the layer's cursor */
    push_node(k, root);
    /* Newest first: index 0 is the front toast. */
    float lift = 0;
    int index = 0;
    for (auto it = k->toasts.rbegin(); it != k->toasts.rend(); ++it) {
        Toast &t = *it;
        char key[24];
        std::snprintf(key, sizeof key, "t%d", t.id);
        Node *n = emit(k, key, Kind::Box, "div", "toast");
        Widget &w = n->w;
        if (!w.label_el) {
            w.icon_el = raw_icon(k, n->el, "lead", 0);
            Rml::Element *content = raw(k, n->el, "div", "content");
            w.label_el = raw(k, content, "span", "title");
            w.meta_el = raw(k, content, "span", "description");
        }
        set_attr(n, "data-type", type_name(t.type));
        uint32_t icon = type_icon(t.type);
        if (w.icon != icon) {
            w.icon = icon;
            w.icon_el->SetInnerRML(icon ? utf8(icon) : "");
            w.icon_el->SetProperty("display", icon ? "inline-block" : "none");
        }
        set_element_text(w.label_el, t.title);
        set_element_text(w.meta_el, t.description);
        w.meta_el->SetProperty("display", t.description.empty() ? "none" : "block");
        push_node(k, n);
        if (!t.action.empty()) {
            kit_button_desc action{};
            action.label = t.action.c_str();
            action.variant = KIT_SUBTLE;
            action.size = KIT_SM;
            action.cls = "toast-action";
            if (kit_button(k, "action", &action) && !t.removing) {
                k->toast_actions[t.id] = k->frame;
                t.removing = true;
            }
        }
        if (t.duration < 0) {
            kit_button_desc close{};
            close.icon = KIT_ICON_CLOSE;
            close.size = KIT_SM;
            close.cls = "toast-close";
            close.tip = "Close";
            if (kit_button(k, "close", &close)) {
                t.removing = true;
            }
        }
        pop_node(k);
        /* Swipe: press anywhere but a button, drag right. */
        bool pressed = n->events & EV_PRESS;
        Rml::Element *hover = k->context->GetHoverElement();
        bool on_button = false;
        for (Rml::Element *el = hover; el && el != n->el; el = el->GetParentNode()) {
            on_button |= el->GetTagName() == "button";
        }
        if (pressed && !on_button && !t.removing) {
            t.swiping = true;
            t.press_x = k->in.mouse_x;
            t.swipe_velocity = 0;
        }
        if (t.swiping) {
            float d = k->in.mouse_x - t.press_x;
            float offset = d >= 0 ? d : d / (1.5f + std::fabs(d) / 20);
            if (dt > 0) {
                t.swipe_velocity = (float)((offset - t.x.x) / dt);
            }
            t.x = Spring{ offset, 0 };
            if (!k->in.down[0]) {
                t.swiping = false;
                if (offset >= kSwipeDistance || t.swipe_velocity >= kSwipeVelocity) {
                    t.removing = true;
                    t.swiped = true;
                }
            }
        }
        kit_rect r = element_rect(k, n->el);
        if (!t.forced_height && r.h > 0) {
            t.height = r.h;
        }
        bool front = index == 0;
        bool visible = index < kVisible;
        double y, scale, opacity;
        if (t.removing) {
            y = t.swiped ? t.y_target : t.y_target + t.height * 0.4;
            scale = t.scale_target;
            opacity = 0;
        } else if (k->toasts_expanded) {
            y = -lift;
            scale = 1;
            opacity = visible ? 1 : 0;
        } else {
            y = -kGap * index;
            scale = 1 - 0.05 * index;
            opacity = visible ? 1 : 0;
        }
        if (!t.placed) {
            /* Enter from one height below, as Sonner's translateY(100%). */
            t.placed = true;
            t.y_target = y;
            t.y = Spring{ t.height > 0 ? t.height : 48, 0 };
            t.scale_target = scale;
            t.opacity_target = opacity;
            t.opacity = Spring{ -opacity, 0 };
        }
        retarget(t.y, t.y_target, y);
        retarget(t.scale, t.scale_target, scale);
        retarget(t.opacity, t.opacity_target, opacity);
        if (t.removing && t.swiped) {
            retarget(t.x, t.x_target, 360);
        } else if (!t.swiping) {
            retarget(t.x, t.x_target, 0);
        }
        step(t.y, dt, k);
        step(t.scale, dt, k);
        step(t.opacity, dt, k);
        if (!t.swiping) {
            step(t.x, dt, k);
        }
        /* Collapsed toasts behind the front take its height and hide their content. */
        bool behind = !front && !k->toasts_expanded && !t.removing;
        t.forced_height = behind && k->front_height > 0;
        char buffer[96];
        if (t.forced_height) {
            std::snprintf(buffer, sizeof buffer, "%.1fdp", k->front_height);
            n->el->SetProperty("height", buffer);
        } else {
            n->el->RemoveProperty("height");
        }
        set_flag(n, "data-behind", behind);
        set_flag(n, "data-front", front);
        std::snprintf(
            buffer,
            sizeof buffer,
            "translate(%.2fdp, %.2fdp) scale(%.4f)",
            t.x_target + t.x.x,
            t.y_target + t.y.x,
            t.scale_target + t.scale.x
        );
        n->el->SetProperty("transform", buffer);
        std::snprintf(buffer, sizeof buffer, "%.3f", std::clamp(t.opacity_target + t.opacity.x, 0.0, 1.0));
        n->el->SetProperty("opacity", buffer);
        n->el->SetProperty("pointer-events", visible && !t.removing ? "auto" : "none");
        char z[16];
        std::snprintf(z, sizeof z, "%d", 100 - index);
        n->el->SetProperty("z-index", z);
        if (front && !t.removing) {
            k->front_height = t.height;
        }
        if (!t.removing) {
            lift += t.height + kGap;
            ++index;
        }
        /* Timers pause while the stack is expanded or the pointer is swiping. */
        if (!t.removing && !k->toasts_expanded && !t.swiping && t.duration >= 0 && t.type != KIT_TOAST_LOADING) {
            t.remaining -= dt;
            if (t.remaining <= 0) {
                t.removing = true;
            }
        }
    }
    pop_node(k);
    k->stack.pop_back();
    k->toasts.erase(
        std::remove_if(
            k->toasts.begin(),
            k->toasts.end(),
            [&](const Toast &t) {
                return t.removing && (k->reduced_motion || (t.opacity_target + t.opacity.x) <= 0.01);
            }
        ),
        k->toasts.end()
    );
}

} // namespace kit_impl

using namespace kit_impl;

extern "C" int kit_toast(kit *k, int id, const kit_toast_desc *d)
{
    Toast *t = nullptr;
    for (Toast &existing : k->toasts) {
        if (existing.id == id && !existing.removing) {
            t = &existing;
        }
    }
    if (!t) {
        k->toasts.push_back(Toast{});
        t = &k->toasts.back();
        t->id = ++k->toast_next;
    }
    t->type = d->type;
    t->title = d->title ? d->title : "";
    t->description = d->description ? d->description : "";
    t->action = d->action ? d->action : "";
    t->duration = d->duration < 0 ? -1 : d->duration > 0 ? d->duration : kLifetime;
    t->remaining = t->duration;
    return t->id;
}

extern "C" void kit_toast_dismiss(kit *k, int id)
{
    for (Toast &t : k->toasts) {
        if (t.id == id) {
            t.removing = true;
        }
    }
}

extern "C" bool kit_toast_action(kit *k, int id)
{
    auto found = k->toast_actions.find(id);
    if (found == k->toast_actions.end()) {
        return false;
    }
    k->toast_actions.erase(found);
    return true;
}
