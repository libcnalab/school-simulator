#include "Rendering/LitMesh.hpp"

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

LitMesh::LitMesh(GraphicsDevice& device, const MeshBuilder& builder)
    : bounds_(builder.Bounds()),
      vertexCount_(static_cast<int>(builder.VertexCount())),
      triangles_(static_cast<int>(builder.Indices().size()) / 3)
{
    std::vector<VertexPositionNormalTexture> vertices;
    vertices.reserve(builder.Vertices().size());
    for (const MeshBuilder::Vertex& v : builder.Vertices()) vertices.emplace_back(v.position, v.normal, v.uv);
    vertexBuffer_ = std::make_unique<VertexBuffer>(device, VertexPositionNormalTexture::getVertexDeclarationStatic(),
                                                   vertexCount_, BufferUsage::WriteOnly);
    vertexBuffer_->SetData(vertices.data(), vertexCount_);
    indexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits,
                                                 static_cast<int>(builder.Indices().size()), BufferUsage::WriteOnly);
    indexBuffer_->SetData(builder.Indices().data(), static_cast<int>(builder.Indices().size()));
}

void LitMesh::Draw(GraphicsDevice& device) const
{
    device.SetVertexBuffer(vertexBuffer_.get());
    device.setIndicesProperty(indexBuffer_.get());
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, vertexCount_, 0, triangles_);
}

Shapes::Shapes(GraphicsDevice& device)
{
    const Vector3 white(1, 1, 1);
    {
        MeshBuilder b;
        b.AddSphere(Vector3::Zero, 0.5f, 40, 20, white);
        sphere_ = std::make_unique<LitMesh>(device, b);
    }
    {
        MeshBuilder b;
        b.AddBox(Vector3(-0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, 0.5f), 0.0f, white, BoxFaces::All, 10.0f);
        cube_ = std::make_unique<LitMesh>(device, b);
    }
    {
        MeshBuilder b;
        b.AddCylinder(Vector3::Zero, 0.5f, 1.0f, 36, 0.0f, white, true);
        cylinder_ = std::make_unique<LitMesh>(device, b);
    }
    {
        MeshBuilder b;
        b.AddCone(Vector3::Zero, 0.5f, 0.0f, 1.0f, 36, white);
        cone_ = std::make_unique<LitMesh>(device, b);
    }
    {
        MeshBuilder b;
        b.AddPatch(Vector3(-0.5f, 0, 0.5f), Vector3(1, 0, 0), Vector3(0, 0, -1), 0.0f, white, 10.0f);
        plane_ = std::make_unique<LitMesh>(device, b);
    }
    {
        MeshBuilder b;
        b.AddPatch(Vector3(-0.5f, -0.5f, 0), Vector3(1, 0, 0), Vector3(0, 1, 0), 0.0f, white, 10.0f);
        panel_ = std::make_unique<LitMesh>(device, b);
    }
}
