#include "Rendering/SkyDome.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Rendering/EffectBank.hpp"
#include "Rendering/TextureLibrary.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {

constexpr float kDomeRadius = 100.0f;

float SmoothStep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vector3 Mix(const Vector3& a, const Vector3& b, float t)
{
    return a + (b - a) * t;
}

Color ToColor(const Vector3& c, float alpha = 1.0f)
{
    const auto b = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return Color(b(c.X), b(c.Y), b(c.Z), b(alpha));
}

}  // namespace

// --- TimeOfDay ------------------------------------------------------------------

void TimeOfDay::SetHours(float hours)
{
    hours_ = std::fmod(hours, 24.0f);
    if (hours_ < 0.0f) hours_ += 24.0f;
}

void TimeOfDay::Advance(float hours)
{
    SetHours(hours_ + hours);
}

Vector3 TimeOfDay::SunDirection() const
{
    const float angle = (hours_ - 6.0f) / 12.0f * MathHelper::Pi;   // 0 at sunrise, pi at sunset
    const float elevation = std::sin(angle) * 1.15f;                  // peak about 66 degrees
    const float azimuth = std::cos(angle);                            // east -> west
    return Vector3::Normalize(Vector3(azimuth * std::cos(elevation), std::sin(elevation), -0.35f));
}

float TimeOfDay::Daylight() const
{
    return SmoothStep(-0.12f, 0.25f, SunDirection().Y);
}

Vector3 TimeOfDay::SunColor() const
{
    const float h = SunDirection().Y;
    const float warm = 1.0f - SmoothStep(0.05f, 0.45f, h);   // low sun is orange
    return Mix(Vector3(1.0f, 0.96f, 0.88f), Vector3(1.0f, 0.55f, 0.28f), warm) * Daylight();
}

Vector3 TimeOfDay::AmbientColor() const
{
    return Mix(Vector3(0.05f, 0.06f, 0.12f), Vector3(0.42f, 0.48f, 0.58f), Daylight());
}

// --- SkyDome ----------------------------------------------------------------------

SkyDome::SkyDome(Services& services, TimeOfDay& time) : s_(services), time_(time)
{
    BuildDome(gradient_, 28, 14);
    BuildDome(clouds_, 28, 8);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    for (int i = 0; i < 220; ++i) {
        const float u = uni(rng) * MathHelper::TwoPi;
        const float y = 0.08f + uni(rng) * 0.92f;
        const float r = std::sqrt(1.0f - y * y);
        stars_.emplace_back(std::cos(u) * r, y, std::sin(u) * r);
    }
    billboardScratch_.reserve(static_cast<std::size_t>(stars_.size() + 2) * 4);
}

void SkyDome::BuildDome(Dome& dome, int slices, int stacks)
{
    // Upper hemisphere plus a little below the horizon so the seam is never visible.
    const float phiMax = MathHelper::PiOver2 + 0.18f;
    for (int j = 0; j <= stacks; ++j) {
        const float phi = static_cast<float>(j) / static_cast<float>(stacks) * phiMax;   // from the zenith down
        for (int i = 0; i <= slices; ++i) {
            const float theta = static_cast<float>(i) / static_cast<float>(slices) * MathHelper::TwoPi;
            dome.directions.emplace_back(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
        }
    }
    dome.vertices.resize(dome.directions.size());
    std::vector<std::uint16_t> idx;
    const int row = slices + 1;
    for (int j = 0; j < stacks; ++j) {
        for (int i = 0; i < slices; ++i) {
            const int a = j * row + i, b = a + 1, c = a + row, d = c + 1;
            // Seen from inside the dome: counter-clockwise a, d, c / a, b, d flipped.
            idx.insert(idx.end(), {static_cast<std::uint16_t>(a), static_cast<std::uint16_t>(c), static_cast<std::uint16_t>(d),
                                   static_cast<std::uint16_t>(a), static_cast<std::uint16_t>(d), static_cast<std::uint16_t>(b)});
        }
    }
    dome.indexCount = static_cast<int>(idx.size());
    dome.indices = std::make_unique<IndexBuffer>(*s_.device, IndexElementSize::SixteenBits, dome.indexCount, BufferUsage::WriteOnly);
    dome.indices->SetData(idx.data(), dome.indexCount);
    dome.buffer = std::make_unique<DynamicVertexBuffer>(*s_.device, VertexPositionColorTexture::getVertexDeclarationStatic(),
                                                        static_cast<int>(dome.vertices.size()), BufferUsage::WriteOnly);
}

void SkyDome::DrawDome(const DrawContext& ctx, Dome& dome)
{
    dome.buffer->SetData(dome.vertices.data(), 0, static_cast<int>(dome.vertices.size()), SetDataOptions::Discard);
    GraphicsDevice& device = ctx.device;
    device.SetVertexBuffer(dome.buffer.get());
    device.setIndicesProperty(dome.indices.get());
    device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, static_cast<int>(dome.vertices.size()), 0, dome.indexCount / 3);
}

void SkyDome::Draw(const DrawContext& ctx)
{
    GraphicsDevice& device = ctx.device;
    const Vector3 sun = time_.SunDirection();
    const float day = time_.Daylight();
    const float sunHeight = sun.Y;

    // Gradient colours: blue by day, near-black at night, orange horizon around sunrise and sunset.
    const Vector3 zenithDay(0.20f, 0.42f, 0.85f);
    const Vector3 horizonDay(0.72f, 0.84f, 0.95f);
    const Vector3 zenithNight(0.004f, 0.010f, 0.045f);
    const Vector3 horizonNight(0.03f, 0.04f, 0.10f);
    const float twilight = (1.0f - SmoothStep(0.0f, 0.35f, std::abs(sunHeight))) * 0.9f;
    for (std::size_t i = 0; i < gradient_.directions.size(); ++i) {
        const Vector3& d = gradient_.directions[i];
        const float h = std::clamp(d.Y, 0.0f, 1.0f);
        const float t = std::pow(h, 0.55f);
        Vector3 zenith = Mix(zenithNight, zenithDay, day);
        Vector3 horizon = Mix(horizonNight, horizonDay, day);
        Vector3 colour = Mix(horizon, zenith, t);
        const float towardSun = std::max(0.0f, Vector3::Dot(d, Vector3::Normalize(Vector3(sun.X, 0.0f, sun.Z))));
        const float glow = std::pow(towardSun, 5.0f) * (1.0f - t) * twilight;
        colour = colour + Vector3(1.0f, 0.45f, 0.18f) * glow;
        const float halo = std::pow(std::max(0.0f, Vector3::Dot(d, sun)), 24.0f) * day * 0.35f;
        colour = colour + Vector3(1.0f, 0.9f, 0.7f) * halo;
        gradient_.vertices[i] = VertexPositionColorTexture(d * kDomeRadius, ToColor(colour), Vector2(0, 0));
    }

    // Clouds: project each direction onto a plane and scroll it slowly.
    const float scroll = static_cast<float>(ctx.time) * 0.004f;
    const Vector3 cloudTint = Mix(Vector3(0.10f, 0.11f, 0.18f), Mix(Vector3(1.0f, 0.62f, 0.45f), Vector3(1.0f, 1.0f, 1.0f), 1.0f - twilight * 0.7f), day);
    for (std::size_t i = 0; i < clouds_.directions.size(); ++i) {
        const Vector3& d = clouds_.directions[i];
        const float denom = std::max(d.Y, 0.12f) + 0.15f;
        const Vector2 uv(d.X / denom * 0.9f + scroll, d.Z / denom * 0.9f + scroll * 0.4f);
        const float fade = SmoothStep(0.02f, 0.30f, d.Y);
        clouds_.vertices[i] = VertexPositionColorTexture(d * (kDomeRadius * 0.96f), ToColor(cloudTint, fade), uv);
    }

    const Matrix world = Matrix::CreateTranslation(ctx.view.eye);
    BasicEffect& flat = ctx.fx.Flat();
    BasicEffect& textured = ctx.fx.World();

    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.getSamplerStatesProperty()[0] = SamplerState::LinearWrap;

    flat.setWorldProperty(world);
    flat.setViewProperty(ctx.view.view);
    flat.setProjectionProperty(ctx.view.projection);
    EffectBank::Apply(flat);
    DrawDome(ctx, gradient_);

    // Everything below is additive or alpha blended on top of the gradient, still without depth.
    device.setBlendStateProperty(BlendState::NonPremultiplied);
    textured.setWorldProperty(world);
    textured.setViewProperty(ctx.view.view);
    textured.setProjectionProperty(ctx.view.projection);
    textured.setDiffuseColorProperty(Vector3(1, 1, 1));
    textured.setTextureProperty(s_.textures->Get("clouds"));
    EffectBank::Apply(textured);
    DrawDome(ctx, clouds_);

    // Stars, sun and moon: camera-facing quads in the particle texture, additive.
    const Matrix viewRotation = ctx.view.view;
    const Vector3 camRight(viewRotation.M11, viewRotation.M21, viewRotation.M31);
    const Vector3 camUp(viewRotation.M12, viewRotation.M22, viewRotation.M32);
    billboardScratch_.clear();
    const auto quad = [&](const Vector3& dir, float size, const Color& c) {
        const Vector3 centre = dir * (kDomeRadius * 0.9f);
        const Vector3 r = camRight * size;
        const Vector3 u = camUp * size;
        billboardScratch_.emplace_back(centre - r + u, c, Vector2(0, 0));
        billboardScratch_.emplace_back(centre + r + u, c, Vector2(1, 0));
        billboardScratch_.emplace_back(centre + r - u, c, Vector2(1, 1));
        billboardScratch_.emplace_back(centre - r + u, c, Vector2(0, 0));
        billboardScratch_.emplace_back(centre + r - u, c, Vector2(1, 1));
        billboardScratch_.emplace_back(centre - r - u, c, Vector2(0, 1));
    };
    const float night = 1.0f - day;
    if (night > 0.02f) {
        for (const Vector3& star : stars_) quad(star, 0.55f, ToColor(Vector3(1, 1, 1), night * 0.85f));
        quad(-sun, 4.5f, ToColor(Vector3(0.85f, 0.9f, 1.0f), std::min(1.0f, night * 1.4f)));
    }
    if (sunHeight > -0.2f) {
        quad(sun, 9.0f, ToColor(Mix(Vector3(1.0f, 0.55f, 0.3f), Vector3(1.0f, 0.97f, 0.85f), SmoothStep(0.0f, 0.5f, sunHeight)), 1.0f));
        quad(sun, 22.0f, ToColor(Vector3(1.0f, 0.85f, 0.6f), 0.35f * std::clamp(sunHeight + 0.2f, 0.0f, 1.0f)));
    }
    device.setBlendStateProperty(BlendState::Additive);
    textured.setTextureProperty(s_.textures->Get("particle"));
    textured.setWorldProperty(world);
    EffectBank::Apply(textured);
    if (!billboardScratch_.empty())
        device.DrawUserPrimitives(PrimitiveType::TriangleList, billboardScratch_.data(), 0,
                                  static_cast<int>(billboardScratch_.size()) / 3);
}
