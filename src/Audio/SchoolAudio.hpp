#pragma once

// The school's soundscape on top of AudioSystem: room ambiences that follow occupancy, corridor chatter
// that follows the crowd around the player, bells and alarms, doors, footsteps and the PA system.

#include <map>
#include <string>
#include <vector>

#include "Audio/AudioSystem.hpp"
#include "Rendering/WorldRenderer.hpp"
#include "Simulation/SchoolSim.hpp"

class SchoolAudio {
public:
    SchoolAudio(AudioSystem& audio, const School::SchoolPlan& plan) : audio_(audio), plan_(plan) {}

    /** Call once per frame after the simulation stepped. `playerRoom` may be kNone. */
    void Update(float dt, const Microsoft::Xna::Framework::Vector3& listener, const Microsoft::Xna::Framework::Vector3& forward, School::SchoolSim& sim,
                const WorldRenderer& world, School::RoomId playerRoom, bool outdoors);

    void Footstep(bool running, bool hardFloor);
    /** Chime and a spoken-sounding announcement from every speaker (it is heard everywhere). */
    void Announce(int which);
    void Bell(bool alarm);
    void SetWeather(const std::string& weather);
    void Click();

    [[nodiscard]] int ActiveLoops() const { return static_cast<int>(loops_.size()); }

private:
    struct Loop {
        Voice3D* voice = nullptr;
        float target = 0.0f;
    };
    const char* AmbienceFor(const School::Room& room, const School::SchoolSim& sim, float& baseVolume, float& radius) const;

    AudioSystem& audio_;
    const School::SchoolPlan& plan_;
    std::map<int, Loop> loops_;          // by room id
    Voice3D* chatter_ = nullptr;
    Voice3D* weather_ = nullptr;
    Voice3D* outdoor_ = nullptr;
    float scanTimer_ = 1.0f;
    float chatterLevel_ = 0.0f;
    std::string weatherName_ = "clear";
    bool outdoors_ = false;
    int stepIndex_ = 0;
};
