#pragma once

// Rain and snow around the player, as camera-facing particles from the shared particle renderer. Fog is
// not a particle effect: SchoolGame sets BasicEffect fog for it. Nothing falls indoors.

#include <memory>
#include <string>

#include "App/Services.hpp"
#include "Rendering/Particles.hpp"
#include "World/Rng.hpp"

class Weather {
public:
    explicit Weather(Services& services);

    void SetKind(const std::string& kind);   // clear | rain | snow | fog
    [[nodiscard]] const std::string& Kind() const { return kind_; }
    [[nodiscard]] bool Precipitation() const { return kind_ == "rain" || kind_ == "snow"; }

    /** `outdoors` stops emission (drops already in the air keep falling). `groundY` is the player's floor height. */
    void Update(float dt, const Microsoft::Xna::Framework::Vector3& eye, bool outdoors, float groundY);
    void Draw(const DrawContext& ctx);
    [[nodiscard]] int Count() const { return static_cast<int>(particles_.Size()); }

private:
    Services& s_;
    std::string kind_ = "clear";
    ParticleSystem particles_;
    std::unique_ptr<ParticleRenderer> renderer_;
    School::Rng rng_{77};
    float carry_ = 0.0f;
    float time_ = 0.0f;
};
