/* Text that changes in place.
 *
 * NumberFlow: each digit is a wheel of cells 0-9 twice over, clipped to one cell.
 * Digits keep their place value, so 99 -> 100 adds a hundreds wheel instead of
 * renumbering. Every wheel spins with the trend (the sign of the change) and
 * wraps, 9 -> 0 forward; new places fade and widen in, lost places spin to 0 and
 * fade out.
 *
 * Torph: text is split into characters, or words when it has spaces, keyed by
 * text and occurrence. Kept segments slide from where they were (FLIP), new ones
 * fade in after a quarter of the duration, removed ones fade out in place over the
 * first quarter, and the box eases between widths. 400 ms, expo-out. */
#include "editor/rmlui/kit_internal.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace kit_impl {

namespace {

constexpr double kMorphDuration = 0.4;
constexpr double kAppearDuration = 0.45; /* NumberFlow opacity timing */
const SpringParams kSpinSpring = spring_params(0.6, 0);

/* cubic-bezier(x1, y1, x2, y2) at progress x, solved for the curve parameter. */
double bezier(double x1, double y1, double x2, double y2, double x)
{
    auto curve = [](double a, double b, double t) { return 3 * a * t * (1 - t) * (1 - t) + 3 * b * t * t * (1 - t) + t * t * t; };
    double t = x;
    for (int i = 0; i < 8; ++i) {
        double error = curve(x1, x2, t) - x;
        double slope = 3 * x1 * (1 - t) * (1 - t) + 6 * (x2 - x1) * t * (1 - t) + 3 * (1 - x2) * t * t;
        if (std::fabs(error) < 1e-5 || std::fabs(slope) < 1e-6) {
            break;
        }
        t = std::clamp(t - error / slope, 0.0, 1.0);
    }
    return curve(y1, y2, t);
}

double expo_out(double x)
{
    return bezier(0.19, 1, 0.22, 1, std::clamp(x, 0.0, 1.0));
}

double cubic_out(double x)
{
    x = 1 - std::clamp(x, 0.0, 1.0);
    return 1 - x * x * x;
}

void style(Rml::Element *el, const char *property, const char *format, double a, double b = 0, double c = 0)
{
    char buffer[96];
    std::snprintf(buffer, sizeof buffer, format, a, b, c);
    el->SetProperty(property, buffer);
}

void track(kit *k, Node *n)
{
    if (std::find(k->text_motion.begin(), k->text_motion.end(), n) == k->text_motion.end()) {
        k->text_motion.push_back(n);
    }
}

/* ---------- NumberFlow ---------- */

std::string format_flow(double value, const kit_number_flow_desc *d)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", std::max(d->precision, 0), std::fabs(value));
    std::string digits = buffer;
    size_t point = digits.find('.');
    std::string whole = digits.substr(0, point), out;
    for (size_t i = 0; i < whole.size(); ++i) {
        if (d->grouping && i > 0 && (whole.size() - i) % 3 == 0) {
            out += ',';
        }
        out += whole[i];
    }
    if (point != std::string::npos) {
        out += digits.substr(point);
    }
    bool negative = value < 0 && out.find_first_not_of("0.,") != std::string::npos;
    return (negative ? "-" : "") + out;
}

Rml::Element *digit_column(kit *k, Node *n)
{
    Rml::Element *col = raw(k, n->el, "span", "nf-digit");
    Rml::Element *strip = raw(k, col, "span", "nf-strip");
    for (int i = 0; i < 20; ++i) {
        char cell[2] = { (char)('0' + i % 10), 0 };
        raw(k, strip, "span", "nf-cell", cell);
    }
    return col;
}

void spin_to(FlowDigit &g, int digit, int trend, bool instant)
{
    double current = std::fmod(g.target + g.pos.x, 10.0);
    if (current < 0) {
        current += 10;
    }
    double start = current, end = digit;
    if (trend > 0 && digit < current - 1e-6) {
        end = digit + 10;
    } else if (trend < 0) {
        start = current + 10;
        end = digit > current + 1e-6 ? digit : digit + 10;
    }
    if (instant) {
        g.target = end;
        g.pos = Spring{};
        return;
    }
    g.pos.x = start - end;
    g.target = end;
}

void flow_restructure(kit *k, Node *n, FlowState &f, const std::string &text, int trend, bool instant)
{
    /* Place values: units 0, tens 1, ...; tenths -1, ... */
    size_t point = text.find('.');
    int whole = 0;
    for (size_t i = 0; i < text.size() && (point == std::string::npos || i < point); ++i) {
        whole += text[i] >= '0' && text[i] <= '9';
    }
    std::vector<std::pair<int, int>> places; /* place, digit */
    int seen = 0, fraction = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            continue;
        }
        bool after = point != std::string::npos && i > point;
        places.emplace_back(after ? -(++fraction) : whole - 1 - seen++, text[i] - '0');
    }
    for (FlowDigit &g : f.digits) {
        bool kept = std::any_of(places.begin(), places.end(), [&](auto &p) { return p.first == g.place; });
        if (!kept && !g.leaving) {
            g.leaving = true;
            spin_to(g, 0, trend, instant);
        }
    }
    for (const auto &entry : places) {
        int place = entry.first, digit = entry.second;
        auto found = std::find_if(f.digits.begin(), f.digits.end(), [&](const FlowDigit &g) { return g.place == place; });
        if (found == f.digits.end()) {
            FlowDigit g;
            g.col = digit_column(k, n);
            g.strip = g.col->GetFirstChild();
            g.place = place;
            g.target = digit;
            g.appear = instant ? 1 : 0;
            f.digits.push_back(g);
            continue;
        }
        found->leaving = false;
        spin_to(*found, digit, trend, instant);
    }
    /* Order: prefix, sign, whole digits high to low with separators, point, fraction, suffix.
       Leaving digits stay beside the place they held. */
    for (Rml::Element *el : f.symbols) {
        n->el->RemoveChild(el);
    }
    f.symbols.clear();
    std::sort(f.digits.begin(), f.digits.end(), [](const FlowDigit &a, const FlowDigit &b) { return a.place > b.place; });
    auto symbol = [&](const std::string &s) {
        Rml::Element *el = raw(k, n->el, "span", "nf-symbol", s.c_str());
        f.symbols.push_back(el);
    };
    auto move_last = [&](Rml::Element *el) { n->el->AppendChild(n->el->RemoveChild(el)); };
    if (!f.prefix.empty()) {
        symbol(f.prefix);
    }
    if (!text.empty() && text[0] == '-') {
        symbol("-");
    }
    bool point_done = point == std::string::npos;
    for (FlowDigit &g : f.digits) {
        if (!point_done && g.place < 0) {
            symbol(".");
            point_done = true;
        }
        move_last(g.col);
        if (f.grouping && g.place > 0 && g.place % 3 == 0 && !g.leaving) {
            symbol(",");
        }
    }
    if (!point_done) {
        symbol(".");
    }
    if (!f.suffix.empty()) {
        symbol(f.suffix);
    }
}

bool flow_frame(kit *k, Node *n, double dt)
{
    FlowState &f = *n->flow;
    bool moving = false;
    for (FlowDigit &g : f.digits) {
        if (k->reduced_motion) {
            g.pos = Spring{};
            g.appear = g.leaving ? 0 : 1;
        }
        spring_step(g.pos, kSpinSpring, dt);
        if (g.pos.x == 0 && g.pos.v == 0) {
            g.target = std::fmod(g.target, 10.0);
        }
        double goal = g.leaving ? 0 : 1;
        g.appear = goal > g.appear ? std::min(goal, g.appear + dt / kAppearDuration) : std::max(goal, g.appear - dt / kAppearDuration);
        style(g.strip, "transform", "translateY(%.3f%%)", -(g.target + g.pos.x) * 5);
        double visible = cubic_out(g.appear);
        if (f.digit_w <= 0 && g.appear >= 1) {
            f.digit_w = element_rect(k, g.col).w;
        }
        if (g.appear < 1) {
            style(g.col, "opacity", "%.3f", visible);
            if (f.digit_w > 0) {
                style(g.col, "width", "%.2fdp", visible * f.digit_w);
            }
        } else {
            g.col->RemoveProperty("opacity");
            g.col->RemoveProperty("width");
        }
        moving |= g.pos.x != 0 || g.appear != goal;
    }
    for (size_t i = 0; i < f.digits.size();) {
        if (f.digits[i].leaving && f.digits[i].appear <= 0) {
            n->el->RemoveChild(f.digits[i].col);
            f.digits.erase(f.digits.begin() + (long)i);
            continue;
        }
        ++i;
    }
    return moving;
}

/* ---------- Torph ---------- */

std::vector<std::string> segment_text(const std::string &text)
{
    std::vector<std::string> out;
    bool words = text.find(' ') != std::string::npos;
    for (size_t i = 0; i < text.size();) {
        size_t j = i + 1;
        if (words) {
            bool space = text[i] == ' ';
            while (j < text.size() && (text[j] == ' ') == space) {
                ++j;
            }
        } else {
            while (j < text.size() && ((unsigned char)text[j] & 0xc0) == 0x80) {
                ++j;
            }
        }
        std::string segment = text.substr(i, j - i);
        if (words && segment[0] == ' ') {
            /* RmlUi collapses a box that holds only spaces; keep their width. */
            std::string spaces;
            for (size_t n = 0; n < segment.size(); ++n) {
                spaces += "\xc2\xa0";
            }
            segment = spaces;
        }
        out.push_back(segment);
        i = j;
    }
    return out;
}

std::vector<std::string> segment_ids(const std::vector<std::string> &segments)
{
    std::map<std::string, int> seen;
    std::vector<std::string> ids;
    for (const std::string &s : segments) {
        ids.push_back(s + "#" + std::to_string(seen[s]++));
    }
    return ids;
}

kit_rect offset_in(kit *k, Rml::Element *el, Rml::Element *box)
{
    kit_rect a = element_rect(k, el), b = element_rect(k, box);
    return { a.x - b.x, a.y - b.y, a.w, a.h };
}

void morph_set(kit *k, Node *n, MorphState &m, Rml::Element *box, const std::string &text)
{
    std::vector<std::string> segments = segment_text(text);
    std::vector<std::string> ids = segment_ids(segments);
    if (!m.init || k->reduced_motion) {
        while (box->GetNumChildren()) {
            box->RemoveChild(box->GetChild(0));
        }
        m.segs.clear();
        for (size_t i = 0; i < segments.size(); ++i) {
            m.segs.push_back({ ids[i], raw(k, box, "span", "morph-seg", segments[i].c_str()) });
        }
        m.init = true;
        m.text = text;
        m.start = -1;
        return;
    }
    /* Where everything is now, including any motion in flight. */
    float from_w = element_rect(k, box).w;
    for (MorphSeg &s : m.segs) {
        if (s.state == MorphSeg::Leave) {
            box->RemoveChild(s.el);
            s.el = nullptr;
            continue;
        }
        kit_rect r = offset_in(k, s.el, box);
        s.from_x = r.x + s.dx;
        s.from_y = r.y + s.dy;
    }
    m.segs.erase(std::remove_if(m.segs.begin(), m.segs.end(), [](const MorphSeg &s) { return !s.el; }), m.segs.end());
    std::vector<MorphSeg> next;
    for (size_t i = 0; i < segments.size(); ++i) {
        auto found = std::find_if(m.segs.begin(), m.segs.end(), [&](const MorphSeg &s) { return s.id == ids[i] && s.el; });
        if (found != m.segs.end()) {
            MorphSeg s = *found;
            found->el = nullptr;
            s.state = MorphSeg::Stay;
            box->AppendChild(box->RemoveChild(s.el));
            next.push_back(s);
            continue;
        }
        MorphSeg s{ ids[i], raw(k, box, "span", "morph-seg", segments[i].c_str()) };
        s.state = MorphSeg::Enter;
        next.push_back(s);
    }
    for (MorphSeg &old : m.segs) {
        if (!old.el) {
            continue;
        }
        /* Leaving segments hold their place, out of the flow. */
        old.state = MorphSeg::Leave;
        old.el->SetProperty("position", "absolute");
        style(old.el, "left", "%.2fdp", old.from_x);
        style(old.el, "top", "%.2fdp", old.from_y);
        old.el->RemoveProperty("transform");
        next.push_back(old);
    }
    m.segs = std::move(next);
    m.text = text;
    m.from_w = from_w;
    m.start = k->in.time;
    m.measure = true;
    m.box = box;
    (void)n;
}

bool morph_frame(kit *k, MorphState &m)
{
    if (m.start < 0) {
        return false;
    }
    Rml::Element *box = m.box;
    if (m.measure) {
        /* The layout pass ran: invert each kept segment to where it was. */
        m.measure = false;
        box->RemoveProperty("width");
        m.to_w = element_rect(k, box).w;
        for (MorphSeg &s : m.segs) {
            if (s.state == MorphSeg::Stay) {
                kit_rect r = offset_in(k, s.el, box);
                s.dx = s.from_x - r.x;
                s.dy = s.from_y - r.y;
            }
        }
    }
    double p = std::clamp((k->in.time - m.start) / kMorphDuration, 0.0, 1.0);
    double e = expo_out(p);
    style(box, "width", "%.2fdp", m.from_w + (m.to_w - m.from_w) * e);
    for (MorphSeg &s : m.segs) {
        switch (s.state) {
            case MorphSeg::Stay:
                style(s.el, "transform", "translate(%.2fdp, %.2fdp)", s.dx * (1 - e), s.dy * (1 - e));
                break;
            case MorphSeg::Enter:
                style(s.el, "opacity", "%.3f", std::clamp((p - 0.25) / 0.5, 0.0, 1.0));
                style(s.el, "transform", "scale(%.4f)", 0.95 + 0.05 * e);
                break;
            case MorphSeg::Leave:
                style(s.el, "opacity", "%.3f", 1 - std::clamp(p / 0.25, 0.0, 1.0));
                style(s.el, "transform", "scale(%.4f)", 1 - 0.05 * e);
                break;
        }
    }
    if (p < 1) {
        return true;
    }
    box->RemoveProperty("width");
    for (MorphSeg &s : m.segs) {
        if (s.state == MorphSeg::Leave) {
            box->RemoveChild(s.el);
            s.el = nullptr;
            continue;
        }
        s.el->RemoveProperty("transform");
        s.el->RemoveProperty("opacity");
        s.dx = s.dy = 0;
        s.state = MorphSeg::Stay;
    }
    m.segs.erase(std::remove_if(m.segs.begin(), m.segs.end(), [](const MorphSeg &s) { return !s.el; }), m.segs.end());
    m.start = -1;
    return false;
}

} // namespace

void morph_label(kit *k, Node *n, Rml::Element *box, const char *text)
{
    std::string t = text ? text : "";
    if (!n->morph) {
        n->morph = std::make_unique<MorphState>();
    }
    MorphState &m = *n->morph;
    if (m.init && m.text == t) {
        return;
    }
    morph_set(k, n, m, box, t);
    if (m.start >= 0) {
        track(k, n);
        k->layout_pass = true;
    }
}

void text_frame(kit *k, double dt)
{
    k->text_motion.erase(
        std::remove_if(
            k->text_motion.begin(),
            k->text_motion.end(),
            [&](Node *n) {
                bool moving = false;
                if (n->flow) {
                    moving |= flow_frame(k, n, dt);
                }
                if (n->morph) {
                    moving |= morph_frame(k, *n->morph);
                }
                return !moving;
            }
        ),
        k->text_motion.end()
    );
}

} // namespace kit_impl

using namespace kit_impl;

extern "C" void kit_number_flow(kit *k, const char *key, const char *cls, double value, const kit_number_flow_desc *d)
{
    const kit_class list[] = { { cls, cls != nullptr } };
    Node *n = emit(k, key, Kind::Box, "span", kit_classes(k, "number-flow", list, 1));
    if (!n->flow) {
        n->flow = std::make_unique<FlowState>();
    }
    FlowState &f = *n->flow;
    std::string text = format_flow(value, d);
    f.prefix = d->prefix ? d->prefix : "";
    f.suffix = d->suffix ? d->suffix : "";
    f.grouping = d->grouping;
    if (f.init && f.text == text) {
        return;
    }
    int trend = !f.init ? 0 : value > f.value ? 1 : value < f.value ? -1 : 0;
    flow_restructure(k, n, f, text, trend, !f.init || k->reduced_motion);
    set_attr(n, "data-value", f.prefix + text + f.suffix);
    f.init = true;
    f.value = value;
    f.text = text;
    track(k, n);
}

extern "C" void kit_morph(kit *k, const char *key, const char *cls, const char *text)
{
    const kit_class list[] = { { cls, cls != nullptr } };
    Node *n = emit(k, key, Kind::Box, "span", kit_classes(k, "morph", list, 1));
    morph_label(k, n, n->el, text);
    set_attr(n, "data-text", text ? text : "");
}
