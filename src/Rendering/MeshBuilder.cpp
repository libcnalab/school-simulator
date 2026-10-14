#include "Rendering/MeshBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "Microsoft/Xna/Framework/MathHelper.hpp"

using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;

namespace {

Vector3 SafeNormalize(const Vector3& v)
{
    const float len = v.Length();
    return len > 1e-8f ? v * (1.0f / len) : Vector3::Up;
}

Color ToColor(const Vector3& c, float alpha)
{
    const auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return Color(b(c.X), b(c.Y), b(c.Z), b(alpha));
}

}  // namespace

int MeshBuilder::AddVertex(const Vertex& v)
{
    if (vertices_.size() >= 65535) throw std::runtime_error("MeshBuilder: more than 65535 vertices in one mesh");
    vertices_.push_back(v);
    vertices_.back().alpha = alpha_;
    return static_cast<int>(vertices_.size()) - 1;
}

void MeshBuilder::AddTriangle(int a, int b, int c)
{
    indices_.push_back(static_cast<std::uint16_t>(a));
    indices_.push_back(static_cast<std::uint16_t>(b));
    indices_.push_back(static_cast<std::uint16_t>(c));
}

void MeshBuilder::AddPatch(const Vector3& origin, const Vector3& U, const Vector3& V, float texMeters,
                           const Vector3& tint, float maxCell, float emissive)
{
    const float lu = U.Length();
    const float lv = V.Length();
    if (lu < 1e-5f || lv < 1e-5f) return;
    const int nu = std::max(1, static_cast<int>(std::ceil(lu / maxCell)));
    const int nv = std::max(1, static_cast<int>(std::ceil(lv / maxCell)));
    const Vector3 normal = SafeNormalize(Vector3::Cross(U, V));
    const float uRepeat = texMeters > 0.0f ? lu / texMeters : 1.0f;
    const float vRepeat = texMeters > 0.0f ? lv / texMeters : 1.0f;

    const int base = static_cast<int>(vertices_.size());
    for (int j = 0; j <= nv; ++j) {
        for (int i = 0; i <= nu; ++i) {
            const float fu = static_cast<float>(i) / static_cast<float>(nu);
            const float fv = static_cast<float>(j) / static_cast<float>(nv);
            Vertex vtx;
            vtx.position = origin + U * fu + V * fv;
            vtx.normal = normal;
            // V grows upward in world space but texture V grows downward, so flip it.
            vtx.uv = Vector2(fu * uRepeat, (1.0f - fv) * vRepeat);
            vtx.albedo = tint;
            vtx.emissive = emissive;
            AddVertex(vtx);
        }
    }
    const int row = nu + 1;
    for (int j = 0; j < nv; ++j) {
        for (int i = 0; i < nu; ++i) {
            const int a = base + j * row + i;
            const int b = a + 1;
            const int c = a + row;
            const int d = c + 1;
            AddTriangle(a, b, d);
            AddTriangle(a, d, c);
        }
    }
}

void MeshBuilder::AddBox(const Vector3& min, const Vector3& max, float texMeters, const Vector3& tint,
                         BoxFaces faces, float maxCell, float emissive)
{
    const float dx = max.X - min.X;
    const float dy = max.Y - min.Y;
    const float dz = max.Z - min.Z;
    if (Has(faces, BoxFaces::PosX))
        AddPatch(Vector3(max.X, min.Y, max.Z), Vector3(0, 0, -dz), Vector3(0, dy, 0), texMeters, tint, maxCell, emissive);
    if (Has(faces, BoxFaces::NegX))
        AddPatch(Vector3(min.X, min.Y, min.Z), Vector3(0, 0, dz), Vector3(0, dy, 0), texMeters, tint, maxCell, emissive);
    if (Has(faces, BoxFaces::PosZ))
        AddPatch(Vector3(min.X, min.Y, max.Z), Vector3(dx, 0, 0), Vector3(0, dy, 0), texMeters, tint, maxCell, emissive);
    if (Has(faces, BoxFaces::NegZ))
        AddPatch(Vector3(max.X, min.Y, min.Z), Vector3(-dx, 0, 0), Vector3(0, dy, 0), texMeters, tint, maxCell, emissive);
    if (Has(faces, BoxFaces::PosY))
        AddPatch(Vector3(min.X, max.Y, max.Z), Vector3(dx, 0, 0), Vector3(0, 0, -dz), texMeters, tint, maxCell, emissive);
    if (Has(faces, BoxFaces::NegY))
        AddPatch(Vector3(min.X, min.Y, min.Z), Vector3(dx, 0, 0), Vector3(0, 0, dz), texMeters, tint, maxCell, emissive);
}

void MeshBuilder::AddBoxTransformed(const Matrix& world, const Vector3& min, const Vector3& max, float texMeters, const Vector3& tint,
                                    BoxFaces faces, float maxCell, float emissive)
{
    const float dx = max.X - min.X;
    const float dy = max.Y - min.Y;
    const float dz = max.Z - min.Z;
    const auto patch = [&](const Vector3& origin, const Vector3& U, const Vector3& V) {
        AddPatch(Vector3::Transform(origin, world), Vector3::TransformNormal(U, world), Vector3::TransformNormal(V, world), texMeters, tint, maxCell, emissive);
    };
    if (Has(faces, BoxFaces::PosX)) patch(Vector3(max.X, min.Y, max.Z), Vector3(0, 0, -dz), Vector3(0, dy, 0));
    if (Has(faces, BoxFaces::NegX)) patch(Vector3(min.X, min.Y, min.Z), Vector3(0, 0, dz), Vector3(0, dy, 0));
    if (Has(faces, BoxFaces::PosZ)) patch(Vector3(min.X, min.Y, max.Z), Vector3(dx, 0, 0), Vector3(0, dy, 0));
    if (Has(faces, BoxFaces::NegZ)) patch(Vector3(max.X, min.Y, min.Z), Vector3(-dx, 0, 0), Vector3(0, dy, 0));
    if (Has(faces, BoxFaces::PosY)) patch(Vector3(min.X, max.Y, max.Z), Vector3(dx, 0, 0), Vector3(0, 0, -dz));
    if (Has(faces, BoxFaces::NegY)) patch(Vector3(min.X, min.Y, min.Z), Vector3(dx, 0, 0), Vector3(0, 0, dz));
}

void MeshBuilder::Append(const MeshBuilder& other)
{
    const int base = static_cast<int>(vertices_.size());
    for (const Vertex& v : other.vertices_) AddVertex(v);
    for (const std::uint16_t i : other.indices_) indices_.push_back(static_cast<std::uint16_t>(base + i));
}

void MeshBuilder::AddCylinder(const Vector3& baseCentre, float radius, float height, int segments,
                              float texMeters, const Vector3& tint, bool caps, float emissive)
{
    const int base = static_cast<int>(vertices_.size());
    const float circumference = MathHelper::TwoPi * radius;
    const float uRepeat = texMeters > 0.0f ? circumference / texMeters : 1.0f;
    const float vRepeat = texMeters > 0.0f ? height / texMeters : 1.0f;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float a = t * MathHelper::TwoPi;
        const Vector3 n(std::cos(a), 0.0f, std::sin(a));
        for (int k = 0; k < 2; ++k) {
            Vertex v;
            v.position = baseCentre + n * radius + Vector3(0, height * static_cast<float>(k), 0);
            v.normal = n;
            v.uv = Vector2(t * uRepeat, (1.0f - static_cast<float>(k)) * vRepeat);
            v.albedo = tint;
            v.emissive = emissive;
            AddVertex(v);
        }
    }
    for (int i = 0; i < segments; ++i) {
        const int b0 = base + i * 2;       // bottom at angle i
        const int t0 = b0 + 1;
        const int b1 = b0 + 2;             // bottom at angle i+1
        const int t1 = b1 + 1;
        // Angle grows counter-clockwise seen from +Y with x=cos, z=sin, which is clockwise seen
        // from outside along the radius, so the outward-facing winding is b0, t0, t1.
        AddTriangle(b0, t0, t1);
        AddTriangle(b0, t1, b1);
    }
    if (caps) {
        const Vector3 topCentre = baseCentre + Vector3(0, height, 0);
        const int c = AddVertex({topCentre, Vector3::Up, Vector2(0.5f, 0.5f), tint, emissive});
        const int firstTop = static_cast<int>(vertices_.size());
        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / static_cast<float>(segments) * MathHelper::TwoPi;
            AddVertex({topCentre + Vector3(std::cos(a), 0, std::sin(a)) * radius, Vector3::Up,
                       Vector2(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)), tint, emissive});
        }
        for (int i = 0; i < segments; ++i) AddTriangle(c, firstTop + i + 1, firstTop + i);
    }
}

void MeshBuilder::AddCone(const Vector3& baseCentre, float baseRadius, float topRadius, float height,
                          int segments, const Vector3& tint, float emissive)
{
    const int base = static_cast<int>(vertices_.size());
    const float slope = (baseRadius - topRadius) / std::max(height, 1e-5f);
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float a = t * MathHelper::TwoPi;
        const Vector3 radial(std::cos(a), 0.0f, std::sin(a));
        const Vector3 n = SafeNormalize(radial + Vector3(0, slope, 0));
        AddVertex({baseCentre + radial * baseRadius, n, Vector2(t, 1.0f), tint, emissive});
        AddVertex({baseCentre + radial * topRadius + Vector3(0, height, 0), n, Vector2(t, 0.0f), tint, emissive});
    }
    for (int i = 0; i < segments; ++i) {
        const int b0 = base + i * 2, t0 = b0 + 1, b1 = b0 + 2, t1 = b1 + 1;
        AddTriangle(b0, t0, t1);
        AddTriangle(b0, t1, b1);
    }
}

void MeshBuilder::AddSphere(const Vector3& centre, float radius, int slices, int stacks, const Vector3& tint,
                            bool inside, float emissive)
{
    const int base = static_cast<int>(vertices_.size());
    for (int j = 0; j <= stacks; ++j) {
        const float v = static_cast<float>(j) / static_cast<float>(stacks);
        const float phi = v * MathHelper::Pi;
        for (int i = 0; i <= slices; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(slices);
            const float theta = u * MathHelper::TwoPi;
            const Vector3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            AddVertex({centre + n * radius, inside ? -n : n, Vector2(u, v), tint, emissive});
        }
    }
    const int row = slices + 1;
    for (int j = 0; j < stacks; ++j) {
        for (int i = 0; i < slices; ++i) {
            const int a = base + j * row + i, b = a + 1, c = a + row, d = c + 1;
            // u runs counter-clockwise seen from +Y, v runs top to bottom.
            if (!inside) {
                AddTriangle(a, d, c);
                AddTriangle(a, b, d);
            } else {
                AddTriangle(a, c, d);
                AddTriangle(a, d, b);
            }
        }
    }
}

BoundingBox MeshBuilder::Bounds() const
{
    if (vertices_.empty()) return BoundingBox(Vector3::Zero, Vector3::Zero);
    Vector3 lo = vertices_.front().position;
    Vector3 hi = lo;
    for (const Vertex& v : vertices_) {
        lo = Vector3::Min(lo, v.position);
        hi = Vector3::Max(hi, v.position);
    }
    return BoundingBox(lo, hi);
}

std::vector<VertexPositionColorTexture> MeshBuilder::Bake(const std::vector<BakedLight>& lights,
                                                          const Vector3& ambient) const
{
    std::vector<VertexPositionColorTexture> out;
    out.reserve(vertices_.size());
    for (const Vertex& v : vertices_) {
        Vector3 lit = ambient;
        for (const BakedLight& light : lights) {
            Vector3 toLight = light.position - v.position;
            const float dist = toLight.Length();
            if (dist >= light.radius || dist < 1e-4f) continue;
            toLight = toLight * (1.0f / dist);
            // Half-Lambert wrap keeps walls facing away from a ceiling light dim rather than black.
            const float ndl = Vector3::Dot(v.normal, toLight) * 0.5f + 0.5f;
            const float falloff = 1.0f - dist / light.radius;
            lit += light.color * (light.intensity * ndl * ndl * falloff * falloff);
        }
        const float k = 1.0f - v.emissive;
        const Vector3 shade = Vector3(v.albedo.X * (lit.X * k + v.emissive), v.albedo.Y * (lit.Y * k + v.emissive),
                                      v.albedo.Z * (lit.Z * k + v.emissive));
        out.emplace_back(v.position, ToColor(shade, v.alpha), v.uv);
    }
    return out;
}
