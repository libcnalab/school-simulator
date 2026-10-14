#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Rendering/MeshBuilder.hpp"

/**
 * An indexed triangle list with normals and texture coordinates
 * (VertexPositionNormalTexture), for objects lit at draw time by BasicEffect.
 * Built from a MeshBuilder; the builder's baked-lighting fields are ignored.
 */
class LitMesh {
public:
    LitMesh(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const MeshBuilder& builder);

    void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingBox& Bounds() const { return bounds_; }
    [[nodiscard]] int TriangleCount() const { return triangles_; }
    [[nodiscard]] int VertexCount() const { return vertexCount_; }
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::VertexBuffer* Vertices() const { return vertexBuffer_.get(); }
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::IndexBuffer* Indices() const { return indexBuffer_.get(); }

private:
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertexBuffer_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indexBuffer_;
    Microsoft::Xna::Framework::BoundingBox bounds_;
    int vertexCount_ = 0;
    int triangles_ = 0;
};

/** The handful of primitive shapes several exhibits share, created once. All are unit sized. */
class Shapes {
public:
    explicit Shapes(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

    /** Radius 0.5 sphere centred on the origin. */
    [[nodiscard]] const LitMesh& Sphere() const { return *sphere_; }
    /** 1x1x1 cube centred on the origin, each face mapped 0..1. */
    [[nodiscard]] const LitMesh& Cube() const { return *cube_; }
    /** Radius 0.5, height 1 cylinder standing on y = 0. */
    [[nodiscard]] const LitMesh& Cylinder() const { return *cylinder_; }
    /** Radius 0.5, height 1 cone standing on y = 0. */
    [[nodiscard]] const LitMesh& Cone() const { return *cone_; }
    /** 1x1 quad in the XZ plane, facing +Y, centred on the origin, UV 0..1. */
    [[nodiscard]] const LitMesh& Plane() const { return *plane_; }
    /** 1x1 quad in the XY plane, facing +Z, centred on the origin, UV 0..1. */
    [[nodiscard]] const LitMesh& Panel() const { return *panel_; }

private:
    std::unique_ptr<LitMesh> sphere_, cube_, cylinder_, cone_, plane_, panel_;
};
