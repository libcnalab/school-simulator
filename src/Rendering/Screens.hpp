#pragma once

// Everything drawn on a screen inside the school: the CCTV wall of the security office (the 3D scene seen
// by cameras elsewhere, rendered into RenderTarget2Ds at a reduced rate), digital signage (clock, phase,
// next bell) and whiteboard content for the lesson in progress. Textures are RenderTarget2Ds drawn with
// SpriteBatch; the quads that show them are drawn with the World effect after the room geometry.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Rendering/NpcRenderer.hpp"
#include "Rendering/SkyDome.hpp"
#include "Rendering/WorldRenderer.hpp"
#include "Simulation/SchoolSim.hpp"

struct CctvCamera {
    std::string name;
    Microsoft::Xna::Framework::Vector3 eye;
    Microsoft::Xna::Framework::Vector3 target;
    int level = 1;
};

class Screens {
public:
    Screens(Services& services, const School::SchoolPlan& plan, const School::LayoutCache& layouts);
    ~Screens();

    [[nodiscard]] const std::vector<CctvCamera>& Cameras() const { return cameras_; }
    /** The first of the four cameras shown on the wall moves on by one. */
    void CycleCameras();
    /** Whether the CCTV wall is near enough to be worth refreshing; decided from the player's position. */
    void Update(float dt, const Microsoft::Xna::Framework::Vector3& eye, const School::SchoolSim& sim);
    [[nodiscard]] bool CctvActive() const { return cctvActive_; }
    /** Rooms to keep loaded for the cameras currently shown (empty when the wall is far away). */
    [[nodiscard]] std::vector<WorldRenderer::Anchor> Anchors() const;

    /** Renders one camera view and refreshes the signage. Call before the back buffer is cleared. */
    void RenderOffscreen(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, EffectBank& fx, SkyDome& sky, WorldRenderer& world, NpcRenderer& npcs,
                         const School::SchoolSim& sim, const WorldLighting& light, double time);
    /** Draws the screen quads in the world (CCTV wall, signage, whiteboards). */
    void Draw(const DrawContext& ctx, const School::SchoolSim& sim, const WorldRenderer& world);

    [[nodiscard]] int RenderedThisFrame() const { return rendered_; }

private:
    struct Quad {
        Microsoft::Xna::Framework::Vector3 centre, right, up;   // half extents along the panel axes
        Microsoft::Xna::Framework::Graphics::Texture2D* texture = nullptr;
        float emissive = 1.0f;
    };
    void DrawQuads(const DrawContext& ctx, const std::vector<Quad>& quads);
    void RenderSignage(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const School::SchoolSim& sim);
    Microsoft::Xna::Framework::Graphics::Texture2D* BoardFor(School::Subject subject);
    Microsoft::Xna::Framework::Graphics::Texture2D* PlateFor(School::RoomId room);

    Services& s_;
    const School::SchoolPlan& plan_;
    const School::LayoutCache& layouts_;
    std::vector<CctvCamera> cameras_;
    std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D>> cameraTargets_;
    std::vector<bool> cameraRendered_;
    int firstShown_ = 0;
    int nextCamera_ = 0;
    bool cctvActive_ = false;
    struct Wall {
        Microsoft::Xna::Framework::Vector3 centre;
        float yaw = 0.0f, width = 3.0f, height = 1.6f, depth = 0.1f;
    };
    std::vector<Wall> walls_;                 // CCTV walls (one per security office)
    struct Sign {
        Microsoft::Xna::Framework::Vector3 centre;
        float yaw = 0.0f;
        float width = 2.4f, height = 1.2f;
    };
    std::vector<Sign> signs_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> signage_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> diagnostics_;
    float diagnosticsAge_ = 10.0f;
    bool diagnosticsNear_ = false;
    struct Monitor {
        Microsoft::Xna::Framework::Vector3 centre;
        float yaw = 0.0f;
    };
    std::vector<Monitor> monitors_;           // the diagnostics monitors (server rooms, security offices)
    void RenderDiagnostics(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    float signageAge_ = 10.0f;
    bool signageNear_ = false;
    std::map<int, std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D>> boards_;
    std::map<int, std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D>> plates_;   // door plates by room
    std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture> scratch_;
    int rendered_ = 0;
    Microsoft::Xna::Framework::Vector3 eye_;
};
