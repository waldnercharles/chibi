/* Live line charts after Liveline, with Recharts' axes: one custom element draws
 * every series as geometry (no element per point). The value range follows the
 * visible data with a 12% margin and eases toward it frame-rate independently
 * (factor 1 - (1 - 0.15)^(dt / 16.67 ms)); curves are monotone cubic
 * (Fritsch-Carlson), so they never overshoot a sample. Five "nice" ticks label the
 * grid, a badge carries the newest value, the newest point pulses every 1.5 s,
 * and hovering scrubs a crosshair with the value under it. */
#include "editor/rmlui/kit_internal.hpp"

#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kit_impl {

namespace {

constexpr float kMargin = 0.12f;
constexpr double kRangeSpeed = 0.15;
constexpr int kTicks = 5;

Rml::ColourbPremultiplied premultiply(uint32_t rgba, float alpha)
{
    float a = ((rgba & 255) / 255.0f) * alpha;
    auto channel = [&](int shift) { return (Rml::byte)std::lround(((rgba >> shift) & 255) * a); };
    return { channel(24), channel(16), channel(8), (Rml::byte)std::lround(a * 255) };
}

uint32_t pack(Rml::Colourb c)
{
    return ((uint32_t)c.red << 24) | ((uint32_t)c.green << 16) | ((uint32_t)c.blue << 8) | c.alpha;
}

/* d3's nice step: 1, 2 or 5 times a power of ten. */
double nice_step(double span, int ticks)
{
    double raw = span / std::max(ticks - 1, 1);
    double power = std::pow(10, std::floor(std::log10(raw)));
    double f = raw / power;
    return (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * power;
}

void quad(Rml::Mesh &mesh, Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f c, Rml::Vector2f d, Rml::ColourbPremultiplied ca, Rml::ColourbPremultiplied cb)
{
    int base = (int)mesh.vertices.size();
    mesh.vertices.push_back({ a, ca, {} });
    mesh.vertices.push_back({ b, ca, {} });
    mesh.vertices.push_back({ c, cb, {} });
    mesh.vertices.push_back({ d, cb, {} });
    for (int i : { 0, 1, 2, 0, 2, 3 }) {
        mesh.indices.push_back(base + i);
    }
}

void segment(Rml::Mesh &mesh, Rml::Vector2f p, Rml::Vector2f q, float width, Rml::ColourbPremultiplied color)
{
    Rml::Vector2f d = q - p;
    float length = std::sqrt(d.x * d.x + d.y * d.y);
    if (length < 1e-4f) {
        return;
    }
    Rml::Vector2f n = { -d.y / length * width / 2, d.x / length * width / 2 };
    /* Extend by half the width so consecutive segments overlap at joins. */
    Rml::Vector2f e = { d.x / length * width / 2, d.y / length * width / 2 };
    quad(mesh, p - e + n, q + e + n, q + e - n, p - e - n, color, color);
}

void disc(Rml::Mesh &mesh, Rml::Vector2f c, float inner, float outer, Rml::ColourbPremultiplied color)
{
    constexpr int kSides = 24;
    for (int i = 0; i < kSides; ++i) {
        float a0 = 2 * (float)M_PI * i / kSides, a1 = 2 * (float)M_PI * (i + 1) / kSides;
        Rml::Vector2f u0 = { std::cos(a0), std::sin(a0) }, u1 = { std::cos(a1), std::sin(a1) };
        quad(mesh, c + u0 * inner, c + u1 * inner, c + u1 * outer, c + u0 * outer, color, color);
    }
}

} // namespace

/* Monotone cubic tangents (Fritsch-Carlson) for x-ascending points. */
static void monotone_tangents(const std::vector<Rml::Vector2f> &p, std::vector<float> &m)
{
    size_t n = p.size();
    m.assign(n, 0);
    if (n < 2) {
        return;
    }
    std::vector<float> slope(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) {
        float dx = p[i + 1].x - p[i].x;
        slope[i] = dx > 0 ? (p[i + 1].y - p[i].y) / dx : 0;
    }
    m[0] = slope[0];
    m[n - 1] = slope[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) {
        m[i] = slope[i - 1] * slope[i] <= 0 ? 0 : (slope[i - 1] + slope[i]) / 2;
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        if (slope[i] == 0) {
            m[i] = m[i + 1] = 0;
            continue;
        }
        float a = m[i] / slope[i], b = m[i + 1] / slope[i];
        float h = a * a + b * b;
        if (h > 9) {
            float t = 3 / std::sqrt(h);
            m[i] = t * a * slope[i];
            m[i + 1] = t * b * slope[i];
        }
    }
}

class ElementChart final : public Rml::Element
{
public:
    explicit ElementChart(const Rml::String &tag)
      : Rml::Element(tag)
    {
    }

    ChartFrame frame;

    void Invalidate()
    {
        dirty_ = true;
    }

protected:
    void OnResize() override
    {
        dirty_ = true;
    }

    void OnRender() override
    {
        Rml::RenderManager *render = GetRenderManager();
        if (!render) {
            return;
        }
        if (dirty_) {
            dirty_ = false;
            Rml::Mesh mesh;
            build(mesh);
            geometry_ = render->MakeGeometry(std::move(mesh));
        }
        geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
    }

private:
    void build(Rml::Mesh &mesh)
    {
        const ChartFrame &f = frame;
        Rml::Vector2f size = GetBox().GetSize(Rml::BoxArea::Content);
        if (size.x <= 1 || size.y <= 1 || f.hi <= f.lo) {
            return;
        }
        float s = f.dp;
        const Rml::ComputedValues &style = GetComputedValues();
        uint32_t line = pack(style.color());
        uint32_t grid = pack(style.border_top_color());
        auto y_of = [&](double v) { return (float)(size.y - (v - f.lo) / (f.hi - f.lo) * size.y); };
        auto x_of = [&](double t) { return (float)((t - (f.now - f.window)) / f.window * size.x); };
        for (double tick : f.ticks) {
            float y = std::round(y_of(tick)) + 0.5f;
            quad(mesh, { 0, y - s * 0.5f }, { size.x, y - s * 0.5f }, { size.x, y + s * 0.5f }, { 0, y + s * 0.5f }, premultiply(grid, 1), premultiply(grid, 1));
        }
        if (f.reference != 0 && f.reference > f.lo && f.reference < f.hi) {
            float y = y_of(f.reference);
            for (float x = 0; x < size.x; x += 6 * s) {
                segment(mesh, { x, y }, { std::min(x + 3 * s, size.x), y }, s, premultiply(line, 0.45f));
            }
        }
        for (size_t si = 0; si < f.series.size(); ++si) {
            const ChartSeries &series = f.series[si];
            uint32_t color = series.color ? series.color : line;
            /* At most two points per pixel column: keep each column's peak. */
            std::vector<Rml::Vector2f> points;
            float column = -1;
            for (size_t i = 0; i < series.times.size(); ++i) {
                float x = x_of(series.times[i]);
                if (x < -2 * s) {
                    continue;
                }
                float y = y_of(series.values[i]);
                if (!points.empty() && x - column < 0.5f * s) {
                    points.back().y = std::min(points.back().y, y);
                    continue;
                }
                column = x;
                points.push_back({ x, y });
            }
            if (points.size() < 2) {
                continue;
            }
            std::vector<float> m;
            monotone_tangents(points, m);
            std::vector<Rml::Vector2f> curve;
            for (size_t i = 0; i + 1 < points.size(); ++i) {
                Rml::Vector2f a = points[i], b = points[i + 1];
                float dx = b.x - a.x;
                int steps = std::clamp((int)(dx / (3 * s)), 1, 8);
                for (int k = 0; k < steps; ++k) {
                    float t = (float)k / steps, t2 = t * t, t3 = t2 * t;
                    float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
                    curve.push_back({ a.x + dx * t, h00 * a.y + h10 * dx * m[i] + h01 * b.y + h11 * dx * m[i + 1] });
                }
            }
            curve.push_back(points.back());
            if (f.fill && si == 0) {
                for (size_t i = 0; i + 1 < curve.size(); ++i) {
                    quad(mesh, curve[i], curve[i + 1], { curve[i + 1].x, size.y }, { curve[i].x, size.y }, premultiply(color, 0.22f), premultiply(color, 0));
                }
            }
            for (size_t i = 0; i + 1 < curve.size(); ++i) {
                segment(mesh, curve[i], curve[i + 1], 1.5f * s, premultiply(color, 1));
            }
            if (f.live && si == 0) {
                Rml::Vector2f end = points.back();
                double phase = std::fmod(f.now, 1.5);
                if (phase < 0.9 && !f.reduced_motion) {
                    double t = phase / 0.9;
                    float radius = (float)(9 + 12 * t) * s * 0.5f;
                    disc(mesh, end, radius - 1.2f * s, radius, premultiply(color, (float)(0.35 * (1 - t))));
                }
                disc(mesh, end, 0, 3.25f * s, premultiply(0xffffffff, 1));
                disc(mesh, end, 0, 1.75f * s, premultiply(color, 1));
            }
        }
        if (f.hover_x >= 0) {
            float x = f.hover_x * s;
            quad(mesh, { x - s * 0.5f, 0 }, { x + s * 0.5f, 0 }, { x + s * 0.5f, size.y }, { x - s * 0.5f, size.y }, premultiply(line, 0.5f), premultiply(line, 0.5f));
            if (f.hover_value == f.hover_value) {
                disc(mesh, { x, y_of(f.hover_value) }, 0, 3 * s, premultiply(line, 1));
            }
        }
    }

    Rml::Geometry geometry_;
    bool dirty_ = true;
};

Rml::ElementInstancerGeneric<ElementChart> chart_instancer;

/* Registered per kit: RmlUi's factory is rebuilt whenever RmlUi reinitializes. */
void register_chart_element()
{
    Rml::Factory::RegisterElementInstancer("kit-chart", &chart_instancer);
}

namespace {

std::string format_value(double v, int precision, const char *unit)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f%s%s", precision, v, unit && unit[0] ? " " : "", unit ? unit : "");
    return buffer;
}

/* Value of the first series at time t, linearly between samples. */
double value_at(const ChartSeries &s, double t)
{
    auto after = std::lower_bound(s.times.begin(), s.times.end(), t);
    if (after == s.times.begin() || after == s.times.end()) {
        return NAN;
    }
    size_t i = (size_t)(after - s.times.begin());
    double t0 = s.times[i - 1], t1 = s.times[i];
    double u = t1 > t0 ? (t - t0) / (t1 - t0) : 0;
    return s.values[i - 1] + (s.values[i] - s.values[i - 1]) * u;
}

} // namespace

} // namespace kit_impl

using namespace kit_impl;

extern "C" float kit_chart(kit *k, const char *key, const char *cls, const kit_chart_desc *d)
{
    const kit_class list[] = { { cls, cls != nullptr } };
    Node *n = emit(k, key, Kind::Box, "kit-chart", kit_classes(k, "chart", list, 1));
    auto *chart = static_cast<ElementChart *>(n->el);
    if (!n->chart) {
        n->chart = std::make_unique<ChartState>();
        n->chart->badge = raw(k, n->el, "span", "chart-badge");
        n->chart->hover = raw(k, n->el, "span", "chart-hover");
    }
    ChartState &c = *n->chart;
    ChartFrame &f = chart->frame;
    double dt = std::clamp(k->in.time - c.time, 0.0, 0.05);
    c.time = k->in.time;
    f.now = d->now;
    f.window = d->window > 0 ? d->window : 30;
    f.dp = dp(k);
    f.fill = d->fill;
    f.live = d->live;
    f.reference = d->reference;
    f.reduced_motion = k->reduced_motion;
    f.series.resize((size_t)d->series_count);
    double lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i < d->series_count; ++i) {
        const kit_chart_series &in = d->series[i];
        ChartSeries &out = f.series[(size_t)i];
        out.color = in.color;
        out.times.assign(in.times, in.times + in.count);
        out.values.assign(in.values, in.values + in.count);
        for (int j = 0; j < in.count; ++j) {
            if (in.times[j] >= f.now - f.window) {
                lo = std::min(lo, (double)in.values[j]);
                hi = std::max(hi, (double)in.values[j]);
            }
        }
    }
    if (d->min != 0 || d->max != 0) {
        lo = d->min;
        hi = d->max;
    } else if (lo <= hi) {
        if (d->reference != 0) {
            lo = std::min(lo, (double)d->reference);
            hi = std::max(hi, (double)d->reference);
        }
        double span = std::max(hi - lo, std::max(std::fabs(hi) * 0.1, 0.4));
        double mid = (hi + lo) / 2;
        lo = std::max(0.0, mid - span / 2 - span * kMargin);
        hi = mid + span / 2 + span * kMargin;
    } else {
        lo = 0;
        hi = 1;
    }
    /* Ease the shown range toward the data, faster when the gap is small. */
    if (!c.ranged || k->reduced_motion) {
        c.lo = lo;
        c.hi = hi;
        c.ranged = true;
    } else {
        double gap = std::fabs(hi - c.hi) + std::fabs(lo - c.lo);
        double speed = kRangeSpeed + (1 - std::min(1.0, gap / std::max(hi - lo, 1e-6))) * 0.2;
        double factor = 1 - std::pow(1 - speed, dt / (1.0 / 60));
        c.lo += (lo - c.lo) * factor;
        c.hi += (hi - c.hi) * factor;
    }
    f.lo = c.lo;
    f.hi = c.hi;
    f.ticks.clear();
    double step = nice_step(f.hi - f.lo, kTicks);
    for (double t = std::ceil(f.lo / step) * step; t <= f.hi; t += step) {
        f.ticks.push_back(t);
    }
    kit_rect r = element_rect(k, n->el);
    /* Tick labels: reuse spans top to bottom. */
    while (c.labels.size() < f.ticks.size()) {
        c.labels.push_back(raw(k, n->el, "span", "chart-tick"));
    }
    char buffer[48];
    for (size_t i = 0; i < c.labels.size(); ++i) {
        bool shown = i < f.ticks.size() && r.h > 0;
        c.labels[i]->SetProperty("display", shown ? "block" : "none");
        if (!shown) {
            continue;
        }
        set_element_text(c.labels[i], format_value(f.ticks[i], step < 1 ? 1 : 0, nullptr));
        std::snprintf(buffer, sizeof buffer, "%.1fdp", r.h - (f.ticks[i] - f.lo) / (f.hi - f.lo) * r.h - 7);
        c.labels[i]->SetProperty("top", buffer);
    }
    /* Badge: the newest value of the first series, at its height. */
    const ChartSeries *first = f.series.empty() || f.series[0].values.empty() ? nullptr : &f.series[0];
    c.badge->SetProperty("display", first && r.h > 0 ? "block" : "none");
    if (first) {
        double v = first->values.back();
        set_element_text(c.badge, format_value(v, d->precision, d->unit));
        std::snprintf(buffer, sizeof buffer, "%.1fdp", std::clamp((float)(r.h - (v - f.lo) / (f.hi - f.lo) * r.h) - 9, 0.0f, std::max(0.0f, r.h - 18)));
        c.badge->SetProperty("top", buffer);
    }
    /* Scrub: a crosshair and the value under the pointer. */
    bool hovered = n->el->IsPseudoClassSet("hover") && rect_contains(r, k->in.mouse_x, k->in.mouse_y);
    f.hover_x = -1;
    f.hover_value = NAN;
    float hover_time = -1;
    if (hovered && first) {
        f.hover_x = k->in.mouse_x - r.x;
        hover_time = (float)(f.now - f.window + f.hover_x / std::max(r.w, 1.0f) * f.window);
        f.hover_value = value_at(*first, hover_time);
    }
    bool show_hover = hovered && f.hover_value == f.hover_value;
    c.hover->SetProperty("display", show_hover ? "block" : "none");
    if (show_hover) {
        set_element_text(c.hover, format_value(f.hover_value, d->precision, d->unit));
        std::snprintf(buffer, sizeof buffer, "%.1fdp", std::clamp(f.hover_x + 8, 0.0f, std::max(0.0f, r.w - 64)));
        c.hover->SetProperty("left", buffer);
    }
    chart->Invalidate();
    return hover_time;
}
