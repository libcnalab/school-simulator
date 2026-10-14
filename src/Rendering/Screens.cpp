#include "Rendering/Screens.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Rendering/EffectBank.hpp"
#include "Rendering/RenderStates.hpp"
#include "Rendering/TextureLibrary.hpp"
#include "Simulation/Schedule.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using School::Room;
using School::RoomType;

namespace {
constexpr int kCamW = 320, kCamH = 180;
constexpr float kCctvRange = 16.0f;    // metres from the wall within which cameras are refreshed
constexpr float kSignRange = 45.0f;

const char* kChalk[static_cast<int>(School::Subject::Count)][3] = {
    {"a^2 + b^2 = c^2", "x = (-b +- sqrt(b^2 - 4ac)) / 2a", "Homework: exercises 12 to 18"},
    {"Les verbes irreguliers", "etre - avoir - aller - faire", "Devoir: page 42"},
    {"Past simple vs present perfect", "I went / I have gone", "Essay: 300 words by Friday"},
    {"1914 - 1918  The Great War", "Causes: alliances, arms race, nationalism", "Read chapter 7"},
    {"Plate tectonics", "convergent / divergent / transform", "Map exercise on page 61"},
    {"F = m * a      p = m * v", "E = 1/2 m v^2", "Lab report due Thursday"},
    {"2 H2 + O2 -> 2 H2O", "Moles: n = m / M", "Safety goggles on!"},
    {"Cell structure: nucleus, mitochondria", "Photosynthesis: 6 CO2 + 6 H2O -> C6H12O6 + 6 O2", "Microscope slides today"},
    {"Warm up: 2 laps", "Basketball: passing drills", "Teams of five"},
    {"Key of G major: one sharp (F#)", "4/4 time, quarter note = 120", "Rehearsal Thursday"},
    {"Still life: light and shadow", "Mix secondary colours", "Portfolio review next week"},
    {"for (int i = 0; i < n; ++i)", "Loops, arrays, functions", "Project: text adventure"},
    {"Measure twice, cut once", "Safety: guards on every machine", "Project: birdhouse"},
    {"Scene 3: blocking", "Project your voice", "Dress rehearsal Friday"},
    {"Quiet please", "Return books by the due date", "New arrivals on the front shelf"},
};
}  // namespace

Screens::Screens(Services& services, const School::SchoolPlan& plan, const School::LayoutCache& layouts) : s_(services), plan_(plan), layouts_(layouts)
{
    const auto cam = [&](const char* name, Vector3 eye, Vector3 target, int level) { cameras_.push_back({name, eye, target, level}); };
    const Room& lobby = plan.room(plan.mainLobby);
    const School::Vec2 lc = lobby.rect.Center();
    cam("Main lobby", Vector3(lobby.rect.x0 + 1.0f, 3.2f, lobby.rect.z0 + 1.0f), Vector3(lc.x, 1.0f, lobby.rect.z1 - 3.0f), 1);
    cam("Front plaza", Vector3(lc.x, 4.0f, lobby.rect.z1 + 0.6f), Vector3(lc.x, 0.5f, lobby.rect.z1 + 18.0f), 1);
    for (const Room& r : plan.rooms) {
        const School::Rect& b = r.rect;
        const Vector3 corner(b.x0 + 1.0f, plan.FloorY(r.level) + (r.levels > 1 ? 5.0f : 3.0f), b.z0 + 1.0f);
        const Vector3 centre(b.Center().x, plan.FloorY(r.level) + 0.8f, b.Center().z);
        if (r.type == RoomType::Cafeteria) cam("Cafeteria", corner, centre, r.level);
        else if (r.type == RoomType::Courtyard) cam("Courtyard", Vector3(b.x0 + 1.0f, 4.5f, b.z0 + 1.0f), Vector3(b.Center().x, 0.0f, b.Center().z), 1);
        else if (r.type == RoomType::Gym) cam("Gymnasium", corner, centre, r.level);
        else if (r.type == RoomType::Library) cam("Library", corner, centre, r.level);
        else if (r.type == RoomType::Parking) cam("Car park", Vector3(b.Center().x, 4.0f, b.z0 + 0.5f), Vector3(b.Center().x, 0.5f, b.z1 - 5.0f), 1);
        else if (r.type == RoomType::Corridor && r.level == 2 && cameras_.size() < 9 && r.rect.Width() > 15.0f && std::fabs(r.rect.z0 - 9.0f) < 0.1f && r.rect.x0 < 10.0f)
            cam("North corridor, first floor", Vector3(b.x0 + 0.5f, plan.FloorY(2) + 3.0f, b.Center().z), Vector3(b.x1 + 30.0f, plan.FloorY(2) + 1.0f, b.Center().z), 2);
    }
    for (std::size_t i = 0; i < cameras_.size(); ++i) {
        cameraTargets_.push_back(std::make_unique<RenderTarget2D>(*s_.device, kCamW, kCamH, false, SurfaceFormat::Color, DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents));
        cameraRendered_.push_back(false);
    }
    // CCTV walls: the Screen fixture of every security office.
    for (const Room& r : plan.rooms) {
        if (r.type != RoomType::SecurityOffice) continue;
        for (const School::Fixture& f : layouts.Get(r.id).fixtures)
            if (f.type == School::FixtureType::Screen) walls_.push_back({Vector3(f.pos.x, plan.FloorY(r.level) + f.y0 + f.sy * 0.5f, f.pos.z), f.yaw, f.sx, f.sy, f.sz});
    }
    // Signage: the lobby's north wall between the courtyard doors, and above the serving line in the cafeteria.
    signs_.push_back({Vector3(lc.x, 2.9f, lobby.rect.z0 + 0.03f), MathHelper::Pi, 2.6f, 1.3f});
    for (const Room& r : plan.rooms)
        if (r.type == RoomType::Cafeteria) signs_.push_back({Vector3(r.rect.Center().x + 8.0f, 3.2f, r.rect.z0 + 0.03f), MathHelper::Pi, 2.6f, 1.3f});
    signage_ = std::make_unique<RenderTarget2D>(*s_.device, 512, 256, false, SurfaceFormat::Color, DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    // Diagnostics monitors: the computer desk in every server room and security office shows the frame statistics.
    for (const Room& r : plan.rooms) {
        if (r.type != RoomType::ServerRoom && r.type != RoomType::SecurityOffice) continue;
        for (const School::Fixture& f : layouts.Get(r.id).fixtures) {
            if (f.type != School::FixtureType::ComputerDesk) continue;
            // the monitor's screen: see AddFixture (ComputerDesk): centre (0, sy + 0.26, hz - 0.2 - 0.019) in the desk's frame
            const Matrix frame = Matrix::CreateRotationY(f.yaw) * Matrix::CreateTranslation(f.pos.x, plan.FloorY(r.level) + f.y0, f.pos.z);
            monitors_.push_back({Vector3::Transform(Vector3(0.0f, f.sy + 0.26f, f.sz * 0.5f - 0.2f - 0.019f), frame), f.yaw});
        }
    }
    diagnostics_ = std::make_unique<RenderTarget2D>(*s_.device, 384, 256, false, SurfaceFormat::Color, DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    desktop_ = std::make_unique<RenderTarget2D>(*s_.device, 256, 192, false, SurfaceFormat::Color, DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    for (std::size_t i = 0; i < plan.elevators.size(); ++i) {
        Indicator ind;
        ind.target = std::make_unique<RenderTarget2D>(*s_.device, 128, 64, false, SurfaceFormat::Color, DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        indicators_.push_back(std::move(ind));
    }
}

void Screens::RenderDesktop(GraphicsDevice& device)
{
    if (s_.font == nullptr) return;
    device.SetRenderTarget(desktop_.get());
    device.Clear(Color(18, 40, 78, 255));
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
    // taskbar, a few window frames and a bouncing logo: enough to read as a desktop from across the room
    s_.sprites->Draw(*s_.textures->White(), Rectangle(0, 172, 256, 20), Color(30, 34, 44, 255));
    s_.sprites->Draw(*s_.textures->White(), Rectangle(24, 22, 150, 110), Color(236, 238, 242, 255));
    s_.sprites->Draw(*s_.textures->White(), Rectangle(24, 22, 150, 14), Color(70, 110, 180, 255));
    for (int i = 0; i < 5; ++i) s_.sprites->Draw(*s_.textures->White(), Rectangle(32, 44 + i * 16, 60 + (i * 37) % 70, 6), Color(120, 128, 140, 255));
    const float t = desktopTime_;
    const int bx = 150 + static_cast<int>(70.0f * std::fabs(std::sin(t * 0.7f)));
    const int by = 40 + static_cast<int>(100.0f * std::fabs(std::sin(t * 0.9f + 1.0f)));
    s_.sprites->Draw(*s_.textures->White(), Rectangle(bx, by, 26, 26), Color(255, 180, 40, 255));
    s_.sprites->Draw(*s_.textures->White(), Rectangle(bx + 6, by + 6, 14, 14), Color(18, 40, 78, 255));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d", static_cast<int>(t / 60.0f) % 24, static_cast<int>(t) % 60);
    s_.sprites->DrawString(*s_.font, "SCHOOL IT", Vector2(8, 173), Color(200, 210, 230, 255), 0.0f, Vector2(0, 0), 0.5f, SpriteEffects::None, 0.0f);
    s_.sprites->End();
    device.SetRenderTarget(nullptr);
    desktopAge_ = 0.0f;
    ++rendered_;
}

void Screens::RenderIndicator(GraphicsDevice& device, int shaft, const School::ElevatorStatus& st)
{
    if (s_.titleFont == nullptr) return;
    Indicator& ind = indicators_[static_cast<std::size_t>(shaft)];
    device.SetRenderTarget(ind.target.get());
    device.Clear(Color(10, 10, 14, 255));
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
    const int floorLabel = st.level - 1;
    std::string text = floorLabel < 0 ? "B" : std::to_string(floorLabel);
    if (st.moving && st.target >= 0) text += st.target > st.level ? " ^" : " v";
    const Vector2 size = s_.titleFont->MeasureString(text) * 0.8f;
    s_.sprites->DrawString(*s_.titleFont, text, Vector2(64.0f - size.X * 0.5f, 6.0f), Color(255, 120, 40, 255), 0.0f, Vector2(0, 0), 0.8f, SpriteEffects::None, 0.0f);
    s_.sprites->End();
    device.SetRenderTarget(nullptr);
    ind.shownLevel = st.level;
    ind.shownMoving = st.moving;
    ++rendered_;
}

void Screens::RenderDiagnostics(GraphicsDevice& device)
{
    if (s_.font == nullptr || s_.stats == nullptr) return;
    device.SetRenderTarget(diagnostics_.get());
    device.Clear(Color(6, 10, 16, 255));
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
    const Color green(120, 255, 140, 255), dim(90, 160, 110, 255);
    const FrameStats& st = *s_.stats;
    char buf[128];
    float y = 10.0f;
    const auto line = [&](const char* text, const Color& c) {
        s_.sprites->DrawString(*s_.font, text, Vector2(12, y), c, 0.0f, Vector2(0, 0), 0.62f, SpriteEffects::None, 0.0f);
        y += 22.0f;
    };
    line("SCHOOL DIAGNOSTICS", dim);
    std::snprintf(buf, sizeof buf, "%.0f fps  %.1f ms", static_cast<double>(st.fps), static_cast<double>(st.frameMilliseconds));
    line(buf, green);
    std::snprintf(buf, sizeof buf, "sim %.2f ms  people %d", static_cast<double>(st.simMilliseconds), st.npcTotal);
    line(buf, green);
    std::snprintf(buf, sizeof buf, "on campus %d  walking %d", st.npcOnCampus, st.npcWalking);
    line(buf, green);
    std::snprintf(buf, sizeof buf, "full %d  drawn %d", st.npcFull, st.npcDrawn);
    line(buf, green);
    std::snprintf(buf, sizeof buf, "rooms %d loaded  %d drawn", st.roomsLoaded, st.roomsDrawn);
    line(buf, green);
    std::snprintf(buf, sizeof buf, "tris %d  calls %d", st.trianglesDrawn, st.drawCalls);
    line(buf, green);
    std::snprintf(buf, sizeof buf, "voices %d  particles %d", st.activeVoices, st.particles);
    line(buf, green);
    std::snprintf(buf, sizeof buf, "textures %d  frame %lld", st.textures, st.frame);
    line(buf, green);
    // a little bar graph of the frame time history: the last 30 half-second samples are not kept, so draw the current value
    const int bar = std::clamp(static_cast<int>(st.frameMilliseconds * 6.0f), 2, 360);
    s_.sprites->Draw(*s_.textures->White(), Rectangle(12, 220, bar, 14), bar > 200 ? Color(255, 120, 80, 255) : green);
    s_.sprites->Draw(*s_.textures->White(), Rectangle(12 + 100, 216, 2, 22), dim);   // the 16.7 ms mark
    s_.sprites->End();
    device.SetRenderTarget(nullptr);
    diagnosticsAge_ = 0.0f;
    ++rendered_;
}

Screens::~Screens() = default;

void Screens::CycleCameras()
{
    if (!cameras_.empty()) firstShown_ = (firstShown_ + 1) % static_cast<int>(cameras_.size());
}

void Screens::Update(float dt, const Vector3& eye, const School::SchoolSim&)
{
    eye_ = eye;
    cctvActive_ = false;
    for (const Wall& w : walls_)
        if (Vector3::DistanceSquared(w.centre, eye) < kCctvRange * kCctvRange) cctvActive_ = true;
    signageNear_ = false;
    for (const Sign& s : signs_)
        if (Vector3::DistanceSquared(s.centre, eye) < kSignRange * kSignRange) signageNear_ = true;
    signageAge_ += dt;
    diagnosticsNear_ = false;
    for (const Monitor& m : monitors_)
        if (Vector3::DistanceSquared(m.centre, eye) < 14.0f * 14.0f) diagnosticsNear_ = true;
    diagnosticsAge_ += dt;
    desktopAge_ += dt;
    desktopTime_ += dt;
    desktopNear_ = false;
    for (const Room& r : plan_.rooms) {
        if (r.type != RoomType::ComputerLab) continue;
        const School::Vec2 c = r.rect.Clamp(School::Vec2{eye.X, eye.Z});
        const float dy = plan_.FloorY(r.level) - eye.Y;
        if ((c.x - eye.X) * (c.x - eye.X) + (c.z - eye.Z) * (c.z - eye.Z) + dy * dy < 20.0f * 20.0f) desktopNear_ = true;
    }
}

std::vector<WorldRenderer::Anchor> Screens::Anchors() const
{
    std::vector<WorldRenderer::Anchor> out;
    if (!cctvActive_ || cameras_.empty()) return out;
    for (int k = 0; k < 4 && k < static_cast<int>(cameras_.size()); ++k) {
        const CctvCamera& c = cameras_[static_cast<std::size_t>((firstShown_ + k) % static_cast<int>(cameras_.size()))];
        out.push_back({c.target, c.level, 24.0f});
    }
    return out;
}

void Screens::RenderSignage(GraphicsDevice& device, const School::SchoolSim& sim)
{
    if (s_.font == nullptr || s_.titleFont == nullptr) return;
    device.SetRenderTarget(signage_.get());
    device.Clear(Color(8, 20, 40, 255));
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
    const Color gold(255, 214, 90, 255), ink(230, 236, 245, 255);
    const std::string clock = School::ClockText(sim.Time());
    s_.sprites->DrawString(*s_.titleFont, clock, Vector2(24, 14), gold, 0.0f, Vector2(0, 0), 1.4f, SpriteEffects::None, 0.0f);
    static const char* days[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};
    const School::BellSchedule& sch = sim.Schedule();
    const School::Phase& p = sch.At(sim.PhaseIndex());
    std::string line = std::string(days[sim.Weekday()]) + "   -   " + (p.kind == School::PhaseKind::Lesson ? "Lesson " + std::to_string(p.lesson) : p.name);
    s_.sprites->DrawString(*s_.font, line, Vector2(24, 112), ink, 0.0f, Vector2(0, 0), 0.9f, SpriteEffects::None, 0.0f);
    // the next bell
    std::string next = "No more bells today";
    for (int i = sim.PhaseIndex() + 1; i < static_cast<int>(sch.phases.size()); ++i)
        if (sch.At(i).bell) {
            next = "Next bell " + School::ClockText(sch.At(i).start) + "  " + (sch.At(i).kind == School::PhaseKind::Lesson ? "Lesson " + std::to_string(sch.At(i).lesson) : sch.At(i).name);
            break;
        }
    s_.sprites->DrawString(*s_.font, next, Vector2(24, 150), ink, 0.0f, Vector2(0, 0), 0.8f, SpriteEffects::None, 0.0f);
    if (sim.Drill().active) {
        s_.sprites->Draw(*s_.textures->White(), Rectangle(0, 196, 512, 60), Color(180, 20, 20, 255));
        s_.sprites->DrawString(*s_.font, "FIRE DRILL - leave by the nearest exit", Vector2(24, 206), Color(255, 240, 240, 255), 0.0f, Vector2(0, 0), 0.85f, SpriteEffects::None, 0.0f);
    } else {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%d people on campus", [&] {
            int n = 0;
            for (const School::Npc& npc : sim.Pop().npcs) n += sim.AgentOf(npc.id).mode != School::Mode::Offsite ? 1 : 0;
            return n;
        }());
        s_.sprites->DrawString(*s_.font, buf, Vector2(24, 206), Color(150, 200, 255, 255), 0.0f, Vector2(0, 0), 0.8f, SpriteEffects::None, 0.0f);
    }
    s_.sprites->End();
    device.SetRenderTarget(nullptr);
    signageAge_ = 0.0f;
    ++rendered_;
}

Texture2D* Screens::BoardFor(School::Subject subject)
{
    const int key = static_cast<int>(subject);
    auto it = boards_.find(key);
    if (it != boards_.end()) return it->second.get();
    if (s_.font == nullptr) return nullptr;
    auto rt = std::make_unique<RenderTarget2D>(*s_.device, 512, 256, false, SurfaceFormat::Color, DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    GraphicsDevice& device = *s_.device;
    device.SetRenderTarget(rt.get());
    device.Clear(Color(246, 247, 250, 255));
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
    const Color pen(30, 50, 120, 255), red(170, 30, 30, 255);
    s_.sprites->DrawString(*s_.font, School::SubjectName(subject), Vector2(26, 18), red, 0.0f, Vector2(0, 0), 1.1f, SpriteEffects::None, 0.0f);
    s_.sprites->Draw(*s_.textures->White(), Rectangle(26, 58, 300, 3), red);
    for (int i = 0; i < 3; ++i) s_.sprites->DrawString(*s_.font, kChalk[key][i], Vector2(26, 80.0f + 50.0f * static_cast<float>(i)), pen, 0.0f, Vector2(0, 0), 0.82f, SpriteEffects::None, 0.0f);
    s_.sprites->End();
    device.SetRenderTarget(nullptr);
    Texture2D* ptr = rt.get();
    boards_[key] = std::move(rt);
    ++rendered_;
    return ptr;
}

Texture2D* Screens::PlateFor(School::RoomId room)
{
    auto it = plates_.find(room);
    if (it != plates_.end()) return it->second.get();
    if (s_.font == nullptr) return nullptr;
    const Room& r = plan_.room(room);
    auto rt = std::make_unique<RenderTarget2D>(*s_.device, 192, 64, false, SurfaceFormat::Color, DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    GraphicsDevice& device = *s_.device;
    device.SetRenderTarget(rt.get());
    const School::Wing& w = plan_.wings[static_cast<std::size_t>(r.wing)];
    device.Clear(Color(static_cast<int>(w.accent[0] * 200.0f), static_cast<int>(w.accent[1] * 200.0f), static_cast<int>(w.accent[2] * 200.0f), 255));
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
    s_.sprites->Draw(*s_.textures->White(), Rectangle(4, 4, 184, 56), Color(250, 250, 248, 255));
    s_.sprites->DrawString(*s_.font, r.code, Vector2(12, 4), Color(20, 24, 40, 255), 0.0f, Vector2(0, 0), 1.05f, SpriteEffects::None, 0.0f);
    std::string type = School::TypeName(r.type);
    if (r.name.rfind(type, 0) != 0) {   // a named room ("Main lobby S0.41"): show the name without the code
        type = r.name.substr(0, r.name.size() > r.code.size() + 1 ? r.name.size() - r.code.size() - 1 : r.name.size());
    }
    s_.sprites->DrawString(*s_.font, type, Vector2(12, 36), Color(60, 64, 80, 255), 0.0f, Vector2(0, 0), 0.55f, SpriteEffects::None, 0.0f);
    s_.sprites->End();
    device.SetRenderTarget(nullptr);
    Texture2D* ptr = rt.get();
    plates_[room] = std::move(rt);
    ++rendered_;
    return ptr;
}

void Screens::RenderOffscreen(GraphicsDevice& device, EffectBank& fx, SkyDome& sky, WorldRenderer& world, NpcRenderer& npcs, const School::SchoolSim& sim, const WorldLighting& light, double time)
{
    rendered_ = 0;
    if (signageNear_ && signageAge_ >= 1.0f) RenderSignage(device, sim);
    if (diagnosticsNear_ && diagnosticsAge_ >= 0.5f) RenderDiagnostics(device);
    if (desktopNear_ && desktopAge_ >= 0.2f) RenderDesktop(device);
    for (int i = 0; i < sim.ElevatorCount() && i < static_cast<int>(indicators_.size()); ++i) {
        const School::ElevatorStatus st = sim.Elevator(i);
        const Indicator& ind = indicators_[static_cast<std::size_t>(i)];
        if (ind.shownLevel != st.level || ind.shownMoving != st.moving) RenderIndicator(device, i, st);
    }
    if (!cctvActive_ || cameras_.empty()) return;
    // One of the four shown cameras per frame, round robin.
    const int shown = std::min(4, static_cast<int>(cameras_.size()));
    const int index = (firstShown_ + nextCamera_) % static_cast<int>(cameras_.size());
    nextCamera_ = (nextCamera_ + 1) % shown;
    const CctvCamera& cam = cameras_[static_cast<std::size_t>(index)];
    RenderTarget2D* rt = cameraTargets_[static_cast<std::size_t>(index)].get();
    device.SetRenderTarget(rt);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f, 0);
    const Matrix view = Matrix::CreateLookAt(cam.eye, cam.target, Vector3::Up);
    const Matrix proj = Matrix::CreatePerspectiveFieldOfView(MathHelper::ToRadians(62.0f), static_cast<float>(kCamW) / static_cast<float>(kCamH), 0.2f, 220.0f);
    const SceneView sv(view, proj, cam.eye);
    DrawContext ctx{device, fx, sv, time, true};
    fx.SetCamera(view, proj);
    RenderStates::SetOpaque3D(device);
    fx.World().setFogEnabledProperty(false);
    sky.Draw(ctx);
    world.Draw(ctx, light, sim.Doors());
    NpcDrawStats dummy;
    npcs.Draw(ctx, sim, world, dummy);
    device.SetRenderTarget(nullptr);
    cameraRendered_[static_cast<std::size_t>(index)] = true;
    ++rendered_;
}

void Screens::DrawQuads(const DrawContext& ctx, const std::vector<Quad>& quads)
{
    if (quads.empty()) return;
    BasicEffect& fx = ctx.fx.World();
    fx.setWorldProperty(Matrix::getIdentityProperty());
    fx.setViewProperty(ctx.view.view);
    fx.setProjectionProperty(ctx.view.projection);
    fx.setDiffuseColorProperty(Vector3(1, 1, 1));
    RenderStates::SetOpaque3D(ctx.device);
    ctx.device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
    for (const Quad& q : quads) {
        if (q.texture == nullptr) continue;
        scratch_.clear();
        const Color c = Color(255, 255, 255, 255);
        const Vector3 a = q.centre - q.right + q.up, b = q.centre + q.right + q.up, cc = q.centre + q.right - q.up, d = q.centre - q.right - q.up;
        // v = 0 is the top edge; u runs from the viewer's left (local +x, "b") to the right (local -x, "a").
        scratch_.emplace_back(a, c, Vector2(1, 0));
        scratch_.emplace_back(b, c, Vector2(0, 0));
        scratch_.emplace_back(cc, c, Vector2(0, 1));
        scratch_.emplace_back(a, c, Vector2(1, 0));
        scratch_.emplace_back(cc, c, Vector2(0, 1));
        scratch_.emplace_back(d, c, Vector2(1, 1));
        fx.setTextureProperty(q.texture);
        EffectBank::Apply(fx);
        ctx.device.DrawUserPrimitives(PrimitiveType::TriangleList, scratch_.data(), 0, 2);
    }
}

void Screens::Draw(const DrawContext& ctx, const School::SchoolSim& sim, const WorldRenderer& world)
{
    std::vector<Quad> quads;
    const auto panelAxes = [](float yaw, float w, float h, Vector3& right, Vector3& up, Vector3& normal) {
        // yaw convention: the panel faces (-sin yaw, 0, -cos yaw); its width runs along local +x = (cos yaw, 0, -sin yaw).
        // Seen from the front the viewer's right is local -x, so DrawQuads maps u = 1 to the -x corners.
        right = Vector3(std::cos(yaw), 0, -std::sin(yaw)) * (w * 0.5f);
        up = Vector3(0, h * 0.5f, 0);
        normal = Vector3(-std::sin(yaw), 0, -std::cos(yaw));
    };
    // CCTV walls: a 2 x 2 grid of camera views.
    for (const Wall& w : walls_) {
        if (Vector3::DistanceSquared(w.centre, ctx.view.eye) > 60.0f * 60.0f) continue;
        Vector3 right, up, n;
        panelAxes(w.yaw, w.width, w.height, right, up, n);
        const int shown = std::min(4, static_cast<int>(cameras_.size()));
        for (int k = 0; k < shown; ++k) {
            const int index = (firstShown_ + k) % static_cast<int>(cameras_.size());
            if (!cameraRendered_[static_cast<std::size_t>(index)]) continue;
            const float cx = (k % 2 == 0 ? -0.5f : 0.5f), cy = (k < 2 ? 0.5f : -0.5f);
            Quad q;
            q.centre = w.centre + right * cx + up * cy + n * (w.depth * 0.5f + 0.012f);
            q.right = right * 0.47f;
            q.up = up * 0.46f;
            q.texture = cameraTargets_[static_cast<std::size_t>(index)].get();
            quads.push_back(q);
        }
    }
    // Signage.
    if (signageAge_ < 30.0f)
        for (const Sign& s : signs_) {
            if (Vector3::DistanceSquared(s.centre, ctx.view.eye) > kSignRange * kSignRange) continue;
            Vector3 right, up, n;
            panelAxes(s.yaw, s.width, s.height, right, up, n);
            Quad q;
            q.centre = s.centre + n * 0.02f;
            q.right = right;
            q.up = up;
            q.texture = signage_.get();
            quads.push_back(q);
        }
    // Whiteboards of rooms with a lesson going on.
    for (const School::RoomId id : world.LoadedRooms()) {
        const Room& r = plan_.room(id);
        if (!School::Info(r.type).teaching || r.type == RoomType::Gym) continue;
        const School::Vec2 c = r.rect.Center();
        if (Vector3::DistanceSquared(Vector3(c.x, plan_.FloorY(r.level), c.z), ctx.view.eye) > 40.0f * 40.0f) continue;
        const School::SchoolSim::RoomLesson lesson = sim.LessonIn(id);
        if (!lesson.valid || sim.OccupantsOf(id) == 0) continue;
        for (const School::Fixture& f : layouts_.Get(id).fixtures) {
            if (f.type != School::FixtureType::Whiteboard) continue;
            Vector3 right, up, n;
            panelAxes(f.yaw, f.sx - 0.08f, f.sy - 0.08f, right, up, n);
            Quad q;
            q.centre = Vector3(f.pos.x, plan_.FloorY(r.level) + f.y0 + f.sy * 0.5f, f.pos.z) + n * (f.sz * 0.5f + 0.008f);
            q.right = right;
            q.up = up;
            q.texture = BoardFor(lesson.subject);
            q.emissive = 0.0f;
            quads.push_back(q);
        }
    }
    // Door plates: the room's code beside each of its doors, on the corridor side, within 25 m.
    const float plateRange2 = 25.0f * 25.0f;
    for (const School::RoomId id : world.LoadedRooms()) {
        const Room& r = plan_.room(id);
        if (!r.Indoor() || School::Info(r.type).circulation) continue;
        for (const School::DoorId did : r.doors) {
            const School::Door& d = plan_.door(did);
            if (d.kind == School::DoorKind::Opening) continue;
            const Room& other = plan_.room(d.Other(id));
            if (!School::Info(other.type).circulation || !other.Indoor()) continue;
            const float y0 = plan_.FloorY(d.level);
            School::Vec2 n = d.Normal();
            if (d.a != id) n = n * -1.0f;   // now points from this room into the corridor
            const School::Vec2 along = n.RightHand();
            const School::Vec2 at = d.pos + n * 0.012f + along * (d.width * 0.5f + 0.32f);
            const Vector3 centre(at.x, y0 + 1.6f, at.z);
            if (Vector3::DistanceSquared(centre, ctx.view.eye) > plateRange2) continue;
            const float yaw = std::atan2(-n.x, -n.z);
            Vector3 right, up, nn;
            panelAxes(yaw, 0.42f, 0.14f, right, up, nn);
            Quad q;
            q.centre = centre;
            q.right = right;
            q.up = up;
            q.texture = PlateFor(id);
            quads.push_back(q);
        }
    }
    // Computer lab monitors show the desktop.
    if (desktopAge_ < 30.0f)
        for (const School::RoomId id : world.LoadedRooms()) {
            const Room& r = plan_.room(id);
            if (r.type != RoomType::ComputerLab) continue;
            const School::Vec2 c = r.rect.Clamp(School::Vec2{ctx.view.eye.X, ctx.view.eye.Z});
            if ((c.x - ctx.view.eye.X) * (c.x - ctx.view.eye.X) + (c.z - ctx.view.eye.Z) * (c.z - ctx.view.eye.Z) > 20.0f * 20.0f) continue;
            for (const School::Fixture& f : layouts_.Get(id).fixtures) {
                if (f.type != School::FixtureType::ComputerDesk) continue;
                const Matrix frame = Matrix::CreateRotationY(f.yaw) * Matrix::CreateTranslation(f.pos.x, plan_.FloorY(r.level) + f.y0, f.pos.z);
                Vector3 right, up, n;
                panelAxes(f.yaw, 0.42f, 0.27f, right, up, n);
                Quad q;
                q.centre = Vector3::Transform(Vector3(0.0f, f.sy + 0.26f, f.sz * 0.5f - 0.2f - 0.019f), frame) + n * 0.003f;
                q.right = right;
                q.up = up;
                q.texture = desktop_.get();
                quads.push_back(q);
            }
        }
    // Elevator floor indicators above every hall door.
    for (std::size_t i = 0; i < plan_.elevators.size() && i < indicators_.size(); ++i) {
        if (indicators_[i].shownLevel == -99) continue;
        const School::ElevatorShaft& e = plan_.elevators[i];
        for (const School::RoomId cab : e.cab) {
            if (cab == School::kNone || !world.IsLoaded(cab)) continue;
            const Room& r = plan_.room(cab);
            for (const School::DoorId did : r.doors) {
                const School::Door& d = plan_.door(did);
                const School::Vec2 n = School::SideNormal(e.entrySide);
                const Vector3 centre(d.pos.x + n.x * 0.03f, plan_.FloorY(r.level) + School::kDoorHeight + 0.32f, d.pos.z + n.z * 0.03f);
                if (Vector3::DistanceSquared(centre, ctx.view.eye) > 30.0f * 30.0f) continue;
                Vector3 right, up, nn;
                panelAxes(std::atan2(-n.x, -n.z), 0.4f, 0.2f, right, up, nn);
                Quad q;
                q.centre = centre;
                q.right = right;
                q.up = up;
                q.texture = indicators_[i].target.get();
                quads.push_back(q);
            }
        }
    }
    // Diagnostics monitors.
    if (diagnosticsAge_ < 30.0f)
        for (const Monitor& m : monitors_) {
            if (Vector3::DistanceSquared(m.centre, ctx.view.eye) > 14.0f * 14.0f) continue;
            Vector3 right, up, n;
            panelAxes(m.yaw, 0.42f, 0.27f, right, up, n);
            Quad q;
            q.centre = m.centre + n * 0.003f;
            q.right = right;
            q.up = up;
            q.texture = diagnostics_.get();
            quads.push_back(q);
        }
    // The auditorium's projection screen shows the signage.
    if (signageAge_ < 30.0f)
        for (const School::RoomId id : world.LoadedRooms()) {
            const Room& r = plan_.room(id);
            if (r.type != RoomType::Auditorium) continue;
            for (const School::Fixture& f : layouts_.Get(id).fixtures) {
                if (f.type != School::FixtureType::Screen) continue;
                Vector3 right, up, n;
                panelAxes(f.yaw, f.sx - 0.3f, f.sy - 0.3f, right, up, n);
                Quad q;
                q.centre = Vector3(f.pos.x, plan_.FloorY(r.level) + f.y0 + f.sy * 0.5f, f.pos.z) + n * (f.sz * 0.5f + 0.012f);
                q.right = right;
                q.up = up;
                q.texture = signage_.get();
                quads.push_back(q);
            }
        }
    DrawQuads(ctx, quads);
}
