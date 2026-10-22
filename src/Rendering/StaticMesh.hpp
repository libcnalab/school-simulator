#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include "CnaEngine/Graphics/GpuMesh.hpp"

/** An immutable indexed triangle list living in GPU buffers. The effect is applied by the caller. */
class StaticMesh {
public:
    StaticMesh(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
               const std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture>& vertices,
               const std::vector<std::uint16_t>& indices,
               const Microsoft::Xna::Framework::BoundingBox& bounds);
    /** With a normal per vertex (cna-engine's PositionNormalColorTexture layout): outdoor surfaces, whose baked light the
     *  sun's shadow takes its share of (Material::bakedKeyLight). */
    StaticMesh(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
               const std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture>& vertices,
               const std::vector<Microsoft::Xna::Framework::Vector3>& normals,
               const std::vector<std::uint16_t>& indices,
               const Microsoft::Xna::Framework::BoundingBox& bounds);

    void Draw(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const;
    [[nodiscard]] const Microsoft::Xna::Framework::BoundingBox& Bounds() const { return bounds_; }
    [[nodiscard]] int TriangleCount() const { return triangles_; }
    [[nodiscard]] bool HasNormals() const { return gpu_.Attributes().Has(CnaEngine::Graphics::VertexAttribute::Normal); }
    /** The buffers as cna-engine's mesh (borrowed: this object owns them). */
    [[nodiscard]] const CnaEngine::Graphics::GpuMesh& Gpu() const { return gpu_; }

private:
    void UploadIndices(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const std::vector<std::uint16_t>& indices);
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::VertexBuffer> vertexBuffer_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indexBuffer_;
    Microsoft::Xna::Framework::BoundingBox bounds_;
    int vertexCount_ = 0;
    int triangles_ = 0;
    CnaEngine::Graphics::GpuMesh gpu_;
};
