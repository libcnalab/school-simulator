#pragma once

// Small animated demonstrations in the specialist rooms, drawn as lit primitives: a pendulum and a launched
// ball in the physics labs, bubbling flasks in chemistry, a turning helix in biology, running machines in
// the workshops, bouncing balls in the gym, a metronome on the piano, steam over the kitchen's pots and the blinking
// lights of the server racks. Only rooms that are loaded and near the player are animated.

#include <map>
#include <vector>

#include "App/Services.hpp"
#include "Rendering/WorldRenderer.hpp"
#include "Rooms/RoomLayout.hpp"
#include "World/SchoolPlan.hpp"

class RoomEffects {
public:
    RoomEffects(Services& services, const School::SchoolPlan& plan, const School::LayoutCache& layouts) : s_(services), plan_(plan), layouts_(layouts) {}

    void Update(float dt, const Microsoft::Xna::Framework::Vector3& eye, const WorldRenderer& world);
    void Draw(const DrawContext& ctx);
    [[nodiscard]] int ActiveRooms() const { return static_cast<int>(active_.size()); }

private:
    struct Ball {
        Microsoft::Xna::Framework::Vector3 pos, vel;
    };
    struct RoomState {
        School::RoomId room = School::kNone;
        float time = 0.0f;
        std::vector<Ball> balls;   // gym
    };
    void DrawShape(const DrawContext& ctx, const class LitMesh& mesh, const Microsoft::Xna::Framework::Matrix& world, const Microsoft::Xna::Framework::Vector3& colour, float alpha = 1.0f);
    void DrawLab(const DrawContext& ctx, const School::Room& r, RoomState& st);
    void DrawWorkshop(const DrawContext& ctx, const School::Room& r, RoomState& st);
    void DrawGym(const DrawContext& ctx, const School::Room& r, RoomState& st);
    void DrawArt(const DrawContext& ctx, const School::Room& r, RoomState& st);
    void DrawMusic(const DrawContext& ctx, const School::Room& r, RoomState& st);
    void DrawKitchen(const DrawContext& ctx, const School::Room& r, RoomState& st);
    void DrawServer(const DrawContext& ctx, const Microsoft::Xna::Framework::Vector3& eye, const School::Room& r, RoomState& st);

    Services& s_;
    const School::SchoolPlan& plan_;
    const School::LayoutCache& layouts_;
    std::map<int, RoomState> active_;
    float range_ = 45.0f;
};
