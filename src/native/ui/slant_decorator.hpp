// A `slant` decorator for RmlUi: a quadrilateral panel whose top or bottom
// corners are inset horizontally, so pages can draw the trapezoids and
// parallelograms of the pre-battle design without clip-path.
//
//   decorator: slant(<fill> <edge> <edge-width> <top-bar> <tl> <tr> <bl> <br>);
//
// <fill> and <edge> are colours; <edge-width> draws an outline all round and
// <top-bar> a thicker bar along the top edge only (either may be 0). The four
// lengths inset the top-left, top-right, bottom-left and bottom-right corners
// along the x axis. Edges are feathered by one pixel; the element itself should
// have no background or border. Geometry only: no textures or shaders.
#pragma once
#include <RmlUi/Core.h>
#include <array>
#include <cmath>
#include <memory>

namespace srw64::ui {

class SlantDecorator final : public Rml::Decorator {
public:
    Rml::Colourb fill, edge;
    Rml::NumericValue width, top, tl, tr, bl, br;

    Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea) const override {
        using V = Rml::Vector2f;
        const V size = element->GetBox().GetSize(Rml::BoxArea::Border);
        const float opacity = element->GetComputedValues().opacity();
        const float w = std::max(0.f, element->ResolveLength(width)), t = std::max(0.f, element->ResolveLength(top));
        const auto fill_c = fill.ToPremultiplied(opacity), edge_c = edge.ToPremultiplied(opacity);
        const Rml::ColourbPremultiplied clear{0, 0, 0, 0};
        const std::array<V, 4> outer{V(element->ResolveLength(tl), 0), V(size.x - element->ResolveLength(tr), 0),
            V(size.x - element->ResolveLength(br), size.y), V(element->ResolveLength(bl), size.y)};
        // Edge i runs from outer[i] to outer[i+1]: top, right, bottom, left (clockwise on screen).
        const auto offset = [&](std::array<float, 4> d) {
            std::array<V, 4> line_p, line_u;
            for (int i = 0; i < 4; ++i) {
                V u = outer[(i + 1) % 4] - outer[i];
                const float len = std::sqrt(u.x * u.x + u.y * u.y);
                u = len > 0 ? u / len : V(1, 0);
                line_u[i] = u;
                line_p[i] = outer[i] + V(-u.y, u.x) * d[i];  // inward normal in y-down coordinates
            }
            std::array<V, 4> out;
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 3) % 4;  // the edge ending at corner i
                const V p = line_p[j], u = line_u[j], q = line_p[i], v = line_u[i];
                const float den = u.x * v.y - u.y * v.x;
                out[i] = std::abs(den) < 1e-6f ? outer[i] : p + u * (((q.x - p.x) * v.y - (q.y - p.y) * v.x) / den);
            }
            return out;
        };
        Rml::Mesh mesh;
        const auto quad = [&](V a, V b, V c, V d, Rml::ColourbPremultiplied ca, Rml::ColourbPremultiplied cb,
                              Rml::ColourbPremultiplied cc, Rml::ColourbPremultiplied cd) {
            const int base = int(mesh.vertices.size());
            for (const auto& [p, col] : {std::pair{a, ca}, std::pair{b, cb}, std::pair{c, cc}, std::pair{d, cd}})
                mesh.vertices.push_back(Rml::Vertex{p, col, V(0, 0)});
            for (int i : {0, 1, 2, 0, 2, 3}) mesh.indices.push_back(base + i);
        };
        const auto inner = offset({w, w, w, w});
        quad(inner[0], inner[1], inner[2], inner[3], fill_c, fill_c, fill_c, fill_c);
        if (w > 0)
            for (int i = 0; i < 4; ++i) quad(outer[i], outer[(i + 1) % 4], inner[(i + 1) % 4], inner[i], edge_c, edge_c, edge_c, edge_c);
        if (t > 0) {
            const auto bar = offset({t, 0, 0, 0});
            quad(outer[0], outer[1], bar[1], bar[0], edge_c, edge_c, edge_c, edge_c);
        }
        // One-pixel feather outside every edge, in that edge's colour.
        const auto feather = offset({-1.f, -1.f, -1.f, -1.f});
        for (int i = 0; i < 4; ++i) {
            const auto c = w > 0 || (i == 0 && t > 0) ? edge_c : fill_c;
            quad(feather[i], feather[(i + 1) % 4], outer[(i + 1) % 4], outer[i], clear, clear, c, c);
        }
        return reinterpret_cast<Rml::DecoratorDataHandle>(new Rml::Geometry(element->GetRenderManager()->MakeGeometry(std::move(mesh))));
    }
    void ReleaseElementData(Rml::DecoratorDataHandle data) const override { delete reinterpret_cast<Rml::Geometry*>(data); }
    void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle data) const override {
        reinterpret_cast<Rml::Geometry*>(data)->Render(element->GetAbsoluteOffset(Rml::BoxArea::Border));
    }
};

// Construct after Rml::Initialise (the parsers live in the style sheet
// specification) and keep alive until after Rml::Shutdown.
class SlantInstancer final : public Rml::DecoratorInstancer {
    Rml::PropertyId fill, edge, width, top, tl, tr, bl, br;
public:
    SlantInstancer() {
        fill = RegisterProperty("fill", "#00000000").AddParser("color").GetId();
        edge = RegisterProperty("edge", "#00000000").AddParser("color").GetId();
        width = RegisterProperty("width", "0px").AddParser("length").GetId();
        top = RegisterProperty("top", "0px").AddParser("length").GetId();
        tl = RegisterProperty("top-left", "0px").AddParser("length").GetId();
        tr = RegisterProperty("top-right", "0px").AddParser("length").GetId();
        bl = RegisterProperty("bottom-left", "0px").AddParser("length").GetId();
        br = RegisterProperty("bottom-right", "0px").AddParser("length").GetId();
        RegisterShorthand("decorator", "fill, edge, width, top, top-left, top-right, bottom-left, bottom-right", Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary& p, const Rml::DecoratorInstancerInterface&) override {
        auto d = Rml::MakeShared<SlantDecorator>();
        d->fill = p.GetProperty(fill)->Get<Rml::Colourb>();
        d->edge = p.GetProperty(edge)->Get<Rml::Colourb>();
        d->width = p.GetProperty(width)->GetNumericValue();
        d->top = p.GetProperty(top)->GetNumericValue();
        d->tl = p.GetProperty(tl)->GetNumericValue();
        d->tr = p.GetProperty(tr)->GetNumericValue();
        d->bl = p.GetProperty(bl)->GetNumericValue();
        d->br = p.GetProperty(br)->GetNumericValue();
        return d;
    }
};

}  // namespace srw64::ui
