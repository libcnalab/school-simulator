#pragma once

// The visible school: room interiors built lazily around the player and dropped again when far away,
// the building's facade and roofs built once, and the door leaves drawn from the simulation's door
// state every frame. Every mesh is static vertex-coloured geometry; cna-engine's forward renderer draws the
// facade, the rooms and the glass, the game draws doors, elevators and lamps between its opaque and transparent items.

#include <cstdint>
#include <deque>
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

#include "CnaEngine/Graphics/ForwardRenderer.hpp"
#include "CnaEngine/Graphics/Material.hpp"
#include "CnaEngine/Graphics/RenderQueue.hpp"
#include "CnaEngine/Graphics/ShadowMap.hpp"

struct WorldStats {
    int roomsLoaded = 0;
    int roomsDrawn = 0;
    int corridorGroupsDrawn = 0;
    int roomsCulled = 0;   // inside the frustum but out of sight: no open door leads to them
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
    /** The sun's share of the baked outdoor light, 0..1: outdoor surfaces keep (1 - share) and get the share back as the sun
     *  reaches them (N.L, the shadow map). 0 draws them as baked. */
    float sunShare = 0.0f;
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

    /** Draws the sun's shadow map around `focus`: the facade and the loaded outdoor places cast (every room is inside the
     *  facade). `towardsSun` points at the sun. */
    void RenderShadows(CnaEngine::Graphics::ShadowMap& map, const Microsoft::Xna::Framework::Vector3& towardsSun,
                       const Microsoft::Xna::Framework::Vector3& focus, float radius);
    /** The renderer whose effects receive the shadow map (Enhanced::ApplyShadowReception). */
    [[nodiscard]] CnaEngine::Graphics::ForwardRenderer& Renderer() { return *renderer_; }

    /** Visual bounds of a room (floor to ceiling). */
    [[nodiscard]] Microsoft::Xna::Framework::BoundingBox RoomBounds(School::RoomId room) const;

    /** Radius within which rooms on the player's level are kept loaded. */
    float loadRadius = 70.0f;
    float adjacentLevelRadius = 36.0f;
    /** Longest run of corridor segments drawn as one mesh (1 turns the merging off). */
    int maxGroupSegments = 3;
    /** Rooms that no open door leads to are not drawn (off: SCHOOL_PORTAL_CULLING=0). */
    bool portalCulling = true;
    int maxLoaded = 240;
    int buildsPerFrame = 3;
    /** Stop building for this frame after 6 ms. Off for fixed-step runs: what is built by a frame then depends on
     *  the frame count only, not on how fast the machine is (deterministic captures). */
    bool buildTimeBudget = true;

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
        int group = -1;          // corridor group, or -1
        bool follower = false;   // a member of a group whose geometry is drawn with the group's first member
    };
    struct Chunk {
        Microsoft::Xna::Framework::BoundingBox bounds;
        std::vector<Batch> opaque;
        std::vector<Batch> transparent;
    };

    /** `outdoor`: the opaque meshes keep their normals, for the sun's share of their light. */
    void Upload(const MeshSet& set, const std::vector<BakedLight>& lights, const Microsoft::Xna::Framework::Vector3& ambient, bool outdoor,
                std::vector<Batch>& opaque, std::vector<Batch>& transparent, Microsoft::Xna::Framework::BoundingBox& bounds, int& triangles);
    void Build(School::RoomId room);
    /** Builds every member of a corridor group at once; the first member holds the merged batches. */
    void BuildGroup(int group);
    void FindCorridorGroups();
    /** Rooms the eye can see: the room it is in, those reachable through doors that are open (or glass) and in the view,
     *  nearby rooms (a safety margin), and windowed rooms when looking out. False = the eye is in no room: draw all. */
    bool ComputePortalVisibility(const DrawContext& ctx, const std::vector<School::DoorRuntime>& doors, std::vector<char>& seen) const;
    float DistanceTo(const School::Room& room, const Microsoft::Xna::Framework::Vector3& eye) const;
    /** Queues `batches` as render items, unlit, vertex colours times texture, multiplied by `tint`; meshes with normals give
     *  `sunShare` of their light to the sun (WorldLighting::sunShare). */
    void Submit(const std::vector<Batch>& batches, const Microsoft::Xna::Framework::Vector3& tint, bool transparent, float sunShare = 0.0f);
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
    // Adjacent corridor segments of one wing and level, built and drawn together: far fewer draw calls in a long hall.
    std::vector<std::vector<School::RoomId>> groups_;
    std::vector<int> groupOf_;   // per room, index into groups_ or -1
    std::vector<School::RoomId> buildQueue_;
    WorldStats stats_;
    std::unique_ptr<CnaEngine::Graphics::ForwardRenderer> renderer_;
    CnaEngine::Graphics::RenderQueue queue_;
    std::vector<CnaEngine::Graphics::RenderItem> casters_;
    CnaEngine::Graphics::Material casterMaterial_;
    std::deque<CnaEngine::Graphics::Material> frameMaterials_;   // this draw's materials; a deque keeps their addresses
    std::int32_t nextOrder_ = 0;
    Microsoft::Xna::Framework::Vector3 lastEye_;
    int lastLevel_ = 1;
    float sinceScan_ = 1.0f;
};
