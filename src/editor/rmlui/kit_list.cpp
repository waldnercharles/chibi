/* Virtualized lists after Virtuoso: a scroll container sized for every row, of
 * which only the visible range plus overscan is emitted. Rows have one fixed
 * height, as kit rows do. Following lists (logs) stay pinned to the bottom only
 * while they were already within 4 dp of it; reveal keeps a row in view with the
 * least scrolling. */
#include "editor/rmlui/kit_internal.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace kit_impl;

namespace {

constexpr float kBottomThreshold = 4;
constexpr float kDefaultOverscan = 120;

} // namespace

extern "C" bool kit_list_begin(kit *k, const char *key, const char *cls, const kit_list_desc *d, int *first, int *last)
{
    const kit_class list[] = { { cls, cls != nullptr } };
    Node *n = emit(k, key, Kind::List, "div", kit_classes(k, "vlist", list, 1));
    Widget &w = n->w;
    if (!w.body) {
        w.body = raw(k, n->el, "div", "vlist-content");
        n->container = w.body;
        w.pending_index = -1;
        w.at_bottom = true;
    }
    float s = dp(k);
    float h = std::max(d->row_height, 1.0f);
    float total = d->count * h;
    float view = n->el->GetClientHeight() / s;
    float scroll = n->el->GetScrollTop() / s;
    float previous_max = std::max(0.0f, (float)w.value - view);
    /* Pinned when the user left it at the bottom of the previous content. */
    if (d->follow && w.at_bottom && total > w.value) {
        scroll = std::max(0.0f, total - view);
    } else {
        w.at_bottom = scroll >= previous_max - kBottomThreshold;
    }
    if (d->reveal >= 0 && d->reveal < d->count && d->reveal != w.pending_index) {
        float top = d->reveal * h;
        if (top < scroll) {
            scroll = top;
        } else if (top + h > scroll + view) {
            scroll = top + h - view;
        }
    }
    w.pending_index = d->reveal;
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, total - view));
    if (std::fabs(scroll * s - n->el->GetScrollTop()) >= 1) {
        n->el->SetScrollTop(scroll * s);
    }
    w.value = total;
    float overscan = d->overscan > 0 ? d->overscan : kDefaultOverscan;
    int from = std::max(0, (int)std::floor((scroll - overscan) / h));
    /* Before the first layout the viewport is unknown; emit a screenful. */
    float extent = view > 0 ? view : 800;
    int to = std::min(d->count - 1, (int)std::ceil((scroll + extent + overscan) / h));
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.2fdp", total);
    w.body->SetProperty("height", buffer);
    std::snprintf(buffer, sizeof buffer, "%.2fdp", from * h);
    w.body->SetProperty("padding-top", buffer);
    push_node(k, n);
    *first = from;
    *last = to;
    return from <= to;
}

extern "C" void kit_list_end(kit *k)
{
    assert(k->stack.back()->kind == Kind::List);
    pop_node(k);
}
