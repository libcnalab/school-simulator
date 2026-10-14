#pragma once

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

/** One particle: simple Euler motion, colour and size interpolated over its life. */
struct Particle {
    Microsoft::Xna::Framework::Vector3 position;
    Microsoft::Xna::Framework::Vector3 velocity;
    float age = 0.0f;
    float life = 1.0f;
    float size0 = 0.1f;
    float size1 = 0.1f;
    float rotation = 0.0f;
    float spin = 0.0f;
    float stretch = 1.0f;         // > 1: a streak this many times taller than wide, aligned with the world's up
    Microsoft::Xna::Framework::Color colour0;
    Microsoft::Xna::Framework::Color colour1;
};

/**
 * A fixed-capacity pool. Emit() overwrites nothing when the pool is full, so a flood of
 * requests degrades to "fewer particles" instead of allocating. Dead particles are removed
 * by swapping with the last one, so the pool never reorders memory beyond that.
 */
class ParticleSystem {
public:
    explicit ParticleSystem(std::size_t capacity) : capacity_(capacity) { particles_.reserve(capacity); }

    bool Emit(const Particle& p)
    {
        if (particles_.size() >= capacity_) return false;
        particles_.push_back(p);
        return true;
    }

    struct Physics {
        Microsoft::Xna::Framework::Vector3 gravity{0, 0, 0};
        float drag = 0.0f;           // fraction of velocity lost per second
        bool hasFloor = false;
        float floorY = 0.0f;
        float bounce = 0.0f;         // 0 = die on contact, otherwise restitution
    };
    void Update(float dt, const Physics& physics);
    void Clear() { particles_.clear(); }

    [[nodiscard]] const std::vector<Particle>& Particles() const { return particles_; }
    /** For effects that move particles themselves, e.g. dust wrapping around a room. */
    [[nodiscard]] std::vector<Particle>& MutableParticles() { return particles_; }
    [[nodiscard]] std::size_t Size() const { return particles_.size(); }

private:
    std::size_t capacity_;
    std::vector<Particle> particles_;
};

/**
 * Draws many camera-facing quads per call from one DynamicVertexBuffer (rewritten with
 * SetDataOptions::Discard) and one static IndexBuffer shared by every quad.
 */
class ParticleRenderer {
public:
    ParticleRenderer(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, int maxQuads);

    /** Draws every particle of the given systems with one texture and one blend state. */
    void Draw(const DrawContext& ctx, Microsoft::Xna::Framework::Graphics::Texture2D* texture,
              const Microsoft::Xna::Framework::Graphics::BlendState& blend, bool sortBackToFront,
              const std::vector<const ParticleSystem*>& systems);

    [[nodiscard]] int LastQuadCount() const { return lastQuads_; }

private:
    struct Item {
        const Particle* particle;
        float distanceSquared;
    };

    int maxQuads_;
    int lastQuads_ = 0;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer> vertexBuffer_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indexBuffer_;
    std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture> vertices_;
    std::vector<Item> order_;
};
