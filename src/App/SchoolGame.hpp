#pragma once

// The application: a CNA Game that owns the plan, the simulation, the player, the renderers, audio and HUD.
// Update and Draw orchestrate; every subsystem does its own work.

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "App/SchoolOptions.hpp"
#include "App/Services.hpp"
#include "Audio/AudioSystem.hpp"
#include "Audio/SchoolAudio.hpp"
#include "Interaction/FirstPersonCamera.hpp"
#include "Interaction/InputState.hpp"
#include "Interaction/Interactables.hpp"
#include "Interaction/PlayerController.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Navigation/NavGraph.hpp"
#include "Rendering/EffectBank.hpp"
#include "Rendering/Gizmos.hpp"
#include "Rendering/LitMesh.hpp"
#include "Rendering/NpcRenderer.hpp"
#include "Rendering/RoomEffects.hpp"
#include "Rendering/Screens.hpp"
#include "Rendering/SkyDome.hpp"
#include "Rendering/TextureLibrary.hpp"
#include "Rendering/Weather.hpp"
#include "Rendering/WorldRenderer.hpp"
#include "Rooms/RoomLayout.hpp"
#include "Scene/Collision.hpp"
#include "Simulation/SchoolSim.hpp"
#include "UI/Hud.hpp"
#include "UI/Minimap.hpp"
#include "World/SchoolPlan.hpp"

#include "CnaEngine/Input/ActionMap.hpp"

class SchoolGame : public Microsoft::Xna::Framework::Game {
public:
    explicit SchoolGame(const SchoolOptions& options);
    ~SchoolGame() override;
    GetTypeNameHPP()

protected:
    void LoadContent() override;
    void UnloadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

private:
    void HandleSystemKeys();
    void SetPaused(bool paused);
    void ApplyMouseMode();
    void ResetToEntrance();
    bool GoToRoom(const std::string& codeOrType);
    void PlaceInRoom(School::RoomId room);
    void UpdateFrameStats(float dt);
    void UpdateInteractables();
    void UpdateRoomBanner(float dt);
    void StepSimulation(float dt);
    void RunScript();
    void SaveScreenshot(const std::string& path);
    void SetTimeScale(float scale);
    std::vector<std::string> StatsLines() const;
    void DrawDebug(const DrawContext& ctx);
    void Announce();

    SchoolOptions options_;
    Microsoft::Xna::Framework::GraphicsDeviceManager graphics_;

    // the school as data (no CNA)
    School::SchoolPlan plan_;
    std::unique_ptr<School::NavGraph> nav_;
    std::unique_ptr<School::NavGraph> liftNav_;
    std::unique_ptr<School::LayoutCache> layouts_;
    std::unique_ptr<School::SchoolSim> sim_;

    // services and GPU-owning subsystems, released in LoadContent's reverse order
    Services services_;
    FrameStats stats_;
    std::unique_ptr<TextureLibrary> textures_;
    std::unique_ptr<EffectBank> effects_;
    std::unique_ptr<Shapes> shapes_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> sprites_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> font_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> titleFont_;
    std::unique_ptr<AudioSystem> audio_;
    std::unique_ptr<SchoolAudio> schoolAudio_;
    TimeOfDay timeOfDay_;
    std::unique_ptr<SkyDome> sky_;
    std::unique_ptr<WorldRenderer> world_;
    std::unique_ptr<NpcRenderer> npcs_;
    std::unique_ptr<Weather> weather_;
    std::unique_ptr<Screens> screens_;
    std::unique_ptr<RoomEffects> roomEffects_;
    std::unique_ptr<CollisionWorld> collision_;
    std::unique_ptr<Gizmos> gizmos_;
    std::unique_ptr<Hud> hud_;
    std::unique_ptr<Minimap> minimap_;
    InteractionSystem interaction_;

    FirstPersonCamera camera_;
    std::unique_ptr<PlayerController> player_;
    InputState input_;
    // The system keys (pause, map, overlays, screenshots, clock, weather, ...) and "use" as cna-engine actions.
    // Walking and looking stay with PlayerController, which reads the devices itself (analog sticks, relative
    // mouse motion).
    CnaEngine::Input::ActionMap controls_;
    struct Actions {
        CnaEngine::Input::ActionId pause, help, stats, fly, debugOverlay, map, minimap, mute, screenshot, fullscreen,
            respawn, mouseFaster, mouseSlower, volumeUp, volumeDown, clockStop, speed1, speed2, speed5, speed10, speed30,
            clockBack, clockForward, nextPhase, bell, drill, announce, weather, quit, resume, use;
    } actions_{};
    void BindControls();
    std::string rendererName_;

    // --hitch-log: where the time of the current frame went (milliseconds), logged when the frame is slow
    using Clock = std::chrono::steady_clock;
    Clock::time_point frameStart_{};
    bool haveFrameStart_ = false;
    double frameGapMs_ = 0.0;
    std::string frameNotes_;
    void NoteFrameTime(const char* what, Clock::time_point since);

    // state
    double time_ = 0.0;            // real seconds since start
    long long frame_ = 0;
    bool paused_ = false;
    bool wasActive_ = true;
    bool showHelp_ = false;
    bool showStats_ = false;
    bool showMinimap_ = true;
    bool bigMap_ = false;
    int debugMode_ = 0;            // 0 off, 1 paths, 2 bounds + nav, 3 collision
    bool timePaused_ = false;
    float timeScale_ = 1.0f;
    float sensitivityScale_ = 1.0f;
    float bellFlash_ = 0.0f;
    School::RoomId currentRoom_ = School::kNone;
    School::RoomId bannerRoom_ = School::kNone;
    float bannerAge_ = 10.0f;
    int lastPhase_ = -1;
    bool lastDrill_ = false;
    int announcements_ = 0;
    float interactScan_ = 1.0f;
    float fpsAccumulator_ = 0.0f;
    int fpsFrames_ = 0;
    std::string focusPrompt_;
    std::vector<std::string> scriptSteps_;
    std::size_t scriptIndex_ = 0;
    int scriptWait_ = 0;
    std::string pendingShot_;
    bool screenshotDone_ = false;
    std::set<std::string> drawErrorsSeen_;
    NpcDrawStats npcStats_;
    std::vector<School::ElevatorStatus> elevators_;
    std::vector<float> roomLight_;   // per room, 0..1, eased towards the wanted state
    std::vector<signed char> lightOverride_;   // per room: 0 automatic, 1 forced on, -1 forced off (the switches by the doors)
    int ridingShaft_ = -1;
    void UpdateElevators();
};
