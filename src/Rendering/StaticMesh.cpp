#include "Rendering/StaticMesh.hpp"

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"

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
    indexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits,
                                                 static_cast<int>(indices.size()), BufferUsage::WriteOnly);
    indexBuffer_->SetData(indices.data(), static_cast<int>(indices.size()));
    using CnaEngine::Graphics::VertexAttribute;
    gpu_ = CnaEngine::Graphics::GpuMesh::Borrow(*vertexBuffer_, *indexBuffer_, vertexCount_, triangles_, bounds_,
                                                {VertexAttribute::Position, VertexAttribute::Color, VertexAttribute::TexCoord0});
}

void StaticMesh::Draw(GraphicsDevice& device) const
{
    gpu_.Draw(device);
}
