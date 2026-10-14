#pragma once

// The visible school: room interiors built lazily around the player and dropped again when far away,
// the building's facade and roofs built once, and the door leaves drawn from the simulation's door
// state every frame. Every mesh is static vertex-coloured geometry drawn through BasicEffect.

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Rendering/RoomGeometry.hpp"
#include "Rendering/StaticMesh.hpp"
#include "Simulation/SchoolSim.hpp"
#include "World/SchoolPlan.hpp"

struct WorldStats {
    int roomsLoaded = 0;
    int roomsDrawn = 0;
    int roomsBuiltTotal = 0;
    int trianglesDrawn = 0;
    int drawCalls = 0;
    int doorsDrawn = 0;
    float buildMilliseconds = 0.0f;
};

/** Daylight for the outdoor geometry and the state of the lights indoors. */
struct WorldLighting {
    Microsoft::Xna::Framework::Vector3 outdoorTint{1, 1, 1};   // multiplies baked outdoor colours
    Microsoft::Xna::Framework::Vector3 sunDirection{0, -1, 0}; // towards the scene (for lit door leaves)
    float daylight = 1.0f;
    /** Per room: 1 = lights on, 0 = dark (only the daylight through the windows). Empty = everything lit. */
    const std::vector<float>* roomLight = nullptr;
};

class WorldRenderer {
public:
    WorldRenderer(Services& services, const School::SchoolPlan& plan, const School::LayoutCache& layouts);
    ~WorldRenderer();

    /** A secondary place to keep rooms loaded around (a CCTV camera). */
    struct Anchor {
        Microsoft::Xna::Framework::Vector3 position;
        int level = 1;
        float radius = 22.0f;
    };
    /** Chooses which rooms to keep in memory around the player (and the anchors) and builds a few of them. */
    void Update(const Microsoft::Xna::Framework::Vector3& eye, int level, float dt);
    void Update(const Microsoft::Xna::Framework::Vector3& eye, int level, float dt, const std::vector<Anchor>& anchors);
    /** Draws the opaque world, then door leaves and elevator cabs, then glass. */
    void Draw(const DrawContext& ctx, const WorldLighting& light, const std::vector<School::DoorRuntime>& doors, const std::vector<School::ElevatorStatus>& elevators = {});

    [[nodiscard]] bool IsLoaded(School::RoomId room) const { return rooms_.count(room) != 0; }
    [[nodiscard]] const std::vector<School::RoomId>& LoadedRooms() const { return loadedList_; }
    [[nodiscard]] const WorldStats& Stats() const { return stats_; }
    /** Forces a room to be built now (scripted teleports). */
    void Ensure(School::RoomId room);
    /** Drops every room (a renderer or texture change). */
    void Clear();

    /** Visual bounds of a room (floor to ceiling). */
    [[nodiscard]] Microsoft::Xna::Framework::BoundingBox RoomBounds(School::RoomId room) const;

    /** Radius within which rooms on the player's level are kept loaded. */
    float loadRadius = 70.0f;
    float adjacentLevelRadius = 36.0f;
    int maxLoaded = 240;
    int buildsPerFrame = 3;

private:
    struct Batch {
        Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
        std::unique_ptr<StaticMesh> mesh;
    };
    struct LoadedRoom {
        School::RoomId id = School::kNone;
        Microsoft::Xna::Framework::BoundingBox bounds;
        std::vector<Batch> opaque;
        std::vector<Batch> transparent;
        bool outdoor = false;
        int triangles = 0;
        float distance = 0.0f;
    };
    struct Chunk {
        Microsoft::Xna::Framework::BoundingBox bounds;
        std::vector<Batch> opaque;
        std::vector<Batch> transparent;
    };

    void Upload(const MeshSet& set, const std::vector<BakedLight>& lights, const Microsoft::Xna::Framework::Vector3& ambient, std::vector<Batch>& opaque,
                std::vector<Batch>& transparent, Microsoft::Xna::Framework::BoundingBox& bounds, int& triangles);
    void Build(School::RoomId room);
    float DistanceTo(const School::Room& room, const Microsoft::Xna::Framework::Vector3& eye) const;
    void DrawBatches(const DrawContext& ctx, const std::vector<Batch>& batches, bool transparent);
    void DrawDoors(const DrawContext& ctx, const WorldLighting& light, const std::vector<School::DoorRuntime>& doors);
    void DrawElevators(const DrawContext& ctx, const std::vector<School::ElevatorStatus>& elevators);
    void DrawLamps(const DrawContext& ctx, const WorldLighting& light);
    struct Lamp {
        Microsoft::Xna::Framework::Vector3 head;
        float groundY;
    };
    std::vector<Lamp> lamps_;
    std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture> lampScratch_;

    Services& s_;
    const School::SchoolPlan& plan_;
    const School::LayoutCache& layouts_;
    std::unordered_map<int, std::unique_ptr<LoadedRoom>> rooms_;
    std::vector<School::RoomId> loadedList_;
    std::vector<Chunk> facade_;
    std::vector<School::RoomId> buildQueue_;
    WorldStats stats_;
    Microsoft::Xna::Framework::Vector3 lastEye_;
    int lastLevel_ = 1;
    float sinceScan_ = 1.0f;
};
