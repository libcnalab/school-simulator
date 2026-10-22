#include "Rendering/StaticMesh.hpp"

#include <cstring>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"

#include "CnaEngine/Graphics/VertexLayouts.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

StaticMesh::StaticMesh(GraphicsDevice& device, const std::vector<VertexPositionColorTexture>& vertices,
                       const std::vector<std::uint16_t>& indices, const BoundingBox& bounds)
    : bounds_(bounds),
      vertexCount_(static_cast<int>(vertices.size())),
      triangles_(static_cast<int>(indices.size()) / 3)
{
    vertexBuffer_ = std::make_unique<VertexBuffer>(
        device, VertexPositionColorTexture::getVertexDeclarationStatic(), vertexCount_, BufferUsage::WriteOnly);
    vertexBuffer_->SetData(vertices.data(), vertexCount_);
    UploadIndices(device, indices);
    using CnaEngine::Graphics::VertexAttribute;
    gpu_ = CnaEngine::Graphics::GpuMesh::Borrow(*vertexBuffer_, *indexBuffer_, vertexCount_, triangles_, bounds_,
                                                {VertexAttribute::Position, VertexAttribute::Color, VertexAttribute::TexCoord0});
}

StaticMesh::StaticMesh(GraphicsDevice& device, const std::vector<VertexPositionColorTexture>& vertices, const std::vector<Vector3>& normals,
                       const std::vector<std::uint16_t>& indices, const BoundingBox& bounds)
    : bounds_(bounds),
      vertexCount_(static_cast<int>(vertices.size())),
      triangles_(static_cast<int>(indices.size()) / 3)
{
    if (normals.size() != vertices.size()) throw std::invalid_argument("StaticMesh: one normal per vertex");
    // CNA's vertex structs are not byte images of their layout: the bytes are written field by field.
    const auto& layout = CnaEngine::Graphics::VertexLayout::PositionNormalColorTexture();
    std::vector<std::uint8_t> bytes(vertices.size() * static_cast<std::size_t>(layout.Stride()));
    std::uint8_t* out = bytes.data();
    const auto put = [&out](const void* data, std::size_t size) {
        std::memcpy(out, data, size);
        out += size;
    };
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const VertexPositionColorTexture& v = vertices[i];
        const float position[3] = {v.Position.X, v.Position.Y, v.Position.Z};
        const float normal[3] = {normals[i].X, normals[i].Y, normals[i].Z};
        const std::uint32_t color = v.Color.getPackedValueProperty();
        const float uv[2] = {v.TextureCoordinate.X, v.TextureCoordinate.Y};
        put(position, sizeof position);
        put(normal, sizeof normal);
        put(&color, sizeof color);
        put(uv, sizeof uv);
    }
    vertexBuffer_ = std::make_unique<VertexBuffer>(device, layout.Declaration(), vertexCount_, BufferUsage::WriteOnly);
    vertexBuffer_->SetData(bytes.data(), static_cast<int>(bytes.size()));
    UploadIndices(device, indices);
    using CnaEngine::Graphics::VertexAttribute;
    gpu_ = CnaEngine::Graphics::GpuMesh::Borrow(*vertexBuffer_, *indexBuffer_, vertexCount_, triangles_, bounds_,
                                                {VertexAttribute::Position, VertexAttribute::Normal, VertexAttribute::Color,
                                                 VertexAttribute::TexCoord0});
}

void StaticMesh::UploadIndices(GraphicsDevice& device, const std::vector<std::uint16_t>& indices)
{
    indexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits,
                                                 static_cast<int>(indices.size()), BufferUsage::WriteOnly);
    indexBuffer_->SetData(indices.data(), static_cast<int>(indices.size()));
}

void StaticMesh::Draw(GraphicsDevice& device) const
{
    gpu_.Draw(device);
}
