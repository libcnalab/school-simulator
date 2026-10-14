#include "Rendering/StaticMesh.hpp"

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

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
}

void StaticMesh::Draw(GraphicsDevice& device) const
{
    device.SetVertexBuffer(vertexBuffer_.get());
    device.setIndicesProperty(indexBuffer_.get());
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, vertexCount_, 0, triangles_);
}
