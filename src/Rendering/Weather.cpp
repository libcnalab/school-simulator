#include "Rendering/Weather.hpp"

#include <cmath>

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Rendering/TextureLibrary.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr std::size_t kCapacity = 6000;
constexpr float kRadius = 16.0f;
}  // namespace

Weather::Weather(Services& services) : s_(services), particles_(kCapacity)
{
    renderer_ = std::make_unique<ParticleRenderer>(*s_.device, static_cast<int>(kCapacity));
}

void Weather::SetKind(const std::string& kind)
{
    if (kind == kind_) return;
    kind_ = kind;
    particles_.Clear();
}

void Weather::Update(float dt, const Vector3& eye, bool outdoors, float groundY)
{
    time_ += dt;
    if (!Precipitation()) {
        if (particles_.Size() > 0) particles_.Clear();
        return;
    }
    const bool rain = kind_ == "rain";
    if (outdoors) {
        // Emission rate per second over the disc around the player.
        carry_ += dt * (rain ? 3600.0f : 700.0f);
        while (carry_ >= 1.0f) {
            carry_ -= 1.0f;
            Particle p;
            const float a = rng_.Range(0.0f, 6.2831853f);
            const float r = kRadius * std::sqrt(rng_.Uniform());
            p.position = Vector3(eye.X + std::cos(a) * r, eye.Y + rng_.Range(4.0f, 11.0f), eye.Z + std::sin(a) * r);
            if (rain) {
                p.velocity = Vector3(1.2f, -11.0f, 0.6f);
                p.life = 1.4f;
                p.size0 = p.size1 = 0.014f;
                p.stretch = 14.0f;
                p.colour0 = Color(200, 215, 240, 170);
                p.colour1 = Color(200, 215, 240, 140);
            } else {
                p.velocity = Vector3(rng_.Range(-0.4f, 0.4f), rng_.Range(-1.6f, -1.0f), rng_.Range(-0.4f, 0.4f));
                p.life = 12.0f;
                p.size0 = p.size1 = rng_.Range(0.05f, 0.11f);
                p.colour0 = Color(255, 255, 255, 235);
                p.colour1 = Color(255, 255, 255, 200);
                p.spin = rng_.Range(-1.0f, 1.0f);
            }
            particles_.Emit(p);
        }
    }
    ParticleSystem::Physics physics;
    physics.hasFloor = true;
    physics.floorY = groundY;
    physics.bounce = 0.0f;
    if (!rain) {
        // Snow sways: nudge every flake sideways with a slow wave.
        for (Particle& p : particles_.MutableParticles()) p.velocity.X = 0.5f * std::sin(time_ * 0.7f + p.position.Z * 0.3f);
    }
    particles_.Update(dt, physics);
}

void Weather::Draw(const DrawContext& ctx)
{
    if (particles_.Size() == 0) return;
    renderer_->Draw(ctx, s_.textures->Get("particle"), BlendState::NonPremultiplied, false, {&particles_});
}
