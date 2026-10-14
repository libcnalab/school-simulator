#include "Rendering/Particles.hpp"

#include <algorithm>
#include <cmath>

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Rendering/EffectBank.hpp"
#include "Rendering/RenderStates.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

void ParticleSystem::Update(float dt, const Physics& physics)
{
    const float drag = std::max(0.0f, 1.0f - physics.drag * dt);
    for (std::size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.age += dt;
        bool dead = p.age >= p.life;
        if (!dead) {
            p.velocity = (p.velocity + physics.gravity * dt) * drag;
            p.position += p.velocity * dt;
            p.rotation += p.spin * dt;
            if (physics.hasFloor && p.position.Y < physics.floorY) {
                if (physics.bounce > 0.0f) {
                    p.position.Y = physics.floorY;
                    p.velocity.Y = -p.velocity.Y * physics.bounce;
                    p.velocity.X *= 0.8f;
                    p.velocity.Z *= 0.8f;
                } else {
                    dead = true;
                }
            }
        }
        if (dead) {
            particles_[i] = particles_.back();
            particles_.pop_back();
        } else {
            ++i;
        }
    }
}

ParticleRenderer::ParticleRenderer(GraphicsDevice& device, int maxQuads) : maxQuads_(maxQuads)
{
    vertexBuffer_ = std::make_unique<DynamicVertexBuffer>(device, VertexPositionColorTexture::getVertexDeclarationStatic(), maxQuads * 4,
                                                          BufferUsage::WriteOnly);
    std::vector<std::uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(maxQuads) * 6);
    for (int q = 0; q < maxQuads; ++q) {
        const auto b = static_cast<std::uint16_t>(q * 4);
        indices.insert(indices.end(), {b, static_cast<std::uint16_t>(b + 1), static_cast<std::uint16_t>(b + 2), b,
                                       static_cast<std::uint16_t>(b + 2), static_cast<std::uint16_t>(b + 3)});
    }
    indexBuffer_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits, static_cast<int>(indices.size()), BufferUsage::WriteOnly);
    indexBuffer_->SetData(indices.data(), static_cast<int>(indices.size()));
    vertices_.reserve(static_cast<std::size_t>(maxQuads) * 4);
    order_.reserve(static_cast<std::size_t>(maxQuads));
}

void ParticleRenderer::Draw(const DrawContext& ctx, Texture2D* texture, const BlendState& blend, bool sortBackToFront,
                            const std::vector<const ParticleSystem*>& systems)
{
    order_.clear();
    for (const ParticleSystem* system : systems) {
        for (const Particle& p : system->Particles()) {
            if (static_cast<int>(order_.size()) >= maxQuads_) break;
            order_.push_back({&p, Vector3::DistanceSquared(p.position, ctx.view.eye)});
        }
    }
    lastQuads_ = static_cast<int>(order_.size());
    if (order_.empty()) return;
    if (sortBackToFront)
        std::sort(order_.begin(), order_.end(), [](const Item& a, const Item& b) { return a.distanceSquared > b.distanceSquared; });

    // Quads face the camera: use the view matrix's right and up axes.
    const Matrix& v = ctx.view.view;
    const Vector3 right(v.M11, v.M21, v.M31);
    const Vector3 up(v.M12, v.M22, v.M32);

    vertices_.clear();
    for (const Item& item : order_) {
        const Particle& p = *item.particle;
        const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
        const float size = p.size0 + (p.size1 - p.size0) * t;
        const auto lerp = [t](std::uint8_t a, std::uint8_t b) {
            return static_cast<std::uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t);
        };
        const Color c(lerp(p.colour0.getRProperty(), p.colour1.getRProperty()), lerp(p.colour0.getGProperty(), p.colour1.getGProperty()),
                      lerp(p.colour0.getBProperty(), p.colour1.getBProperty()), lerp(p.colour0.getAProperty(), p.colour1.getAProperty()));
        const float cs = std::cos(p.rotation) * size;
        const float sn = std::sin(p.rotation) * size;
        const Vector3 r = right * cs + up * sn;
        const Vector3 u = up * cs - right * sn;
        vertices_.emplace_back(p.position - r + u, c, Vector2(0, 0));
        vertices_.emplace_back(p.position + r + u, c, Vector2(1, 0));
        vertices_.emplace_back(p.position + r - u, c, Vector2(1, 1));
        vertices_.emplace_back(p.position - r - u, c, Vector2(0, 1));
    }

    GraphicsDevice& device = ctx.device;
    vertexBuffer_->SetData(vertices_.data(), 0, static_cast<int>(vertices_.size()), SetDataOptions::Discard);
    RenderStates::SetTransparent3D(device, blend);
    BasicEffect& fx = ctx.fx.World();
    fx.setTextureProperty(texture);
    fx.setDiffuseColorProperty(Vector3(1, 1, 1));
    fx.setWorldProperty(Matrix::getIdentityProperty());
    fx.setViewProperty(ctx.view.view);
    fx.setProjectionProperty(ctx.view.projection);
    EffectBank::Apply(fx);
    device.SetVertexBuffer(vertexBuffer_.get());
    device.setIndicesProperty(indexBuffer_.get());
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, static_cast<int>(vertices_.size()), 0, lastQuads_ * 2);
}
