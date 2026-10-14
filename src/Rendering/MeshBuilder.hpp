#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

// A point light used only to bake per-vertex lighting into the school's static
// architecture. Dynamic exhibits are lit at draw time by BasicEffect instead.
struct BakedLight {
    Microsoft::Xna::Framework::Vector3 position;
    Microsoft::Xna::Framework::Vector3 color{1.0f, 0.95f, 0.85f};
    float intensity = 1.0f;
    float radius = 9.0f;
};

enum class BoxFaces : unsigned {
    PosX = 1, NegX = 2, PosY = 4, NegY = 8, PosZ = 16, NegZ = 32,
    All = 63, Sides = 1 | 2 | 16 | 32, NoBottom = 63 & ~8, NoTop = 63 & ~4,
};
constexpr BoxFaces operator|(BoxFaces a, BoxFaces b)
{
    return static_cast<BoxFaces>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr BoxFaces operator&(BoxFaces a, BoxFaces b)
{
    return static_cast<BoxFaces>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}
constexpr BoxFaces operator~(BoxFaces a)
{
    return static_cast<BoxFaces>(~static_cast<unsigned>(a) & 63U);
}
constexpr bool Has(BoxFaces set, BoxFaces f)
{
    return (static_cast<unsigned>(set) & static_cast<unsigned>(f)) != 0;
}

/**
 * Accumulates lit, textured triangles on the CPU. Triangles are counter-clockwise
 * seen from outside (the glTF convention), so everything in the school is drawn
 * with RasterizerState::CullClockwise.
 *
 * Surfaces are tessellated into small cells because lighting is baked per vertex:
 * a single quad per wall would light only its four corners.
 */
class MeshBuilder {
public:
    struct Vertex {
        Microsoft::Xna::Framework::Vector3 position;
        Microsoft::Xna::Framework::Vector3 normal;
        Microsoft::Xna::Framework::Vector2 uv;
        Microsoft::Xna::Framework::Vector3 albedo;  // linear 0..1 tint
        float emissive = 0.0f;                      // 1 = ignore lighting
        float alpha = 1.0f;                         // vertex alpha (glass, nets)
    };

    /** Alpha given to every vertex added from now on (1 = opaque). */
    void SetAlpha(float alpha) { alpha_ = alpha; }

    /**
     * origin + u*U + v*V, with the front face toward cross(U, V).
     * `texMeters` > 0 maps one texture repeat to that many metres; 0 maps the whole patch to 0..1.
     */
    void AddPatch(const Microsoft::Xna::Framework::Vector3& origin,
                  const Microsoft::Xna::Framework::Vector3& U,
                  const Microsoft::Xna::Framework::Vector3& V, float texMeters,
                  const Microsoft::Xna::Framework::Vector3& tint, float maxCell = 1.0f,
                  float emissive = 0.0f);

    void AddBox(const Microsoft::Xna::Framework::Vector3& min,
                const Microsoft::Xna::Framework::Vector3& max, float texMeters,
                const Microsoft::Xna::Framework::Vector3& tint, BoxFaces faces = BoxFaces::All,
                float maxCell = 1.0f, float emissive = 0.0f);

    /** A box given in a local frame: `world` takes local coordinates to world space (rotation + translation). */
    void AddBoxTransformed(const Microsoft::Xna::Framework::Matrix& world, const Microsoft::Xna::Framework::Vector3& min,
                           const Microsoft::Xna::Framework::Vector3& max, float texMeters, const Microsoft::Xna::Framework::Vector3& tint,
                           BoxFaces faces = BoxFaces::All, float maxCell = 4.0f, float emissive = 0.0f);

    /** Appends every vertex and triangle of another builder. */
    void Append(const MeshBuilder& other);

    /** Appends another builder's triangles moved by `world` (rotation and translation only: normals are rotated, not rescaled). */
    void AppendTransformed(const MeshBuilder& other, const Microsoft::Xna::Framework::Matrix& world);

    /** Vertical cylinder with smooth side normals. */
    void AddCylinder(const Microsoft::Xna::Framework::Vector3& baseCentre, float radius, float height,
                     int segments, float texMeters, const Microsoft::Xna::Framework::Vector3& tint,
                     bool caps = true, float emissive = 0.0f);

    /** UV sphere; normals point outward, or inward when `inside` is set (sky domes). */
    void AddSphere(const Microsoft::Xna::Framework::Vector3& centre, float radius, int slices, int stacks,
                   const Microsoft::Xna::Framework::Vector3& tint, bool inside = false,
                   float emissive = 0.0f);

    /** Vertical cone/frustum, top radius may be zero. */
    void AddCone(const Microsoft::Xna::Framework::Vector3& baseCentre, float baseRadius, float topRadius,
                 float height, int segments, const Microsoft::Xna::Framework::Vector3& tint,
                 float emissive = 0.0f);

    int AddVertex(const Vertex& v);
    void AddTriangle(int a, int b, int c);

    [[nodiscard]] bool Empty() const { return indices_.empty(); }
    [[nodiscard]] std::size_t VertexCount() const { return vertices_.size(); }
    [[nodiscard]] const std::vector<Vertex>& Vertices() const { return vertices_; }
    [[nodiscard]] const std::vector<std::uint16_t>& Indices() const { return indices_; }
    [[nodiscard]] Microsoft::Xna::Framework::BoundingBox Bounds() const;

    /** Bakes lighting into vertex colours: ambient + sum of the lights (half-Lambert, smooth falloff). */
    [[nodiscard]] std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture>
    Bake(const std::vector<BakedLight>& lights, const Microsoft::Xna::Framework::Vector3& ambient) const;

private:
    std::vector<Vertex> vertices_;
    std::vector<std::uint16_t> indices_;
    float alpha_ = 1.0f;
};

/**
 * Builders grouped by texture name, so a room draws with one call per texture. Indices are 16-bit, so a
 * texture whose geometry grows past 60 000 vertices is continued in a further builder.
 */
class MeshSet {
public:
    using Group = std::vector<MeshBuilder>;
    MeshBuilder& Opaque(const std::string& texture) { return Current(opaque_[texture]); }
    MeshBuilder& Transparent(const std::string& texture) { return Current(transparent_[texture]); }
    [[nodiscard]] const std::map<std::string, Group>& OpaqueMeshes() const { return opaque_; }
    [[nodiscard]] const std::map<std::string, Group>& TransparentMeshes() const { return transparent_; }
    [[nodiscard]] bool Empty() const { return opaque_.empty() && transparent_.empty(); }

    /** Appends another set's geometry texture by texture (corridor segments that are drawn as one). */
    void Merge(const MeshSet& other)
    {
        for (const auto& [texture, group] : other.opaque_)
            for (const MeshBuilder& b : group) Add(opaque_[texture], b);
        for (const auto& [texture, group] : other.transparent_)
            for (const MeshBuilder& b : group) Add(transparent_[texture], b);
    }

private:
    static void Add(Group& g, const MeshBuilder& b)
    {
        if (g.empty() || g.back().VertexCount() + b.VertexCount() > 65000) g.emplace_back();
        g.back().Append(b);
    }
    static MeshBuilder& Current(Group& g)
    {
        if (g.empty() || g.back().VertexCount() > 60000) g.emplace_back();
        return g.back();
    }
    std::map<std::string, Group> opaque_;
    std::map<std::string, Group> transparent_;
};
