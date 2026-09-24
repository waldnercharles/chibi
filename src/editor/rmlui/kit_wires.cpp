/* Node-editor wires (kit_wires.cpp): one element draws the canvas grid and every
 * wire as geometry, after xyflow's edges and Blender's noodles. Wire ends anchor
 * to kit paths and resolve after layout, so a wire follows its node in the same
 * frame the node moves. The shape of each wire comes from arrange_path through
 * kit_wire_shape, so the canvas draws what auto-arrange routes and scores.
 * Strokes carry a one-pixel feather because the canvas has no multisampling. */
#include "editor/rmlui/kit_internal.hpp"

#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace kit_impl {

namespace {

constexpr int kCurveSamples = 24;
constexpr float kDash = 6, kDashGap = 4;
constexpr float kFlowSpacing = 32, kFlowSpeed = 60, kFlowRadius = 2.5f;

using Point = Rml::Vector2f;

Rml::ColourbPremultiplied premultiply(uint32_t rgba, float alpha)
{
    float a = std::clamp(((rgba & 255) / 255.0f) * alpha, 0.0f, 1.0f);
    auto channel = [&](int shift) {
        return (Rml::byte)std::lround(((rgba >> shift) & 255) * a);
    };
    return { channel(24), channel(16), channel(8), (Rml::byte)std::lround(a * 255) };
}

struct WireData
{
    std::string from, to;
    Point from_point, to_point;
    std::vector<Point> via;
    uint32_t color = 0xffffffff;
    float width = 1.5f;
    unsigned flags = 0;
    float flow = 0;
};

/* Samples the cubic pieces kit_wire_shape gives a wire through its knots, in
   dp, into pixels. */
void shape(std::vector<Point> &out, kit_wire_style style, const std::vector<float> &knots, float dp)
{
    int count = (int)knots.size() / 2;
    std::vector<float> pieces(16 * 8);
    int n = kit_wire_shape(style, knots.data(), count, pieces.data(), 16);
    if (n > 16) {
        pieces.resize((size_t)n * 8);
        kit_wire_shape(style, knots.data(), count, pieces.data(), n);
    }
    if (n == 0) {
        out = { Point{ knots[0], knots[1] } * dp,
                Point{ knots[count * 2 - 2], knots[count * 2 - 1] } * dp };
        return;
    }
    out.push_back(Point{ pieces[0], pieces[1] } * dp);
    for (int i = 0; i < n; ++i) {
        const float *p = &pieces[(size_t)i * 8];
        Point p0 = { p[0], p[1] }, p1 = { p[2], p[3] }, p2 = { p[4], p[5] },
              p3 = { p[6], p[7] };
        bool straight = p1 == p0 && p2 == p3;
        int samples = straight ? 1 : kCurveSamples;
        for (int k = 1; k <= samples; ++k) {
            float t = (float)k / samples, u = 1 - t;
            out.push_back(
                (p0 * (u * u * u) + p1 * (3 * u * u * t) +
                 p2 * (3 * u * t * t) + p3 * (t * t * t)) *
                dp
            );
        }
    }
}

/* Four vertices per point: feather, core edge, core edge, feather. */
void stroke(Rml::Mesh &mesh, const std::vector<Point> &p, float width, Rml::ColourbPremultiplied color, float px)
{
    if (p.size() < 2) {
        return;
    }
    float alpha = std::min(width / px, 1.0f);
    float core = std::max(width / 2 - px / 2, 0.0f), outer = core + px;
    Rml::ColourbPremultiplied solid = { (Rml::byte)(color.red * alpha),
                                        (Rml::byte)(color.green * alpha),
                                        (Rml::byte)(color.blue * alpha),
                                        (Rml::byte)(color.alpha * alpha) };
    Rml::ColourbPremultiplied clear = { 0, 0, 0, 0 };
    int base = (int)mesh.vertices.size();
    size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
        Point d0 = i > 0 ? p[i] - p[i - 1] : p[i + 1] - p[i];
        Point d1 = i + 1 < n ? p[i + 1] - p[i] : d0;
        float l0 = std::max(std::hypot(d0.x, d0.y), 1e-5f),
              l1 = std::max(std::hypot(d1.x, d1.y), 1e-5f);
        Point n0 = { -d0.y / l0, d0.x / l0 }, n1 = { -d1.y / l1, d1.x / l1 };
        Point m = n0 + n1;
        float lm = std::hypot(m.x, m.y);
        m = lm > 1e-5f ? m / lm : n0;
        float miter = std::min(1.0f / std::max(m.x * n0.x + m.y * n0.y, 0.25f), 4.0f);
        mesh.vertices.push_back({ p[i] + m * (outer * miter), clear, {} });
        mesh.vertices.push_back({ p[i] + m * (core * miter), solid, {} });
        mesh.vertices.push_back({ p[i] - m * (core * miter), solid, {} });
        mesh.vertices.push_back({ p[i] - m * (outer * miter), clear, {} });
    }
    for (size_t i = 0; i + 1 < n; ++i) {
        int a = base + (int)i * 4, b = a + 4;
        for (int s = 0; s < 3; ++s) {
            int q[6] = { a + s, b + s, b + s + 1, a + s, b + s + 1, a + s + 1 };
            mesh.indices.insert(mesh.indices.end(), q, q + 6);
        }
    }
}

void disc(Rml::Mesh &mesh, Point c, float r, Rml::ColourbPremultiplied color, float px)
{
    /* Enough sides that the edge stays round at any size. */
    int kSides = std::clamp((int)(r * 1.5f), 12, 64);
    int base = (int)mesh.vertices.size();
    Rml::ColourbPremultiplied clear = { 0, 0, 0, 0 };
    mesh.vertices.push_back({ c, color, {} });
    for (int i = 0; i < kSides; ++i) {
        float a = 2 * (float)M_PI * i / kSides;
        Point u = { std::cos(a), std::sin(a) };
        mesh.vertices.push_back({ c + u * r, color, {} });
        mesh.vertices.push_back({ c + u * (r + px), clear, {} });
    }
    for (int i = 0; i < kSides; ++i) {
        int j = (i + 1) % kSides;
        int in_i = base + 1 + i * 2, out_i = in_i + 1, in_j = base + 1 + j * 2,
            out_j = in_j + 1;
        int q[9] = { base, in_i, in_j, in_i, out_i, out_j, in_i, out_j, in_j };
        mesh.indices.insert(mesh.indices.end(), q, q + 9);
    }
}

/* The dashes of a polyline, each as its own run of points. */
std::vector<std::vector<Point>> dashes(const std::vector<Point> &p, float dash, float gap)
{
    std::vector<std::vector<Point>> out;
    float along = 0;
    bool on = true;
    std::vector<Point> current = { p.empty() ? Point{} : p[0] };
    for (size_t i = 1; i < p.size(); ++i) {
        Point a = p[i - 1], b = p[i];
        float length = std::hypot(b.x - a.x, b.y - a.y), used = 0;
        while (length - used > 1e-4f) {
            float left = (on ? dash : gap) - along;
            float step = std::min(left, length - used);
            used += step;
            along += step;
            Point q = a + (b - a) * (used / length);
            if (on) {
                current.push_back(q);
            }
            if (along >= (on ? dash : gap) - 1e-4f) {
                if (on && current.size() > 1) {
                    out.push_back(current);
                }
                on = !on;
                along = 0;
                current = { q };
            }
        }
    }
    if (on && current.size() > 1) {
        out.push_back(current);
    }
    return out;
}

} // namespace

class ElementWires final : public Rml::Element
{
public:
    explicit ElementWires(const Rml::String &tag)
      : Rml::Element(tag)
    {
    }

    kit *owner = nullptr;
    std::vector<WireData> wires;
    kit_wire_style style = KIT_WIRE_CURVED;
    float grid = 0, grid_x = 0, grid_y = 0;
    uint32_t grid_color = 0, grid_major = 0;
    /* Sampled curves from the last render, in dp relative to the element. */
    std::vector<std::vector<Point>> drawn;

protected:
    void OnRender() override
    {
        Rml::RenderManager *render = GetRenderManager();
        if (!render || !owner) {
            return;
        }
        float dp = GetContext()->GetDensityIndependentPixelRatio();
        Point origin = GetAbsoluteOffset(Rml::BoxArea::Border);
        Rml::Mesh mesh;
        build_grid(mesh, dp);
        drawn.assign(wires.size(), {});
        for (size_t i = 0; i < wires.size(); ++i) {
            const WireData &w = wires[i];
            Point a, b;
            if (!resolve(w.from, w.from_point, origin, dp, a) ||
                !resolve(w.to, w.to_point, origin, dp, b)) {
                continue;
            }
            std::vector<float> knots = { a.x / dp, a.y / dp };
            for (Point v : w.via) {
                knots.push_back(v.x);
                knots.push_back(v.y);
            }
            knots.push_back(b.x / dp);
            knots.push_back(b.y / dp);
            std::vector<Point> &path = drawn[i];
            shape(path, style, knots, dp);
            Rml::ColourbPremultiplied color = premultiply(w.color, 1);
            if (w.flags & KIT_WIRE_DASHED) {
                for (const auto &dash : dashes(path, kDash * dp, kDashGap * dp)) {
                    stroke(mesh, dash, w.width * dp, color, 1);
                }
            } else {
                stroke(mesh, path, w.width * dp, color, 1);
            }
            if (w.flags & KIT_WIRE_FLOW) {
                flow_markers(mesh, path, w, dp);
            }
            for (Point &p : path) {
                p = p / dp;
            }
        }
        geometry_ = render->MakeGeometry(std::move(mesh));
        geometry_.Render(origin);
    }

private:
    bool resolve(const std::string &path, Point point, Point origin, float dp, Point &out) const
    {
        if (path.empty()) {
            out = point * dp;
            return true;
        }
        auto found = owner->by_path.find(path);
        if (found == owner->by_path.end() || !found->second->el) {
            return false;
        }
        Rml::Element *el = found->second->el;
        if (!el->IsVisible(true)) {
            return false;
        }
        Point size = el->GetBox().GetSize(Rml::BoxArea::Border);
        out = el->GetAbsoluteOffset(Rml::BoxArea::Border) + size / 2 - origin;
        return true;
    }

    void build_grid(Rml::Mesh &mesh, float dp)
    {
        Point size = GetBox().GetSize(Rml::BoxArea::Border);
        float step = grid * dp;
        if (step <= 0 || size.x <= 0 || size.y <= 0) {
            return;
        }
        /* Minor lines fade out before they crowd; major lines every fourth. */
        float minor = std::clamp((step - 6) / 10, 0.0f, 1.0f);
        auto lines = [&](int axis, float extent, float across, float offset) {
            float first = std::ceil(offset / grid);
            for (float g = first;; g += 1) {
                float at = (g * grid - offset) * dp;
                if (at > extent) {
                    break;
                }
                bool major = std::fmod(std::fabs(g), 4.0f) < 0.5f;
                float alpha = major ? 1 : minor;
                if (alpha <= 0) {
                    continue;
                }
                auto color = premultiply(major ? grid_major : grid_color, alpha);
                float x = std::floor(at) + 0.5f;
                std::vector<Point> line = axis == 0
                                            ? std::vector<Point>{ { x, 0 }, { x, across } }
                                            : std::vector<Point>{ { 0, x },
                                                                  { across, x } };
                stroke(mesh, line, 1, color, 1);
            }
        };
        lines(0, size.x, size.y, grid_x);
        lines(1, size.y, size.x, grid_y);
    }

    /* Markers move from source to target, spaced evenly along the wire. */
    void flow_markers(Rml::Mesh &mesh, const std::vector<Point> &path, const WireData &w, float dp)
    {
        float spacing = kFlowSpacing * dp, total = 0;
        for (size_t i = 1; i < path.size(); ++i) {
            total += std::hypot(
                path[i].x - path[i - 1].x,
                path[i].y - path[i - 1].y
            );
        }
        if (total <= 0) {
            return;
        }
        float phase = std::fmod(w.flow * kFlowSpeed * dp, spacing);
        auto color = premultiply(w.color, 1);
        float along = 0;
        size_t seg = 1;
        for (float d = phase; d < total; d += spacing) {
            while (seg < path.size()) {
                float l = std::hypot(
                    path[seg].x - path[seg - 1].x,
                    path[seg].y - path[seg - 1].y
                );
                if (along + l >= d) {
                    Point p = path[seg - 1] + (path[seg] - path[seg - 1]) *
                                                  ((d - along) / std::max(l, 1e-5f));
                    disc(mesh, p, kFlowRadius * dp, color, 1);
                    break;
                }
                along += l;
                seg++;
            }
        }
    }

    Rml::Geometry geometry_;
};

namespace {

Rml::ElementInstancerGeneric<ElementWires> wires_instancer;

} // namespace

void register_wires_element()
{
    Rml::Factory::RegisterElementInstancer("kit-wires", &wires_instancer);
}

} // namespace kit_impl

using namespace kit_impl;

extern "C" void kit_wires(kit *k, const char *key, const char *cls, const kit_wires_desc *d)
{
    const kit_class list[] = { { cls, cls != nullptr } };
    Node *n = emit(k, key, Kind::Box, "kit-wires", kit_classes(k, "wires", list, 1));
    auto *el = static_cast<ElementWires *>(n->el);
    el->owner = k;
    el->style = d->style;
    el->grid = d->grid;
    el->grid_x = d->grid_x;
    el->grid_y = d->grid_y;
    el->grid_color = d->grid_color;
    el->grid_major = d->grid_major_color;
    el->wires.resize((size_t)std::max(d->count, 0));
    for (int i = 0; i < d->count; ++i) {
        const kit_wire &in = d->wires[i];
        WireData &out = el->wires[(size_t)i];
        out.from = in.from ? in.from : "";
        out.to = in.to ? in.to : "";
        out.from_point = { in.from_x, in.from_y };
        out.to_point = { in.to_x, in.to_y };
        out.via.clear();
        for (int v = 0; v < in.via_count; ++v) {
            out.via.push_back({ in.via[v * 2], in.via[v * 2 + 1] });
        }
        out.color = in.color;
        out.width = in.width > 0 ? in.width : 1.5f;
        out.flags = in.flags;
        out.flow = in.flow;
    }
}

extern "C" int kit_wire_points(const kit *k, const char *path, int index, float *xy, int max)
{
    auto found = k->by_path.find(path);
    if (found == k->by_path.end() || found->second->tag != "kit-wires") {
        return 0;
    }
    auto *el = static_cast<ElementWires *>(found->second->el);
    if (index < 0 || index >= (int)el->drawn.size()) {
        return 0;
    }
    const auto &points = el->drawn[(size_t)index];
    int count = std::min((int)points.size(), max);
    for (int i = 0; i < count; ++i) {
        xy[i * 2] = points[(size_t)i].x;
        xy[i * 2 + 1] = points[(size_t)i].y;
    }
    return (int)points.size();
}

/* Dots (kit_dot): sockets and status marks drawn as geometry, because RmlUi's
   rounded boxes carry no antialiasing and read as squares at small sizes. */
namespace kit_impl {

namespace {

void triangle(Rml::Mesh &mesh, Point a, Point b, Point c, Rml::ColourbPremultiplied color)
{
    int base = (int)mesh.vertices.size();
    mesh.vertices.push_back({ a, color, {} });
    mesh.vertices.push_back({ b, color, {} });
    mesh.vertices.push_back({ c, color, {} });
    mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2 });
}

void square(Rml::Mesh &mesh, Point min, Point max, Rml::ColourbPremultiplied color)
{
    triangle(mesh, min, { max.x, min.y }, max, color);
    triangle(mesh, min, max, { min.x, max.y }, color);
}

} // namespace

class ElementDot final : public Rml::Element
{
public:
    explicit ElementDot(const Rml::String &tag)
      : Rml::Element(tag)
    {
    }

    kit_dot_desc desc{};
    bool dirty = true;

protected:
    void OnResize() override { dirty = true; }

    void OnRender() override
    {
        Rml::RenderManager *render = GetRenderManager();
        if (!render) {
            return;
        }
        if (dirty) {
            dirty = false;
            Rml::Mesh mesh;
            build(mesh);
            geometry_ = render->MakeGeometry(std::move(mesh));
        }
        geometry_.Render(GetAbsoluteOffset(Rml::BoxArea::Border));
    }

private:
    void build(Rml::Mesh &mesh)
    {
        float dp = GetContext()->GetDensityIndependentPixelRatio();
        Point size = GetBox().GetSize(Rml::BoxArea::Border);
        float ring = desc.ring_width * dp;
        auto fill = premultiply(desc.fill, 1), edge = premultiply(desc.ring, 1);
        if (desc.shape == KIT_DOT_CIRCLE) {
            Point c = size / 2;
            float r = std::min(size.x, size.y) / 2 - 0.5f;
            if (ring > 0) {
                disc(mesh, c, r - 0.5f, edge, 1);
            }
            disc(mesh, c, std::max(r - ring - 0.5f, 0.0f), fill, ring > 0 ? 0.75f : 1);
            return;
        }
        if (desc.shape == KIT_DOT_SQUARE) {
            /* Inset so it weighs about as much as a circle of the same box. */
            float inset = 0.5f + size.x * 0.1f;
            Point a = { inset, inset }, b = { size.x - inset, inset },
                  c = { size.x - inset, size.y - inset },
                  d = { inset, size.y - inset };
            square(mesh, a, c, ring > 0 ? edge : fill);
            if (ring > 0) {
                square(mesh, a + Point{ ring, ring }, c - Point{ ring, ring }, fill);
            }
            stroke(mesh, { a, b, c, d, a }, 1, ring > 0 ? edge : fill, 1);
            return;
        }
        /* An arrow pointing right, inset by half a pixel for its feather. */
        Point a = { 0.5f + size.x * 0.1f, 0.5f }, b = { size.x - 0.5f, size.y / 2 },
              c = { 0.5f + size.x * 0.1f, size.y - 0.5f };
        triangle(mesh, a, b, c, ring > 0 ? edge : fill);
        if (ring > 0) {
            Point center = (a + b + c) / 3;
            float k = std::max(0.0f, 1 - 2.2f * ring / size.y);
            triangle(
                mesh,
                center + (a - center) * k,
                center + (b - center) * k,
                center + (c - center) * k,
                fill
            );
        }
        stroke(mesh, { a, b, c, a }, 1, ring > 0 ? edge : fill, 1);
    }

    Rml::Geometry geometry_;
};

namespace {

Rml::ElementInstancerGeneric<ElementDot> dot_instancer;

} // namespace

void register_dot_element()
{
    Rml::Factory::RegisterElementInstancer("kit-dot", &dot_instancer);
}

} // namespace kit_impl

extern "C" void kit_dot(kit *k, const char *key, const char *cls, const kit_dot_desc *d)
{
    Node *n = emit(k, key, Kind::Box, "kit-dot", cls);
    auto *el = static_cast<ElementDot *>(n->el);
    if (std::memcmp(&el->desc, d, sizeof *d) != 0) {
        el->desc = *d;
        el->dirty = true;
    }
}
