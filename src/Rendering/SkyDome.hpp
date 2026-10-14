#pragma once

#include <memory>
#include <vector>

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

/** Time of day and the light it implies. Shared by the sky and by anything lit by the sun. */
class TimeOfDay {
public:
    void SetHours(float hours);
    void Advance(float hours);
    [[nodiscard]] float Hours() const { return hours_; }
    /** Unit vector pointing from the scene towards the sun (below the horizon at night). */
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 SunDirection() const;
    /** 0 at night .. 1 in full daylight. */
    [[nodiscard]] float Daylight() const;
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 SunColor() const;
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 AmbientColor() const;

private:
    float hours_ = 11.0f;
};

/**
 * A sky dome drawn around the camera: a vertex-coloured gradient, sun and moon
 * billboards, stars, and a scrolling cloud layer. The dome is rebuilt on the CPU into a
 * DynamicVertexBuffer every frame; there are only a few hundred vertices.
 */
class SkyDome {
public:
    SkyDome(Services& services, TimeOfDay& time);

    void Draw(const DrawContext& ctx);

private:
    struct Dome {
        std::vector<Microsoft::Xna::Framework::Vector3> directions;   // unit vectors per vertex
        std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture> vertices;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer> buffer;
        std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indices;
        int indexCount = 0;
    };

    void BuildDome(Dome& dome, int slices, int stacks);
    void DrawDome(const DrawContext& ctx, Dome& dome);

    Services& s_;
    TimeOfDay& time_;
    Dome gradient_;
    Dome clouds_;
    std::vector<Microsoft::Xna::Framework::Vector3> stars_;
    std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture> billboardScratch_;
};
