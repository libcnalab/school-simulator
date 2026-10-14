#pragma once

// Draws the people: articulated figures made of shaded boxes (head, torso, arms, legs) with a walk cycle
// and a handful of poses, batched into one dynamic vertex buffer per frame. Detail drops with distance:
// ten boxes close by, three further away, two at the far limit, nothing beyond it.

#include <memory>
#include <vector>

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Rendering/WorldRenderer.hpp"
#include "Simulation/SchoolSim.hpp"

struct NpcDrawStats {
    int drawn = 0;
    int detailed = 0;
    int onCampus = 0;
    int walking = 0;
};

class NpcRenderer {
public:
    explicit NpcRenderer(Services& services);

    /** Draws everybody on campus who is in a loaded room (or outdoors) and inside the view. */
    void Draw(const DrawContext& ctx, const School::SchoolSim& sim, const WorldRenderer& world, NpcDrawStats& stats);

    /** Appearance of one person, derived from their identity; also used by the minimap for colours. */
    struct Look {
        Microsoft::Xna::Framework::Vector3 skin, hair, shirt, trousers, shoes;
        float height = 1.7f;
    };
    static Look LookOf(const School::Npc& npc, const School::Population& pop);

    float detailRadius = 22.0f;    // full figure
    float mediumRadius = 50.0f;    // head, torso, legs
    float farRadius = 110.0f;      // body and head only

private:
    struct Figure {
        Microsoft::Xna::Framework::Vector3 pos;
        float yaw;
        School::Pose pose;
        float walkPhase;      // radians; 0 when standing still
        bool walking;
        float time;           // for idle motions
        int lod;
        Look look;
    };
    void AddFigure(const Figure& f);
    void AddBox(const Microsoft::Xna::Framework::Matrix& world, const Microsoft::Xna::Framework::Vector3& min, const Microsoft::Xna::Framework::Vector3& max,
                const Microsoft::Xna::Framework::Vector3& colour);
    void Flush(const DrawContext& ctx);

    Services& s_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer> vertexBuffer_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::IndexBuffer> indexBuffer_;
    std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture> vertices_;
    int boxes_ = 0;
    int maxBoxes_ = 0;
    const DrawContext* ctx_ = nullptr;
};
