#pragma once

// A plan view of one level: rooms as coloured rectangles, doors as gaps, people as dots, the player as a
// marker. Drawn with SpriteBatch rectangles only, inside an open sprite batch (see Hud::Draw).

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Rendering/WorldRenderer.hpp"
#include "UI/Hud.hpp"

struct MapView {
    int level = 1;
    Microsoft::Xna::Framework::Vector3 player;
    float playerYaw = 0.0f;
    float metresAcross = 90.0f;        // how much of the school fits in the panel's width
    bool follow = true;                // centred on the player; otherwise on the whole campus
    bool showPeople = true;
    bool showLoaded = false;           // outline the rooms that are built
    School::RoomId currentRoom = School::kNone;
};

class Minimap {
public:
    Minimap(Services& services, Hud& hud) : s_(services), hud_(hud) {}
    void Draw(const MapView& view, const Microsoft::Xna::Framework::Rectangle& panel, const School::SchoolSim& sim, const WorldRenderer* world);

private:
    Services& s_;
    Hud& hud_;
};
