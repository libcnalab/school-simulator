#include "App/SchoolGame.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <sstream>

#include "App/Log.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Rendering/AssetPaths.hpp"
#include "Rendering/ImageUtil.hpp"
#include "Rendering/RenderStates.hpp"
#include "Rendering/RoomGeometry.hpp"
#include "Simulation/Schedule.hpp"
#include "UI/FontLoader.hpp"
#include "World/StairGeometry.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Input;
using School::Room;
using School::RoomId;
using School::RoomType;

namespace {
/** Resident set size of this process in megabytes (Linux), or -1. */
long ResidentMegabytes()
{
    long pages = -1;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        long size = 0;
        if (std::fscanf(f, "%ld %ld", &size, &pages) != 2) pages = -1;
        std::fclose(f);
    }
    return pages < 0 ? -1 : pages * 4096L / (1024L * 1024L);
}

constexpr float kFov = 70.0f * MathHelper::Pi / 180.0f;
constexpr float kMaxFrameSeconds = 0.1f;
constexpr float kNearPlane = 0.15f;
constexpr float kFarPlane = 340.0f;
constexpr double kDefaultStart = 7 * 3600.0 + 50 * 60.0;

const char* kWeekdays[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};

std::string PhaseLabel(const School::BellSchedule& s, int phase)
{
    const School::Phase& p = s.At(phase);
    if (p.kind == School::PhaseKind::Lesson) return "Lesson " + std::to_string(p.lesson);
    return p.name;
}
}  // namespace

SchoolGame::SchoolGame(const SchoolOptions& options) : options_(options), graphics_(this)
{
    graphics_.setGraphicsProfileProperty(GraphicsProfile::HiDef);
    graphics_.setPreferredBackBufferWidthProperty(options.width);
    graphics_.setPreferredBackBufferHeightProperty(options.height);
    graphics_.setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    graphics_.setIsFullScreenProperty(options.fullscreen);
    graphics_.setSynchronizeWithVerticalRetraceProperty(options.vsync);
    setIsFixedTimeStepProperty(false);
    getWindowProperty().setTitleProperty("CNA School Simulator");
    getWindowProperty().setAllowUserResizingProperty(true);
}

SchoolGame::~SchoolGame() = default;

void SchoolGame::LoadContent()
{
    GraphicsDevice& device = getGraphicsDeviceProperty();
    rendererName_ = std::string(device.GetGraphicsRendererName());
    getWindowProperty().setTitleProperty("CNA School Simulator  -  " + rendererName_);
    services_.game = this;
    services_.device = &device;
    services_.graphics = &graphics_;
    services_.supportsThreeD = device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
    if (!services_.supportsThreeD) {
        School::Log("main", "renderer %s has no 3D pipeline; the school needs a 3D renderer", rendererName_.c_str());
        return;
    }
    services_.contentRoot = AssetPaths::FindContentRoot();
    if (services_.contentRoot.empty()) School::Log("main", "cannot find the Content directory (set SCHOOL_CONTENT); textures, fonts and sounds will be missing");
    else getContentProperty().setRootDirectoryProperty(services_.contentRoot);
    services_.content = &getContentProperty();

    // --- the school as data ---------------------------------------------------------------------------------------
    const auto t0 = std::chrono::steady_clock::now();
    plan_ = School::BuildSchoolPlan(School::PlanOptions{options_.seed});
    for (const std::string& problem : School::ValidatePlan(plan_)) School::Log("plan", "INVALID: %s", problem.c_str());
    nav_ = std::make_unique<School::NavGraph>(plan_);
    liftNav_ = std::make_unique<School::NavGraph>(plan_, true);
    layouts_ = std::make_unique<School::LayoutCache>(plan_);
    School::BellSchedule schedule = School::BellSchedule::Default();
    {
        std::string text;
        const std::string path = !options_.schedulePath.empty() ? options_.schedulePath : services_.contentRoot + "/Data/schedule.cfg";
        if (AssetPaths::ReadTextFile(path, text)) {
            std::vector<std::string> errors;
            School::BellSchedule parsed = School::BellSchedule::Parse(text, errors);
            for (const std::string& e : errors) School::Log("schedule", "%s: %s", path.c_str(), e.c_str());
            const std::vector<std::string> invalid = parsed.Validate();
            for (const std::string& e : invalid) School::Log("schedule", "%s: INVALID: %s", path.c_str(), e.c_str());
            if (invalid.empty() && !parsed.phases.empty()) schedule = std::move(parsed);
            else School::Log("schedule", "using the built-in schedule");
        }
    }
    School::SimSettings settings;
    settings.seed = options_.seed;
    bool presetOk = false;
    settings.preset = School::ParsePreset(options_.preset, &presetOk);
    if (!presetOk) School::Log("main", "unknown preset '%s', using normal", options_.preset.c_str());
    settings.students = options_.students;
    settings.maxFull = options_.maxFull;
    settings.badWeather = options_.badWeather;
    sim_ = std::make_unique<School::SchoolSim>(plan_, *nav_, *layouts_, schedule, settings, liftNav_.get());
    for (const std::string& problem : School::ValidateTimetable(plan_, sim_->Pop())) School::Log("timetable", "INVALID: %s", problem.c_str());
    sim_->FastForwardTo(options_.startTime.value_or(kDefaultStart));
    (void)sim_->TakeBells();   // bells rung while fast-forwarding are history, not news
    timeScale_ = std::max(0.0f, options_.timeScale);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    School::Log("main", "school built: %zu rooms, %zu doors, %zu stairs, %zu elevators, %zu people, clock %s (%.0f ms)", plan_.rooms.size(), plan_.doors.size(), plan_.stairs.size(),
                plan_.elevators.size(), sim_->Pop().npcs.size(), School::ClockText(sim_->Time()).c_str(), ms);
    services_.plan = &plan_;
    services_.nav = nav_.get();
    services_.layouts = layouts_.get();
    services_.sim = sim_.get();

    // --- rendering, audio, UI --------------------------------------------------------------------------------------
    textures_ = std::make_unique<TextureLibrary>(device, getContentProperty());
    effects_ = std::make_unique<EffectBank>(device, textures_->White());
    shapes_ = std::make_unique<Shapes>(device);
    sprites_ = std::make_unique<SpriteBatch>(device);
    font_ = FontLoader::Load(getContentProperty(), services_.contentRoot, "ui");
    titleFont_ = FontLoader::Load(getContentProperty(), services_.contentRoot, "title");
    if (!titleFont_ && font_) titleFont_ = FontLoader::Load(getContentProperty(), services_.contentRoot, "ui");
    audio_ = std::make_unique<AudioSystem>(getContentProperty(), options_.audio, options_.volume);
    services_.textures = textures_.get();
    services_.effects = effects_.get();
    services_.shapes = shapes_.get();
    services_.sprites = sprites_.get();
    services_.font = font_.get();
    services_.titleFont = titleFont_.get();
    services_.audio = audio_.get();
    services_.stats = &stats_;
    services_.mutableStats = &stats_;
    services_.whiteTexture = textures_->White();
    sky_ = std::make_unique<SkyDome>(services_, timeOfDay_);
    services_.sky = sky_.get();
    services_.timeOfDay = &timeOfDay_;
    world_ = std::make_unique<WorldRenderer>(services_, plan_, *layouts_);
    npcs_ = std::make_unique<NpcRenderer>(services_);
    weather_ = std::make_unique<Weather>(services_);
    weather_->SetKind(options_.weather);
    screens_ = std::make_unique<Screens>(services_, plan_, *layouts_);
    roomEffects_ = std::make_unique<RoomEffects>(services_, plan_, *layouts_);
    collision_ = std::make_unique<CollisionWorld>(plan_, *layouts_);
    gizmos_ = std::make_unique<Gizmos>(services_);
    hud_ = std::make_unique<Hud>(services_);
    minimap_ = std::make_unique<Minimap>(services_, *hud_);
    schoolAudio_ = std::make_unique<SchoolAudio>(*audio_, plan_);
    schoolAudio_->SetWeather(options_.weather);

    PlayerController::Settings ps;
    ps.mouseSensitivity = options_.mouseSensitivity;
    player_ = std::make_unique<PlayerController>(ps, plan_);
    player_->onFootstep = [this](bool running) {
        bool hard = true;
        if (currentRoom_ != School::kNone) {
            const std::string tex = FloorTextureOf(plan_, plan_.room(currentRoom_));
            hard = tex != "floor_carpet" && tex != "floor_grass" && tex != "pitch" && tex != "floor_sand";
        }
        schoolAudio_->Footstep(running, hard);
    };

    ResetToEntrance();
    if (options_.startRoom && !GoToRoom(*options_.startRoom)) School::Log("main", "unknown room '%s'", options_.startRoom->c_str());
    if (options_.startPosition) player_->Place(*options_.startPosition, camera_.Yaw(), 0.0f, camera_);
    if (options_.startYawDegrees || options_.startPitchDegrees)
        camera_.SetPose(camera_.Position(), MathHelper::ToRadians(options_.startYawDegrees.value_or(MathHelper::ToDegrees(camera_.Yaw()))),
                        MathHelper::ToRadians(options_.startPitchDegrees.value_or(0.0f)));
    lastPhase_ = sim_->PhaseIndex();
    SetPaused(false);
    hud_->Toast("Welcome to the school. F1 shows the controls.", 5.0f);
    if (!options_.script.empty()) {
        std::stringstream ss(options_.script);
        std::string step;
        while (std::getline(ss, step, ';')) scriptSteps_.push_back(step);
    }
}

void SchoolGame::UnloadContent()
{
    if (player_ && player_->MouseCaptured()) player_->SetMouseCaptured(false, 0, 0);
    minimap_.reset();
    hud_.reset();
    gizmos_.reset();
    roomEffects_.reset();
    screens_.reset();
    weather_.reset();
    npcs_.reset();
    world_.reset();
    sky_.reset();
    schoolAudio_.reset();
    audio_.reset();
    titleFont_.reset();
    font_.reset();
    sprites_.reset();
    shapes_.reset();
    effects_.reset();
    textures_.reset();
}

// --- placing the player ----------------------------------------------------------------------------------------------------

void SchoolGame::ResetToEntrance()
{
    // On the front plaza, facing the main entrance.
    const Room& lobby = plan_.room(plan_.mainLobby);
    player_->Place(Vector3(lobby.rect.Center().x, 0.0f, lobby.rect.z1 + 9.0f), 0.0f, 0.0f, camera_);
    if (world_) world_->Ensure(plan_.RoomAt(School::Vec2{lobby.rect.Center().x, lobby.rect.z1 + 9.0f}, 1));
}

void SchoolGame::PlaceInRoom(RoomId id)
{
    const Room& r = plan_.room(id);
    const School::RoomLayout& l = layouts_->Get(id);
    School::Vec2 at = l.aisles.valid ? l.hub : r.rect.Center();
    // A couple of metres into the room, so the first view shows the room and not the door wall behind.
    at = at + School::SideNormal(l.doorSide) * -std::min(2.5f, 0.2f * std::min(r.rect.Width(), r.rect.Depth()));
    at = r.rect.Clamp(at, 0.8f);
    // face away from the door wall; in a corridor, along it
    School::Vec2 away = School::SideNormal(l.doorSide) * -1.0f;
    if (r.rect.Width() > 2.0f * r.rect.Depth()) away = School::Vec2{1.0f, 0.0f};
    else if (r.rect.Depth() > 2.0f * r.rect.Width()) away = School::Vec2{0.0f, 1.0f};
    const float yaw = std::atan2(-away.x, -away.z);
    float y = plan_.FloorY(r.level);
    if (r.type == RoomType::StairWell) {
        const School::StairShaft& s = plan_.stairs[static_cast<std::size_t>(r.stairId)];
        const School::StairFrame f{s.footprint, s.entrySide};
        at = f.Point(f.W() * 0.5f, s.entryDepth * 0.5f);
    }
    player_->Place(Vector3(at.x, y, at.z), yaw, 0.0f, camera_);
    School::Log("script", "placed in %s %s: room x %.1f..%.1f z %.1f..%.1f, feet (%.1f, %.1f, %.1f)", r.code.c_str(), r.name.c_str(), r.rect.x0, r.rect.x1, r.rect.z0, r.rect.z1, at.x, y, at.z);
    world_->Ensure(id);
    currentRoom_ = School::kNone;   // so the banner announces the new room
    bannerRoom_ = School::kNone;
}

bool SchoolGame::GoToRoom(const std::string& key)
{
    for (const Room& r : plan_.rooms)
        if (r.code == key) {
            PlaceInRoom(r.id);
            return true;
        }
    // a type name ("Gymnasium", "gym", "computer lab", "ComputerLab"): the largest room of that type
    const auto fold = [](std::string t) {
        std::string out;
        for (const char c : t)
            if (c != ' ' && c != '\'') out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    };
    const std::string want = fold(key);
    RoomId best = School::kNone;
    for (int pass = 0; pass < 3 && best == School::kNone; ++pass)
        for (const Room& r : plan_.rooms) {
            const std::string tn = fold(School::TypeName(r.type));
            const bool hit = pass == 0 ? tn == want : pass == 1 ? fold(r.name).rfind(want, 0) == 0 : tn.rfind(want, 0) == 0;
            if (hit && (best == School::kNone || r.rect.Area() > plan_.room(best).rect.Area())) best = r.id;
        }
    if (best == School::kNone) return false;
    PlaceInRoom(best);
    return true;
}

// --- input ---------------------------------------------------------------------------------------------------------------------

void SchoolGame::SetPaused(bool paused)
{
    paused_ = paused;
    ApplyMouseMode();
}

void SchoolGame::ApplyMouseMode()
{
    const bool menu = paused_ || bigMap_;
    setIsMouseVisibleProperty(menu);
    if (player_) {
        const auto& vp = getGraphicsDeviceProperty().getViewportProperty();
        player_->SetMouseCaptured(!menu, vp.getWidthProperty(), vp.getHeightProperty());
    }
}

void SchoolGame::SetTimeScale(float scale)
{
    timeScale_ = scale;
    timePaused_ = scale <= 0.0f;
    char buf[48];
    if (timePaused_) std::snprintf(buf, sizeof buf, "School clock stopped");
    else std::snprintf(buf, sizeof buf, "Time x%g", static_cast<double>(scale));
    hud_->Toast(buf, 1.5f);
}

void SchoolGame::Announce()
{
    ++announcements_;
    schoolAudio_->Announce(announcements_);
    static const char* texts[] = {"PA: Would the owner of a blue bicycle please come to reception.", "PA: Reminder, the library closes at half past five today.",
                                  "PA: Year 9 football practice moves to the playground this afternoon.", "PA: The drama club meets in the auditorium after school."};
    hud_->Toast(texts[announcements_ % 4], 5.0f);
}

void SchoolGame::HandleSystemKeys()
{
    if (input_.Pressed(Keys::Escape) || input_.PadPressed(&GamePadButtons::getStartProperty)) {
        if (bigMap_) {
            bigMap_ = false;
            ApplyMouseMode();
        } else {
            SetPaused(!paused_);
        }
    }
    if (input_.Pressed(Keys::F1) || input_.Pressed(Keys::H)) showHelp_ = !showHelp_;
    if (input_.Pressed(Keys::F3)) showStats_ = !showStats_;
    if (input_.Pressed(Keys::F6)) {
        player_->SetFlying(!player_->Flying());
        if (!player_->Flying()) {
            // Back on the ground: drop to the floor under the camera.
            const Vector3 f = player_->Feet();
            const float ground = plan_.GroundHeight(School::Vec2{f.X, f.Z}, f.Y);
            player_->Place(Vector3(f.X, ground, f.Z), camera_.Yaw(), camera_.Pitch(), camera_);
        }
        hud_->Toast(player_->Flying() ? "Free flight (debug): W flies where you look" : "Back on the ground", 2.0f);
    }
    if (input_.Pressed(Keys::F4)) {
        debugMode_ = (debugMode_ + 1) % 4;
        static const char* names[] = {"Debug overlays off", "Debug: NPC paths and fidelity", "Debug: room bounds and navigation nodes", "Debug: collision boxes"};
        hud_->Toast(names[debugMode_], 1.5f);
    }
    if (input_.Pressed(Keys::Tab) || input_.PadPressed(&GamePadButtons::getYProperty)) {
        bigMap_ = !bigMap_;
        ApplyMouseMode();
    }
    if (input_.Pressed(Keys::M)) showMinimap_ = !showMinimap_;
    if (input_.Pressed(Keys::F2)) audio_->SetMuted(!audio_->Muted());
    if (input_.Pressed(Keys::F12)) {
        static int shotIndex = 0;
        char name[64];
        std::snprintf(name, sizeof name, "school-%s-%03d.png", School::ClockText(sim_->Time()).c_str(), ++shotIndex);
        for (char& ch : name)
            if (ch == ':') ch = '.';
        pendingShot_ = name;
        hud_->Toast(std::string("Screenshot ") + name, 2.0f);
    }
    if (input_.Pressed(Keys::F11)) {
        graphics_.ToggleFullScreen();
        const auto& vp = getGraphicsDeviceProperty().getViewportProperty();
        if (player_->MouseCaptured()) {
            player_->SetMouseCaptured(false, 0, 0);
            player_->SetMouseCaptured(true, vp.getWidthProperty(), vp.getHeightProperty());
        }
    }
    if (input_.Pressed(Keys::R)) {
        ResetToEntrance();
        hud_->Toast("Back at the entrance");
        if (paused_) SetPaused(false);
    }
    if (input_.Pressed(Keys::OemCloseBrackets)) sensitivityScale_ = std::min(4.0f, sensitivityScale_ * 1.15f);
    if (input_.Pressed(Keys::OemOpenBrackets)) sensitivityScale_ = std::max(0.2f, sensitivityScale_ / 1.15f);
    if (input_.Pressed(Keys::OemPlus) || input_.Pressed(Keys::Add)) audio_->SetMasterVolume(audio_->MasterVolume() + 0.1f);
    if (input_.Pressed(Keys::OemMinus) || input_.Pressed(Keys::Subtract)) audio_->SetMasterVolume(audio_->MasterVolume() - 0.1f);
    player_->GetSettings().mouseSensitivity = options_.mouseSensitivity * sensitivityScale_;

    if (!paused_) {
        if (input_.Pressed(Keys::Space)) SetTimeScale(timePaused_ ? std::max(1.0f, timeScale_) : 0.0f);
        if (input_.Pressed(Keys::D1)) SetTimeScale(1.0f);
        if (input_.Pressed(Keys::D2)) SetTimeScale(2.0f);
        if (input_.Pressed(Keys::D3)) SetTimeScale(5.0f);
        if (input_.Pressed(Keys::D4)) SetTimeScale(10.0f);
        if (input_.Pressed(Keys::D5)) SetTimeScale(30.0f);
        if (input_.Pressed(Keys::OemComma) || input_.Pressed(Keys::OemPeriod)) {
            const double delta = input_.Pressed(Keys::OemPeriod) ? 900.0 : -900.0;
            sim_->JumpToTime(std::clamp(sim_->Time() + delta, 0.0, 86399.0));
            (void)sim_->TakeBells();
            lastPhase_ = sim_->PhaseIndex();
            hud_->Toast("Clock set to " + School::ClockText(sim_->Time()), 1.5f);
        }
        if (input_.Pressed(Keys::N)) {
            sim_->SkipToNextPhase(true);
            hud_->Toast("Skipped to " + PhaseLabel(sim_->Schedule(), sim_->PhaseIndex()) + " at " + School::ClockText(sim_->Time()), 2.5f);
        }
        if (input_.Pressed(Keys::B)) {
            schoolAudio_->Bell(false);
            bellFlash_ = 1.0f;
        }
        if (input_.Pressed(Keys::F)) {
            if (sim_->Drill().active) {
                sim_->EndDrill();
                hud_->Toast("Fire drill ended: everybody returns to the timetable", 3.0f);
            } else {
                sim_->StartDrill();
                hud_->Toast("FIRE DRILL: the alarm sounds, everybody walks to the assembly points", 4.0f);
            }
        }
        if (input_.Pressed(Keys::P)) Announce();
        if (input_.Pressed(Keys::O)) {
            static const char* kinds[] = {"clear", "rain", "snow", "fog"};
            int k = 0;
            for (int i = 0; i < 4; ++i)
                if (options_.weather == kinds[i]) k = i;
            options_.weather = kinds[(k + 1) % 4];
            weather_->SetKind(options_.weather);
            schoolAudio_->SetWeather(options_.weather);
            hud_->Toast(std::string("Weather: ") + options_.weather, 1.5f);
        }
    }
    if (paused_) {
        if (input_.Pressed(Keys::Q)) Exit();
        if (input_.LeftClicked()) SetPaused(false);
    }
}

// --- per frame -------------------------------------------------------------------------------------------------------------------

void SchoolGame::UpdateFrameStats(float dt)
{
    fpsAccumulator_ += dt;
    ++fpsFrames_;
    if (fpsAccumulator_ >= 0.5f) {
        stats_.fps = static_cast<float>(fpsFrames_) / fpsAccumulator_;
        stats_.frameMilliseconds = fpsAccumulator_ / static_cast<float>(fpsFrames_) * 1000.0f;
        fpsAccumulator_ = 0.0f;
        fpsFrames_ = 0;
    }
    const auto& vp = getGraphicsDeviceProperty().getViewportProperty();
    stats_.backBufferWidth = vp.getWidthProperty();
    stats_.backBufferHeight = vp.getHeightProperty();
    stats_.fullscreen = graphics_.getIsFullScreenProperty();
    stats_.frame = frame_;
    const WorldStats& w = world_->Stats();
    stats_.roomsLoaded = w.roomsLoaded;
    stats_.roomsDrawn = w.roomsDrawn;
    stats_.roomsCulled = w.roomsCulled;
    stats_.roomsBuiltTotal = w.roomsBuiltTotal;
    stats_.trianglesDrawn = w.trianglesDrawn;
    stats_.drawCalls = w.drawCalls;
    stats_.doorsDrawn = w.doorsDrawn;
    stats_.buildMilliseconds = w.buildMilliseconds;
    const School::SimStats& ss = sim_->Stats();
    stats_.npcTotal = static_cast<int>(sim_->Pop().npcs.size());
    stats_.npcFull = static_cast<int>(sim_->FullAgents().size());
    stats_.npcOnCampus = npcStats_.onCampus;
    stats_.npcWalking = npcStats_.walking;
    stats_.npcDrawn = npcStats_.drawn;
    stats_.npcDrawnDetailed = npcStats_.detailed;
    stats_.activeVoices = audio_->ActiveVoiceCount();
    stats_.particles = weather_->Count();
    stats_.screensRendered = screens_->RenderedThisFrame();
    stats_.textures = static_cast<int>(textures_->Count());
    (void)ss;
}

void SchoolGame::StepSimulation(float dt)
{
    const auto t0 = std::chrono::steady_clock::now();
    const Vector3 feet = player_->Feet();
    School::Focus f;
    f.pos = School::Vec2{feet.X, feet.Z};
    f.level = player_->Level();
    f.radius = sim_->Settings().fullRadius;
    std::vector<School::Focus> focus = {f};
    if (screens_)
        for (const WorldRenderer::Anchor& a : screens_->Anchors()) focus.push_back(School::Focus{School::Vec2{a.position.X, a.position.Z}, a.level, a.radius});
    sim_->SetFocus(focus);
    if (!timePaused_ && timeScale_ > 0.0f) {
        // Large multipliers are stepped in slices so the full-fidelity walkers stay stable.
        double remaining = static_cast<double>(dt) * static_cast<double>(timeScale_);
        remaining = std::min(remaining, 4.0);
        sim_->Advance(remaining);
    }
    stats_.simMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    timeOfDay_.SetHours(static_cast<float>(sim_->Time() / 3600.0));
    // Bells, alarms and phase changes.
    for (const School::BellEvent& b : sim_->TakeBells()) {
        schoolAudio_->Bell(b.alarm);
        bellFlash_ = 1.0f;
        if (b.alarm) hud_->Toast("Alarm: fire drill, leave the building by the nearest exit", 4.0f);
        else hud_->Toast("The bell rings: " + PhaseLabel(sim_->Schedule(), b.phase), 3.0f);
    }
    const int phase = sim_->PhaseIndex();
    if (phase != lastPhase_) {
        lastPhase_ = phase;
        const School::Phase& p = sim_->Schedule().At(phase);
        if (p.kind == School::PhaseKind::Lunch || p.kind == School::PhaseKind::Dismissal || p.kind == School::PhaseKind::Clubs) Announce();
    }
    const bool drill = sim_->Drill().active;
    if (lastDrill_ && !drill) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "Fire drill over: everybody assembled in %.0f s", sim_->Drill().lastDuration);
        hud_->Toast(buf, 4.0f);
    }
    lastDrill_ = drill;
    bellFlash_ = std::max(0.0f, bellFlash_ - dt * 1.5f);
}

void SchoolGame::UpdateInteractables()
{
    interaction_.Clear();
    const Vector3 feet = player_->Feet();
    const int level = player_->Level();
    const School::Vec2 p{feet.X, feet.Z};
    // Doors within reach: hold them open.
    for (const RoomId id : collision_->RoomsAround(p, level, 8.0f)) {
        const Room& r = plan_.room(id);
        for (const School::DoorId did : r.doors) {
            const School::Door& d = plan_.door(did);
            if (!d.Leaf() || d.kind == School::DoorKind::ElevatorDoor || School::DistanceSq(d.pos, p) > 8.0f * 8.0f) continue;
            const float y0 = plan_.FloorY(d.level);
            const bool alongX = d.sideOfA == School::Side::North || d.sideOfA == School::Side::South;
            const float hw = d.width * 0.5f;
            Interactable it;
            it.box = BoundingBox(Vector3(d.pos.x - (alongX ? hw : 0.25f), y0, d.pos.z - (alongX ? 0.25f : hw)), Vector3(d.pos.x + (alongX ? hw : 0.25f), y0 + School::kDoorHeight, d.pos.z + (alongX ? 0.25f : hw)));
            const float open = sim_->Doors()[static_cast<std::size_t>(did)].open;
            it.prompt = open > 0.5f ? "Hold the door open" : (d.kind == School::DoorKind::Automatic ? "Automatic door" : "Open the door");
            it.tag = did;
            it.activate = [this, did] { sim_->RequestDoor(did, 8.0f); };
            interaction_.Add(std::move(it));
        }
        if (id != currentRoom_) continue;
        // Things inside the player's room.
        const float y0 = plan_.FloorY(r.level);
        const School::RoomLayout& l = layouts_->Get(id);
        // Light switches beside the doors (see RoomGeometry: hinge side, 1.15 m up, 0.25 m past the opening).
        if (!School::Info(r.type).circulation)
            for (const School::DoorId did : r.doors) {
                const School::Door& d = plan_.door(did);
                if (d.kind == School::DoorKind::Opening) continue;
                School::Vec2 n = d.Normal();
                if (d.a == id) n = n * -1.0f;   // into this room
                const School::Vec2 along = n.RightHand() * -1.0f;   // the wall's u direction seen from inside runs the other way
                const School::Vec2 at = d.pos + n * 0.03f + along * (d.width * 0.5f + 0.25f);
                if (School::DistanceSq(at, p) > 5.0f * 5.0f) continue;
                Interactable it;
                it.box = BoundingBox(Vector3(at.x - 0.08f, y0 + 1.07f, at.z - 0.08f), Vector3(at.x + 0.08f, y0 + 1.23f, at.z + 0.08f));
                if (lightOverride_.size() != plan_.rooms.size()) lightOverride_.assign(plan_.rooms.size(), 0);
                const signed char ov = lightOverride_[static_cast<std::size_t>(id)];
                it.prompt = ov < 0 ? "Lights: switch on" : ov > 0 ? "Lights: back to automatic" : "Lights: switch off";
                it.activate = [this, id] {
                    signed char& o = lightOverride_[static_cast<std::size_t>(id)];
                    o = o < 0 ? 1 : o > 0 ? 0 : -1;
                };
                interaction_.Add(std::move(it));
            }
        for (const School::Fixture& f : l.fixtures) {
            if (School::DistanceSq(f.pos, p) > 6.0f * 6.0f) continue;
            Interactable it;
            it.box = CollisionWorld::FixtureBounds(f, y0);
            if (f.type == School::FixtureType::Whiteboard || f.type == School::FixtureType::Screen) {
                const RoomId room = id;
                it.prompt = r.type == RoomType::SecurityOffice ? "Switch the CCTV cameras" : "Read the board";
                if (r.type == RoomType::SecurityOffice) {
                    it.activate = [this] { screens_->CycleCameras(); };
                    interaction_.Add(std::move(it));
                    continue;
                }
                it.activate = [this, room] {
                    const School::SchoolSim::RoomLesson lesson = sim_->LessonIn(room);
                    const Room& rr = plan_.room(room);
                    if (lesson.valid) {
                        const School::ClassGroup& g = sim_->Pop().classes[static_cast<std::size_t>(lesson.classId)];
                        std::string who = lesson.teacher >= 0 ? sim_->Pop().npcs[static_cast<std::size_t>(lesson.teacher)].name : "self-study";
                        hud_->Toast(rr.name + ": " + School::SubjectName(lesson.subject) + " with class " + g.name + " (" + who + ")", 4.0f);
                    } else {
                        hud_->Toast(rr.name + ": nothing is scheduled here right now", 3.0f);
                    }
                };
                interaction_.Add(std::move(it));
            } else if (f.type == School::FixtureType::Counter && (r.type == RoomType::Reception || r.type == RoomType::Lobby)) {
                it.prompt = "Make an announcement (PA)";
                it.activate = [this] { Announce(); };
                interaction_.Add(std::move(it));
            } else if (f.type == School::FixtureType::ComputerDesk && r.type == RoomType::SecurityOffice) {
                it.prompt = "Switch the CCTV cameras";
                it.activate = [this] {
                    screens_->CycleCameras();
                    const auto& cams = screens_->Cameras();
                    if (!cams.empty()) hud_->Toast("CCTV: " + cams[0].name + " and the next three", 2.0f);
                };
                interaction_.Add(std::move(it));
            } else if (f.type == School::FixtureType::Vending && r.type == RoomType::Lobby) {
                it.prompt = "Trophy cabinet";
                it.activate = [this] { hud_->Toast("Regional champions in basketball, chess and debating, and a very old football.", 4.0f); };
                interaction_.Add(std::move(it));
            } else if (f.type == School::FixtureType::Vending) {
                it.prompt = "Buy a snack";
                it.activate = [this] {
                    schoolAudio_->Click();
                    hud_->Toast("Clunk. A snack bar drops into the tray.", 2.5f);
                };
                interaction_.Add(std::move(it));
            } else if (f.type == School::FixtureType::Piano) {
                it.prompt = "Play a note";
                it.activate = [this] { audio_->PlayUi("music", 0.6f, static_cast<float>((frame_ % 7) - 3) * 0.15f); };
                interaction_.Add(std::move(it));
            } else if (f.type == School::FixtureType::Scoreboard) {
                it.prompt = "Scoreboard";
                it.activate = [this] {
                    audio_->PlayUi("cheer", 0.6f);
                    hud_->Toast("The crowd cheers!", 2.0f);
                };
                interaction_.Add(std::move(it));
            }
        }
    }
    // People within a few metres: look at somebody to learn who they are and what they are doing.
    {
        const School::Population& pop = sim_->Pop();
        int added = 0;
        for (const School::Npc& n : pop.npcs) {
            const School::Agent& a = sim_->AgentOf(n.id);
            if (a.mode == School::Mode::Offsite) continue;
            School::Vec3 pos;
            float yaw;
            RoomId room;
            sim_->Observe(n.id, pos, yaw, room);
            if (std::fabs(pos.y - feet.Y) > 2.5f || School::DistanceSq(School::Vec2{pos.x, pos.z}, p) > 8.0f * 8.0f) continue;
            Interactable it;
            it.box = BoundingBox(Vector3(pos.x - 0.25f, pos.y + 0.2f, pos.z - 0.25f), Vector3(pos.x + 0.25f, pos.y + 1.75f, pos.z + 0.25f));
            std::string who = n.name + ", " + School::RoleName(n.role);
            if (n.role == School::Role::Student && n.classId >= 0) who += " " + pop.classes[static_cast<std::size_t>(n.classId)].name;
            else if (n.role == School::Role::Teacher) who += std::string(" of ") + School::SubjectName(n.subject);
            std::string doing = School::ActivityName(a.activity);
            if (a.mode == School::Mode::Transit && a.goal.room != School::kNone) doing += " to " + plan_.room(a.goal.room).name;
            it.prompt = who + ": " + doing;
            const int id = n.id;
            it.activate = [this, id] {
                const School::Npc& npc = sim_->Pop().npcs[static_cast<std::size_t>(id)];
                const School::Agent& ag = sim_->AgentOf(id);
                std::string text = npc.name + " (" + School::RoleName(npc.role) + ")";
                if (npc.role == School::Role::Student && npc.classId >= 0) {
                    const School::ClassGroup& g = sim_->Pop().classes[static_cast<std::size_t>(npc.classId)];
                    text += ", class " + g.name + ", home room " + plan_.room(g.home).code;
                    const School::Phase& ph = sim_->Schedule().At(sim_->PhaseIndex());
                    const int period = ph.lesson > 0 ? ph.lesson : 1;
                    if (period <= sim_->Pop().periods) {
                        const School::Lesson& l = g.timetable[static_cast<std::size_t>(sim_->Weekday())][static_cast<std::size_t>(period - 1)];
                        text += std::string("; lesson ") + std::to_string(period) + ": " + (l.free ? "self-study" : School::SubjectName(l.subject)) + " in " + plan_.room(l.free ? g.home : l.room).code;
                    }
                } else if (npc.base != School::kNone) {
                    text += ", works in " + plan_.room(npc.base).name;
                }
                if (npc.usesElevator) text += "; takes the lift";
                text += "; now " + std::string(School::ActivityName(ag.activity));
                hud_->Toast(text, 5.0f);
            };
            interaction_.Add(std::move(it));
            if (++added >= 40) break;
        }
    }
    // Elevators: a call button beside every cab door on the corridor side, up and down buttons inside the cab.
    for (int i = 0; i < sim_->ElevatorCount(); ++i) {
        const School::ElevatorShaft& e = plan_.elevators[static_cast<std::size_t>(i)];
        const School::Vec2 c = e.footprint.Center();
        if (School::DistanceSq(c, p) > 10.0f * 10.0f) continue;
        for (std::size_t k = 0; k < e.cab.size(); ++k) {
            const int lvl = e.levelLow + static_cast<int>(k);
            if (std::abs(lvl - level) > 1) continue;
            const float y0 = plan_.FloorY(lvl);
            for (const School::DoorId did : plan_.room(e.cab[k]).doors) {
                const School::Door& d = plan_.door(did);
                const School::Vec2 n = School::SideNormal(e.entrySide);   // out of the cab into the corridor
                const School::Vec2 at = d.pos + n * 0.08f + n.RightHand() * (d.width * 0.5f + 0.35f);
                Interactable it;
                it.box = BoundingBox(Vector3(at.x - 0.1f, y0 + 1.0f, at.z - 0.1f), Vector3(at.x + 0.1f, y0 + 1.25f, at.z + 0.1f));
                const School::ElevatorStatus st = sim_->Elevator(i);
                it.prompt = st.level == lvl && st.doorOpen > 0.5f ? "Elevator is here" : "Call the elevator";
                it.activate = [this, i, lvl] { sim_->CallElevator(i, lvl); };
                interaction_.Add(std::move(it));
            }
        }
        // the cab's own panel: one button up, one down, at the back wall of the cab wherever it is
        const School::ElevatorStatus st = sim_->Elevator(i);
        const School::Vec2 n = School::SideNormal(e.entrySide);
        const School::Vec2 back = c - n * (std::max(e.footprint.Width(), e.footprint.Depth()) * 0.5f - 0.2f);
        const School::Vec2 side = back + n.RightHand() * -0.9f;
        for (int dir = -1; dir <= 1; dir += 2) {
            const int target = st.level + dir;
            if (target < e.levelLow || target > e.levelHigh) continue;
            Interactable it;
            const float y = st.y + (dir > 0 ? 1.35f : 1.1f);
            it.box = BoundingBox(Vector3(side.x - 0.12f, y - 0.1f, side.z - 0.12f), Vector3(side.x + 0.12f, y + 0.1f, side.z + 0.12f));
            it.prompt = dir > 0 ? "Elevator: up" : "Elevator: down";
            it.activate = [this, i, target] { sim_->CallElevator(i, target); };
            interaction_.Add(std::move(it));
        }
    }
    // Fire alarm points: beside every exit of the lobby.
    if (currentRoom_ == plan_.mainLobby) {
        const Room& lobby = plan_.room(plan_.mainLobby);
        for (const School::DoorId did : lobby.doors) {
            const School::Door& d = plan_.door(did);
            if (!d.exit) continue;
            const School::Vec2 at = d.pos + d.Normal().RightHand() * (d.width * 0.5f + 0.4f) - d.Normal() * 0.1f;
            Interactable it;
            it.box = BoundingBox(Vector3(at.x - 0.12f, 1.2f, at.z - 0.12f), Vector3(at.x + 0.12f, 1.45f, at.z + 0.12f));
            it.prompt = sim_->Drill().active ? "Fire alarm (reset)" : "Fire alarm";
            it.activate = [this] {
                if (sim_->Drill().active) sim_->EndDrill();
                else sim_->StartDrill();
            };
            interaction_.Add(std::move(it));
        }
    }
}

void SchoolGame::UpdateElevators()
{
    // Statuses for rendering, closed cab doors as collision, and the player's own ride.
    elevators_.clear();
    std::vector<BoundingBox> blockers;
    const Vector3 feet = player_->Feet();
    ridingShaft_ = -1;
    for (int i = 0; i < sim_->ElevatorCount(); ++i) {
        const School::ElevatorStatus st = sim_->Elevator(i);
        elevators_.push_back(st);
        const School::ElevatorShaft& e = plan_.elevators[static_cast<std::size_t>(i)];
        for (std::size_t k = 0; k < e.cab.size(); ++k) {
            const int level = e.levelLow + static_cast<int>(k);
            const bool open = st.level == level && !st.moving && st.doorOpen > 0.6f;
            if (open) continue;
            for (const School::DoorId did : plan_.room(e.cab[k]).doors) {
                const School::Door& d = plan_.door(did);
                const float y0 = plan_.FloorY(level);
                const bool alongX = d.sideOfA == School::Side::North || d.sideOfA == School::Side::South;
                const float hw = d.width * 0.5f + 0.1f;
                blockers.emplace_back(Vector3(d.pos.x - (alongX ? hw : 0.08f), y0 - 0.5f, d.pos.z - (alongX ? 0.08f : hw)), Vector3(d.pos.x + (alongX ? hw : 0.08f), y0 + School::kDoorHeight, d.pos.z + (alongX ? 0.08f : hw)));
            }
        }
        // Standing in the shaft's footprint within reach of the cab floor: ride.
        if (e.footprint.Contains(School::Vec2{feet.X, feet.Z}, 0.05f) && std::fabs(st.y - feet.Y) < 0.7f) ridingShaft_ = i;
    }
    collision_->SetDynamicBoxes(std::move(blockers));
    player_->SetRide(ridingShaft_ >= 0, ridingShaft_ >= 0 ? elevators_[static_cast<std::size_t>(ridingShaft_)].y : 0.0f);
}

void SchoolGame::UpdateRoomBanner(float dt)
{
    const Vector3 feet = player_->Feet();
    RoomId room = plan_.RoomAt(School::Vec2{feet.X, feet.Z}, player_->Level(), 0.05f);
    if (room == School::kNone) room = currentRoom_;
    if (room != currentRoom_) {
        currentRoom_ = room;
        if (room != School::kNone) {
            const Room& r = plan_.room(room);
            // Corridors of the same wing do not announce themselves again and again.
            const bool corridor = r.type == RoomType::Corridor;
            const bool sameWingCorridor = corridor && bannerRoom_ != School::kNone && plan_.room(bannerRoom_).type == RoomType::Corridor && plan_.room(bannerRoom_).wing == r.wing &&
                                          plan_.room(bannerRoom_).level == r.level;
            if (!sameWingCorridor) {
                bannerAge_ = 0.0f;
                bannerRoom_ = room;
            }
        }
    }
    bannerAge_ += dt;
}

void SchoolGame::Update(GameTime& gameTime)
{
    if (!world_) {
        if (++frame_ > 2) Exit();
        return;
    }
    Game::Update(gameTime);
    float dt = static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalMillisecondsProperty()) / 1000.0f;
    dt = std::clamp(dt, 0.0f, kMaxFrameSeconds);
    if (options_.fixedTimeStep > 0.0f) dt = options_.fixedTimeStep;
    ++frame_;
    input_.Update();
    UpdateFrameStats(dt);
    HandleSystemKeys();
    const bool active = getIsActiveProperty();
    if (wasActive_ && !active && !paused_ && options_.maxFrames == 0 && options_.script.empty()) SetPaused(true);
    wasActive_ = active;
    RunScript();

    if (!paused_) {
        time_ += dt;
        UpdateElevators();
        if (!bigMap_) player_->Update(dt, camera_, *collision_, input_, true);
        UpdateRoomBanner(dt);
        StepSimulation(dt);
        screens_->Update(dt, camera_.Position(), *sim_);
        world_->Update(camera_.Position(), player_->Level(), dt, screens_->Anchors());
        {
            const bool out = currentRoom_ == School::kNone || !plan_.room(currentRoom_).Indoor();
            weather_->Update(dt, camera_.Position(), out, player_->Feet().Y);
        }
        roomEffects_->Update(dt, camera_.Position(), *world_);
        // Doors open for the player as they do for everybody else.
        const Vector3 feet = player_->Feet();
        if (currentRoom_ != School::kNone)
            for (const School::DoorId did : plan_.room(currentRoom_).doors) {
                const School::Door& d = plan_.door(did);
                if (d.Leaf() && d.kind != School::DoorKind::ElevatorDoor && School::DistanceSq(d.pos, School::Vec2{feet.X, feet.Z}) < 2.0f * 2.0f) sim_->RequestDoor(did, 1.5f);
            }
        interactScan_ += dt;
        if (interactScan_ >= 0.25f) {
            interactScan_ = 0.0f;
            UpdateInteractables();
        }
        const std::optional<float> wall = collision_->Raycast(camera_.ViewRay(), 4.0f, player_->Level());
        interaction_.Focus(camera_.ViewRay(), 6.0f, wall.value_or(1e9f));
        focusPrompt_ = interaction_.FocusedPrompt();
        if (!bigMap_ && (input_.Pressed(Keys::E) || input_.LeftClicked() || input_.PadPressed(&GamePadButtons::getAProperty))) {
            if (interaction_.Activate()) {
                schoolAudio_->Click();
                UpdateInteractables();
            }
        }
        const bool outdoors = currentRoom_ == School::kNone || !plan_.room(currentRoom_).Indoor();
        schoolAudio_->Update(dt, camera_.Position(), camera_.Forward(), *sim_, *world_, currentRoom_, outdoors);
    }
    audio_->Update();
    hud_->Update(dt);
    if (options_.maxFrames > 0 && frame_ >= options_.maxFrames) Exit();
}

// --- drawing ----------------------------------------------------------------------------------------------------------------------

std::vector<std::string> SchoolGame::StatsLines() const
{
    std::vector<std::string> out;
    char buf[256];
    const School::SimStats& ss = sim_->Stats();
    std::snprintf(buf, sizeof buf, "%.0f fps  %.1f ms  %dx%d  %s", static_cast<double>(stats_.fps), static_cast<double>(stats_.frameMilliseconds), stats_.backBufferWidth, stats_.backBufferHeight, rendererName_.c_str());
    out.emplace_back(buf);
    std::snprintf(buf, sizeof buf, "sim %.2f ms  clock %s  day %d  %s  x%g  digest %016llx", static_cast<double>(stats_.simMilliseconds), School::ClockText(sim_->Time(), true).c_str(),
                  sim_->Day() + 1, PhaseLabel(sim_->Schedule(), sim_->PhaseIndex()).c_str(), static_cast<double>(timePaused_ ? 0.0f : timeScale_), static_cast<unsigned long long>(sim_->Digest()));
    out.emplace_back(buf);
    std::snprintf(buf, sizeof buf, "people %d  on campus %d  walking %d (full %d, reduced %d, abstract %d)  drawn %d (%d detailed)", stats_.npcTotal, stats_.npcOnCampus, stats_.npcWalking,
                  stats_.npcFull, ss.reduced, ss.abstract, stats_.npcDrawn, stats_.npcDrawnDetailed);
    out.emplace_back(buf);
    std::snprintf(buf, sizeof buf, "rooms loaded %d  drawn %d  culled %d  built %d  build %.1f ms  doors %d", stats_.roomsLoaded, stats_.roomsDrawn, stats_.roomsCulled, stats_.roomsBuiltTotal,
                  static_cast<double>(stats_.buildMilliseconds), stats_.doorsDrawn);
    out.emplace_back(buf);
    std::snprintf(buf, sizeof buf, "triangles %d  draw calls %d  textures %d  voices %d  loops %d  particles %d  screens/frame %d  cctv %s", stats_.trianglesDrawn, stats_.drawCalls,
                  stats_.textures, stats_.activeVoices, schoolAudio_->ActiveLoops(), stats_.particles, stats_.screensRendered, screens_->CctvActive() ? "on" : "off");
    out.emplace_back(buf);
    std::snprintf(buf, sizeof buf, "paths %d  events %lld  door passes %lld  rides %lld  unstuck %lld  unreachable %d  late %.0f  stuck now %d", ss.pathBuilds, ss.events, ss.doorPasses,
                  ss.elevatorRides, ss.unstuck, ss.unreachable, ss.lateArrivals, sim_->StuckSuspects());
    out.emplace_back(buf);
    const Vector3 f = player_->Feet();
    std::snprintf(buf, sizeof buf, "player %.1f %.1f %.1f  level %d  room %s  nav trees %d  rss %ld MB", static_cast<double>(f.X), static_cast<double>(f.Y), static_cast<double>(f.Z), player_->Level(),
                  currentRoom_ == School::kNone ? "-" : plan_.room(currentRoom_).code.c_str(), nav_->TreesBuilt(), ResidentMegabytes());
    out.emplace_back(buf);
    if (sim_->Drill().active) {
        std::snprintf(buf, sizeof buf, "DRILL: %d of %d assembled, %.0f s", sim_->Drill().assembled, sim_->Drill().total, sim_->Time() - sim_->Drill().startedAt);
        out.emplace_back(buf);
    }
    return out;
}

void SchoolGame::DrawDebug(const DrawContext& ctx)
{
    if (debugMode_ == 0) return;
    const Vector3 eye = ctx.view.eye;
    if (debugMode_ == 1) {
        // Walkers' remaining paths, coloured by fidelity: full green, reduced yellow, abstract grey.
        const School::Population& pop = sim_->Pop();
        int shown = 0;
        for (const School::Npc& n : pop.npcs) {
            const School::Agent& a = sim_->AgentOf(n.id);
            if (a.mode != School::Mode::Transit || !a.journey) continue;
            School::Vec3 p;
            float yaw;
            RoomId room;
            sim_->Observe(n.id, p, yaw, room);
            if (Vector3::DistanceSquared(Vector3(p.x, p.y, p.z), eye) > 70.0f * 70.0f) continue;
            const Color c = a.fidelity == School::Fidelity::Full ? Color(80, 255, 90, 255) : a.fidelity == School::Fidelity::Reduced ? Color(255, 220, 60, 255) : Color(160, 160, 170, 255);
            Vector3 prev(p.x, p.y + 0.1f, p.z);
            const int from = a.fidelity == School::Fidelity::Full ? a.leg : 0;
            for (std::size_t i = static_cast<std::size_t>(std::max(0, from)); i < a.journey->pts.size(); ++i) {
                const School::Vec3& w = a.journey->pts[i].pos;
                const Vector3 next(w.x, w.y + 0.1f, w.z);
                if (i > static_cast<std::size_t>(from) || a.fidelity == School::Fidelity::Full) gizmos_->Line(prev, next, c);
                prev = next;
            }
            gizmos_->Line(Vector3(p.x, p.y, p.z), Vector3(p.x, p.y + 2.2f, p.z), c);
            if (++shown > 400) break;
        }
    } else if (debugMode_ == 2) {
        for (const RoomId id : world_->LoadedRooms()) {
            const BoundingBox b = world_->RoomBounds(id);
            if (Vector3::DistanceSquared(b.Min, eye) > 90.0f * 90.0f && Vector3::DistanceSquared(b.Max, eye) > 90.0f * 90.0f) continue;
            gizmos_->Box(b, id == currentRoom_ ? Color(255, 230, 90, 255) : Color(90, 160, 255, 255));
            const Room& r = plan_.room(id);
            const float y = plan_.FloorY(r.level) + 0.05f;
            for (const School::DoorId did : r.doors) {
                const School::Door& d = plan_.door(did);
                gizmos_->Circle(Vector3(d.pos.x, y, d.pos.z), Vector3::Up, 0.4f, Color(255, 120, 60, 255), 12);
            }
            const School::RoomLayout& l = layouts_->Get(id);
            for (const School::Spot& s : l.seats) gizmos_->Circle(Vector3(s.pos.x, y, s.pos.z), Vector3::Up, 0.15f, Color(120, 220, 255, 255), 8);
            for (const School::Spot& s : l.idle) gizmos_->Circle(Vector3(s.pos.x, y, s.pos.z), Vector3::Up, 0.15f, Color(200, 200, 120, 255), 8);
            gizmos_->Circle(Vector3(l.hub.x, y, l.hub.z), Vector3::Up, 0.3f, Color(255, 255, 255, 255), 10);
        }
    } else {
        const Vector3 feet = player_->Feet();
        for (const RoomId id : collision_->RoomsAround(School::Vec2{feet.X, feet.Z}, player_->Level(), 1.5f))
            for (const BoundingBox& b : collision_->RoomBoxes(id)) gizmos_->Box(b, Color(255, 90, 90, 255));
    }
    gizmos_->Flush(ctx, false);
}

void SchoolGame::Draw(const GameTime& gameTime)
{
    GraphicsDevice& device = getGraphicsDeviceProperty();
    if (!world_) {
        device.Clear(Color::DarkSlateGray);
        return;
    }
    const auto& vp = device.getViewportProperty();
    const float aspect = static_cast<float>(vp.getWidthProperty()) / static_cast<float>(std::max(1, vp.getHeightProperty()));
    camera_.SetLens(kFov, aspect, kNearPlane, kFarPlane);
    const SceneView view(camera_.View(), camera_.Projection(), camera_.Position());
    services_.mainView = &view;
    DrawContext ctx{device, *effects_, view, time_, false};
    effects_->SetCamera(view.view, view.projection);

    // Daylight for everything outdoors; fog for distance and weather.
    const float day = timeOfDay_.Daylight();
    const float overcast = options_.weather == "clear" ? 0.0f : (options_.weather == "fog" ? 0.6f : 0.85f);
    sky_->SetOvercast(overcast);
    WorldLighting light;
    const Vector3 sun = timeOfDay_.SunColor();
    light.daylight = day;
    light.sunDirection = -timeOfDay_.SunDirection();
    light.outdoorTint = Vector3::Clamp(timeOfDay_.AmbientColor() * 0.9f + sun * 0.75f + Vector3(0.08f, 0.08f, 0.1f), Vector3(0.12f, 0.13f, 0.18f), Vector3(1, 1, 1));
    light.outdoorTint = light.outdoorTint * (1.0f - 0.4f * overcast);
    // Lights: on in every room while the school is open and it is dark; otherwise only where somebody is. Eased so a
    // switch is a quick fade, not a pop.
    {
        const bool open = sim_->Schedule().At(sim_->PhaseIndex()).kind != School::PhaseKind::Night;
        const bool dim = day < 0.55f;
        if (roomLight_.size() != plan_.rooms.size()) roomLight_.assign(plan_.rooms.size(), 1.0f);
        const float dt = std::min(0.1f, static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalMillisecondsProperty()) / 1000.0f);
        for (const RoomId id : world_->LoadedRooms()) {
            const Room& r = plan_.room(id);
            if (!r.Indoor()) continue;
            const bool circulation = School::Info(r.type).circulation;
            const bool occupied = sim_->OccupantsOf(id) > 0 || id == currentRoom_;
            float want;
            if (!dim) want = 1.0f;                       // daylight: the lights hardly matter, keep the rooms bright
            else if (open) want = circulation || occupied ? 1.0f : 0.0f;
            else want = occupied ? 1.0f : (r.type == RoomType::ServerRoom || r.type == RoomType::SecurityOffice ? 1.0f : 0.0f);
            if (lightOverride_.size() != plan_.rooms.size()) lightOverride_.assign(plan_.rooms.size(), 0);
            const signed char ov = lightOverride_[static_cast<std::size_t>(id)];
            if (ov > 0) want = 1.0f;
            else if (ov < 0) want = 0.12f;   // off: the room keeps a little light from its windows and the corridor
            float& v = roomLight_[static_cast<std::size_t>(id)];
            v += (want - v) * std::min(1.0f, dt * 4.0f);
        }
        light.roomLight = &roomLight_;
    }
    const Vector3 fogColour = Vector3::Lerp(Vector3::Lerp(Vector3(0.03f, 0.04f, 0.09f), Vector3(0.74f, 0.82f, 0.92f), day), Vector3(0.55f, 0.57f, 0.6f) * (0.2f + 0.8f * day), overcast);
    const bool foggy = options_.weather == "fog";
    for (BasicEffect* fx : {&effects_->World(), &effects_->Lit()}) {
        fx->setFogEnabledProperty(true);
        fx->setFogColorProperty(fogColour);
        fx->setFogStartProperty(foggy ? 6.0f : 140.0f);
        fx->setFogEndProperty(foggy ? 55.0f : kFarPlane);
    }

    const auto stage = [&](const char* name, auto&& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            if (drawErrorsSeen_.insert(std::string(name) + ": " + e.what()).second) School::Log("draw-error", "%s: %s", name, e.what());
            device.SetRenderTarget(nullptr);
        }
    };
    stage("offscreen", [&] { screens_->RenderOffscreen(device, *effects_, *sky_, *world_, *npcs_, *sim_, light, time_); });
    device.SetRenderTarget(nullptr);
    effects_->SetCamera(view.view, view.projection);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color(0, 0, 0, 255), 1.0f, 0);
    RenderStates::SetOpaque3D(device);
    effects_->World().setFogEnabledProperty(false);
    stage("sky", [&] { sky_->Draw(ctx); });
    effects_->World().setFogEnabledProperty(true);
    stage("world", [&] { world_->Draw(ctx, light, sim_->Doors(), elevators_); });
    stage("people", [&] { npcs_->Draw(ctx, *sim_, *world_, npcStats_); });
    stage("screens", [&] { screens_->Draw(ctx, *sim_, *world_); });
    stage("room effects", [&] { roomEffects_->Draw(ctx); });
    stage("weather", [&] { weather_->Draw(ctx); });
    stage("debug", [&] { DrawDebug(ctx); });
    effects_->World().setFogEnabledProperty(false);
    effects_->Lit().setFogEnabledProperty(false);

    Game::Draw(gameTime);
    if (options_.hud) {
        HudState hud;
        hud.clock = School::ClockText(sim_->Time());
        hud.day = kWeekdays[sim_->Weekday()];
        hud.phase = PhaseLabel(sim_->Schedule(), sim_->PhaseIndex());
        hud.speed = timePaused_ ? "clock stopped" : (timeScale_ == 1.0f ? "x1" : "x" + std::to_string(static_cast<int>(timeScale_)));
        hud.prompt = paused_ || bigMap_ ? std::string() : focusPrompt_;
        hud.minimal = bigMap_;
        hud.paused = paused_;
        hud.showHelp = showHelp_;
        hud.showStats = showStats_;
        hud.drill = sim_->Drill().active;
        if (hud.drill) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "FIRE DRILL  -  %d of %d at the assembly points", sim_->Drill().assembled, sim_->Drill().total);
            hud.drillText = buf;
        }
        hud.bellFlash = bellFlash_;
        hud.rendererName = rendererName_;
        hud.mouseSensitivityScale = sensitivityScale_;
        hud.volume = audio_->MasterVolume();
        hud.audioMuted = audio_->Muted();
        if (showStats_) hud.statsLines = StatsLines();
        if (bannerRoom_ != School::kNone && bannerAge_ < 5.0f) {
            const Room& r = plan_.room(bannerRoom_);
            hud.roomTitle = r.name;
            const School::SchoolSim::RoomLesson lesson = sim_->LessonIn(bannerRoom_);
            const int people = sim_->OccupantsOf(bannerRoom_);
            if (lesson.valid) {
                const School::ClassGroup& g = sim_->Pop().classes[static_cast<std::size_t>(lesson.classId)];
                static const char* kStates[] = {"empty", "arriving", "lesson in progress", "lesson ending", "busy"};
                hud.roomSubtitle = std::string(School::SubjectName(lesson.subject)) + ", class " + g.name + (lesson.teacher >= 0 ? ", " + sim_->Pop().npcs[static_cast<std::size_t>(lesson.teacher)].name : "") +
                                   "  (" + std::to_string(people) + " people, " + kStates[static_cast<int>(sim_->StateOf(bannerRoom_))] + ")";
            } else {
                hud.roomSubtitle = plan_.wings[static_cast<std::size_t>(r.wing)].name + "  (" + std::to_string(people) + " people)";
            }
            hud.bannerAlpha = std::clamp(std::min(bannerAge_ * 3.0f, (5.0f - bannerAge_) * 1.5f), 0.0f, 1.0f);
        }
        stage("hud", [&] {
            hud_->Draw(hud);
            if (bigMap_ || (showMinimap_ && !paused_)) {
                MapView mv;
                mv.level = player_->Level();
                mv.player = player_->Feet();
                mv.playerYaw = camera_.Yaw();
                mv.currentRoom = currentRoom_;
                mv.showLoaded = debugMode_ != 0;
                if (bigMap_) {
                    mv.follow = false;
                    minimap_->Draw(mv, Rectangle(40, 30, hud_->Width() - 80, hud_->Height() - 60), *sim_, world_.get());
                } else {
                    mv.follow = true;
                    mv.metresAcross = 110.0f;
                    const int size = std::clamp(hud_->Width() / 5, 200, 300);
                    minimap_->Draw(mv, Rectangle(hud_->Width() - size - 12, 12, size, size), *sim_, world_.get());
                }
            }
            hud_->End();
        });
    }
    services_.mainView = nullptr;

    if (!pendingShot_.empty()) {
        SaveScreenshot(pendingShot_);
        pendingShot_.clear();
    }
    if (!options_.screenshotPath.empty() && !screenshotDone_ && frame_ >= options_.screenshotFrame) {
        SaveScreenshot(options_.screenshotPath);
        screenshotDone_ = true;
        Exit();
    }
}

void SchoolGame::SaveScreenshot(const std::string& path)
{
    GraphicsDevice& device = getGraphicsDeviceProperty();
    const PresentationParameters& pp = device.getPresentationParametersProperty();
    const int w = pp.getBackBufferWidthProperty();
    const int h = pp.getBackBufferHeightProperty();
    std::vector<Color> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    try {
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        if (ImageUtil::WritePng(path.c_str(), pixels.data(), w, h)) School::Log("screenshot", "wrote %s (%dx%d)", path.c_str(), w, h);
        else School::Log("screenshot", "cannot write %s", path.c_str());
    } catch (const std::exception& e) {
        School::Log("screenshot", "back buffer read failed: %s", e.what());
    }
}

// Test scripting (--script "a;b;c"). Steps run one per frame unless they wait:
//   wait:N          skip N frames                  goto:ROOM       stand in a room (code, name or type)
//   pos:x,y,z       put the feet there             look:yaw,pitch  set the view (degrees)
//   walk:F,S,N      walk with forward F / strafe S input for N frames (collision applies)
//   turn:DEG        turn by an angle               pitch:DEG       look up or down
//   time:HH:MM      set the school clock           speed:F         time scale        skip   next lesson
//   drill           toggle the fire drill          bell            ring the bell     announce   PA message
//   e               use what is under the crosshair    map     toggle the big map    debug:N   overlay mode
//   stats           log the statistics line       shot:FILE       save a screenshot     quit   exit
void SchoolGame::RunScript()
{
    if (scriptWait_ > 0) {
        --scriptWait_;
        return;
    }
    while (scriptIndex_ < scriptSteps_.size()) {
        const std::string step = scriptSteps_[scriptIndex_++];
        const std::size_t colon = step.find(':');
        const std::string op = step.substr(0, colon);
        const std::string arg = colon == std::string::npos ? std::string() : step.substr(colon + 1);
        if (op == "wait") {
            scriptWait_ = std::atoi(arg.c_str());
            return;
        } else if (op == "goto") {
            if (!GoToRoom(arg)) School::Log("script", "goto: unknown room '%s'", arg.c_str());
        } else if (op == "busiest") {
            // busiest:TYPE  stand in the room of that type with the most people in it
            // Counts everybody, including walkers passing through (OccupantsOf knows only the seated and the nearby).
            std::vector<int> count(plan_.rooms.size(), 0);
            for (const School::Npc& n : sim_->Pop().npcs) {
                if (sim_->AgentOf(n.id).mode == School::Mode::Offsite) continue;
                School::Vec3 p;
                float yaw;
                RoomId room;
                sim_->Observe(n.id, p, yaw, room);
                if (room != School::kNone) ++count[static_cast<std::size_t>(room)];
            }
            RoomId best = School::kNone;
            for (const Room& r : plan_.rooms) {
                const std::string tn = School::TypeName(r.type);
                if (tn.size() != arg.size() || !std::equal(tn.begin(), tn.end(), arg.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); })) continue;
                if (best == School::kNone || count[static_cast<std::size_t>(r.id)] > count[static_cast<std::size_t>(best)]) best = r.id;
            }
            if (best != School::kNone) {
                PlaceInRoom(best);
                School::Log("script", "busiest %s: %s with %d people", arg.c_str(), plan_.room(best).name.c_str(), count[static_cast<std::size_t>(best)]);
            } else {
                School::Log("script", "busiest: no room of type '%s'", arg.c_str());
            }
        } else if (op == "atdoor") {
            // atdoor:CODE[,N]  stand two metres outside the N-th door of a room (default: the first with a leaf), facing it
            std::string code = arg;
            int which = 0;
            if (const std::size_t comma = arg.find(','); comma != std::string::npos) {
                code = arg.substr(0, comma);
                which = std::atoi(arg.c_str() + comma + 1);
            }
            RoomId room = School::kNone;
            for (const Room& r : plan_.rooms)
                if (r.code == code) room = r.id;
            if (room == School::kNone) {
                School::Log("script", "atdoor: unknown room '%s'", code.c_str());
            } else {
                int seen = 0;
                for (const School::DoorId did : plan_.room(room).doors) {
                    const School::Door& d = plan_.door(did);
                    if (!d.Leaf() || seen++ != which) continue;
                    School::Vec2 n = d.Normal();
                    if (d.a != room) n = n * -1.0f;   // from the room outwards
                    const School::Vec2 at = d.pos + n * 2.0f;
                    const float yaw = std::atan2(n.x, n.z);   // facing -n
                    player_->Place(Vector3(at.x, plan_.FloorY(d.level), at.z), yaw, 0.0f, camera_);
                    world_->Ensure(room);
                    world_->Ensure(d.Other(room));
                    break;
                }
            }
        } else if (op == "pos") {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(arg.c_str(), "%f,%f,%f", &x, &y, &z) == 3) player_->Place(Vector3(x, y, z), camera_.Yaw(), camera_.Pitch(), camera_);
        } else if (op == "turn") {
            camera_.Rotate(MathHelper::ToRadians(static_cast<float>(std::atof(arg.c_str()))), 0.0f);
        } else if (op == "pitch") {
            camera_.SetPose(camera_.Position(), camera_.Yaw(), MathHelper::ToRadians(static_cast<float>(std::atof(arg.c_str()))));
        } else if (op == "look") {
            float yaw = 0, pitch = 0;
            if (std::sscanf(arg.c_str(), "%f,%f", &yaw, &pitch) == 2) camera_.SetPose(camera_.Position(), MathHelper::ToRadians(yaw), MathHelper::ToRadians(pitch));
        } else if (op == "walk") {
            float f = 0, s = 0;
            int n = 0;
            if (std::sscanf(arg.c_str(), "%f,%f,%d", &f, &s, &n) == 3) player_->ScriptWalk(f, s, n);
            scriptWait_ = n;
            return;
        } else if (op == "time") {
            int h = 0, m = 0;
            if (std::sscanf(arg.c_str(), "%d:%d", &h, &m) == 2) {
                sim_->JumpToTime(h * 3600.0 + m * 60.0);
                (void)sim_->TakeBells();
                lastPhase_ = sim_->PhaseIndex();
            }
        } else if (op == "speed") {
            SetTimeScale(static_cast<float>(std::atof(arg.c_str())));
        } else if (op == "skip") {
            sim_->SkipToNextPhase(true);
        } else if (op == "drill") {
            if (sim_->Drill().active) sim_->EndDrill();
            else sim_->StartDrill();
        } else if (op == "bell") {
            schoolAudio_->Bell(false);
            bellFlash_ = 1.0f;
        } else if (op == "announce") {
            Announce();
        } else if (op == "e") {
            UpdateInteractables();
            const std::optional<float> wall = collision_->Raycast(camera_.ViewRay(), 4.0f, player_->Level());
            interaction_.Focus(camera_.ViewRay(), 6.0f, wall.value_or(1e9f));
            const std::string prompt = interaction_.FocusedPrompt();
            const bool used = interaction_.Activate();
            School::Log("script", "e: %s", used ? prompt.c_str() : "nothing under the crosshair");
        } else if (op == "map") {
            bigMap_ = !bigMap_;
            ApplyMouseMode();
        } else if (op == "debug") {
            debugMode_ = std::atoi(arg.c_str()) % 4;
        } else if (op == "stats") {
            for (const std::string& l : StatsLines()) School::Log("stats", "%s", l.c_str());
        } else if (op == "shot") {
            pendingShot_ = arg;
            scriptWait_ = 2;
            return;
        } else if (op == "quit") {
            Exit();
            return;
        } else {
            School::Log("script", "unknown step '%s'", step.c_str());
        }
    }
}

GetTypeNameCPP(SchoolGame, "SchoolGame")
