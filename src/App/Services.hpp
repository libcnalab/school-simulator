#pragma once

// Everything the application's subsystems share. Pointers are owned by SchoolGame and outlive every user.

#include <functional>
#include <string>

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

class AudioSystem;
class EffectBank;
class TextureLibrary;
class Shapes;
class SkyDome;
class TimeOfDay;
namespace Microsoft::Xna::Framework { class GraphicsDeviceManager; }
namespace School {
class SchoolPlan;
class NavGraph;
class LayoutCache;
class SchoolSim;
}
struct SceneView;

/** Per-frame numbers for the F3 overlay, the IT room screens and the scripted `stats` command. */
struct FrameStats {
    float fps = 0.0f;
    float frameMilliseconds = 0.0f;
    long long frame = 0;
    int backBufferWidth = 0;
    int backBufferHeight = 0;
    bool fullscreen = false;
    // world
    int roomsLoaded = 0;
    int roomsDrawn = 0;
    int roomsCulled = 0;
    int roomsBuiltTotal = 0;
    int trianglesDrawn = 0;
    int drawCalls = 0;
    int doorsDrawn = 0;
    float buildMilliseconds = 0.0f;   // spent building room meshes this frame
    // people
    int npcTotal = 0;
    int npcOnCampus = 0;
    int npcWalking = 0;
    int npcFull = 0;
    int npcReduced = 0;
    int npcDrawn = 0;
    int npcDrawnDetailed = 0;
    float simMilliseconds = 0.0f;
    // audio and effects
    int activeVoices = 0;
    int particles = 0;
    int screensRendered = 0;
    int textures = 0;
};

/** Shared services; see SchoolGame::LoadContent for who fills what. */
struct Services {
    Microsoft::Xna::Framework::Game* game = nullptr;
    Microsoft::Xna::Framework::Graphics::GraphicsDevice* device = nullptr;
    Microsoft::Xna::Framework::Content::ContentManager* content = nullptr;
    Microsoft::Xna::Framework::GraphicsDeviceManager* graphics = nullptr;
    TextureLibrary* textures = nullptr;
    EffectBank* effects = nullptr;
    Shapes* shapes = nullptr;
    SkyDome* sky = nullptr;
    TimeOfDay* timeOfDay = nullptr;
    AudioSystem* audio = nullptr;
    Microsoft::Xna::Framework::Graphics::SpriteBatch* sprites = nullptr;
    Microsoft::Xna::Framework::Graphics::SpriteFont* font = nullptr;
    Microsoft::Xna::Framework::Graphics::SpriteFont* titleFont = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D* whiteTexture = nullptr;
    const FrameStats* stats = nullptr;
    FrameStats* mutableStats = nullptr;
    const SceneView* mainView = nullptr;   // the player's camera for this frame (valid during Draw)
    const School::SchoolPlan* plan = nullptr;
    const School::NavGraph* nav = nullptr;
    const School::LayoutCache* layouts = nullptr;
    School::SchoolSim* sim = nullptr;
    std::string contentRoot;
    bool supportsThreeD = true;
};

/** A camera, its matrices and the frustum built from them. */
struct SceneView {
    Microsoft::Xna::Framework::Matrix view;
    Microsoft::Xna::Framework::Matrix projection;
    Microsoft::Xna::Framework::Vector3 eye;
    Microsoft::Xna::Framework::BoundingFrustum frustum;

    SceneView(const Microsoft::Xna::Framework::Matrix& v, const Microsoft::Xna::Framework::Matrix& p, const Microsoft::Xna::Framework::Vector3& e)
        : view(v), projection(p), eye(e), frustum(v * p)
    {
    }
};

/** Passed to every draw call. `offscreen` is true while rendering into a RenderTarget2D (CCTV, monitors). */
struct DrawContext {
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device;
    EffectBank& fx;
    const SceneView& view;
    double time;
    bool offscreen;
};
