#include "UI/Minimap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Rendering/TextureLibrary.hpp"
#include "Simulation/SchoolSim.hpp"
#include "World/SchoolPlan.hpp"

using namespace Microsoft::Xna::Framework;
using School::Room;
using School::RoomType;

namespace {

Color RoomColour(const School::SchoolPlan& plan, const Room& r)
{
    const School::Wing& w = plan.wings[static_cast<std::size_t>(r.wing)];
    const auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    switch (r.type) {
    case RoomType::Corridor:
    case RoomType::Lobby: return Color(b(w.wall[0] * 0.95f), b(w.wall[1] * 0.95f), b(w.wall[2] * 0.95f), 230);
    case RoomType::StairWell: return Color(120, 120, 130, 230);
    case RoomType::Toilets:
    case RoomType::ChangingRoom: return Color(150, 190, 210, 230);
    case RoomType::Gym:
    case RoomType::FitnessRoom: return Color(240, 160, 110, 230);
    case RoomType::Cafeteria:
    case RoomType::Kitchen: return Color(240, 200, 120, 230);
    case RoomType::Library: return Color(200, 170, 120, 230);
    case RoomType::Auditorium:
    case RoomType::DramaStudio:
    case RoomType::MusicRoom: return Color(190, 130, 170, 230);
    case RoomType::PhysicsLab:
    case RoomType::ChemistryLab:
    case RoomType::BiologyLab:
    case RoomType::LabPrep: return Color(140, 210, 160, 230);
    case RoomType::ComputerLab:
    case RoomType::ServerRoom: return Color(140, 170, 230, 230);
    case RoomType::ArtStudio:
    case RoomType::Workshop: return Color(230, 190, 150, 230);
    case RoomType::Courtyard:
    case RoomType::AssemblyPoint: return Color(120, 180, 100, 200);
    case RoomType::FootballField:
    case RoomType::Track: return Color(90, 160, 80, 200);
    case RoomType::Playground: return Color(210, 190, 140, 200);
    case RoomType::Parking:
    case RoomType::Street: return Color(90, 90, 95, 200);
    case RoomType::Plaza:
    case RoomType::BikeArea: return Color(160, 160, 150, 200);
    default:
        if (School::Info(r.type).teaching) return Color(b(w.accent[0] * 0.5f + 0.45f), b(w.accent[1] * 0.5f + 0.45f), b(w.accent[2] * 0.5f + 0.45f), 230);
        return Color(190, 190, 185, 230);
    }
}

}  // namespace

void Minimap::Draw(const MapView& view, const Rectangle& panel, const School::SchoolSim& sim, const WorldRenderer* world)
{
    const School::SchoolPlan& plan = *s_.plan;
    hud_.Panel(panel.X, panel.Y, panel.Width, panel.Height);
    const int px = panel.X + 4, py = panel.Y + 4, pw = panel.Width - 8, ph = panel.Height - 8;
    const float scale = view.follow ? static_cast<float>(pw) / view.metresAcross : std::min(static_cast<float>(pw) / (plan.campus.Width() + 10.0f), static_cast<float>(ph) / (plan.campus.Depth() + 10.0f));
    const float cx = view.follow ? view.player.X : plan.campus.Center().x;
    const float cz = view.follow ? view.player.Z : plan.campus.Center().z;
    const auto toScreen = [&](float x, float z, int& sx, int& sy) {
        sx = px + pw / 2 + static_cast<int>((x - cx) * scale);
        sy = py + ph / 2 + static_cast<int>((z - cz) * scale);
    };
    const auto clipRect = [&](int x0, int y0, int x1, int y1, const Color& c) {
        x0 = std::max(x0, px);
        y0 = std::max(y0, py);
        x1 = std::min(x1, px + pw);
        y1 = std::min(y1, py + ph);
        if (x1 > x0 && y1 > y0) hud_.Rect(x0, y0, x1 - x0, y1 - y0, c);
    };
    hud_.Rect(px, py, pw, ph, Color(20, 26, 36, 255));
    for (const Room& r : plan.rooms) {
        const bool onLevel = view.level >= r.level && view.level <= r.TopLevel();
        if (!onLevel && !(r.Indoor() == false && view.level == 1)) continue;
        int x0, y0, x1, y1;
        toScreen(r.rect.x0, r.rect.z0, x0, y0);
        toScreen(r.rect.x1, r.rect.z1, x1, y1);
        if (x1 < px || x0 > px + pw || y1 < py || y0 > py + ph) continue;
        Color c = RoomColour(plan, r);
        if (r.id == view.currentRoom) c = Color(255, 240, 180, 255);
        clipRect(x0 + 1, y0 + 1, x1, y1, c);
        if (view.showLoaded && world != nullptr && world->IsLoaded(r.id)) {
            clipRect(x0, y0, x1, y0 + 1, Color(90, 255, 120, 255));
            clipRect(x0, y1 - 1, x1, y1, Color(90, 255, 120, 255));
        }
    }
    // Doors as light gaps on the wall line.
    for (const School::Door& d : plan.doors) {
        if (d.level != view.level || d.kind == School::DoorKind::Opening) continue;
        int sx, sy;
        toScreen(d.pos.x, d.pos.z, sx, sy);
        const int w = std::max(2, static_cast<int>(d.width * scale));
        const bool alongX = d.sideOfA == School::Side::North || d.sideOfA == School::Side::South;
        clipRect(alongX ? sx - w / 2 : sx - 1, alongX ? sy - 1 : sy - w / 2, alongX ? sx + w / 2 : sx + 1, alongX ? sy + 1 : sy + w / 2, Color(255, 255, 255, 200));
    }
    if (view.showPeople) {
        const School::Population& pop = sim.Pop();
        int shown = 0;
        for (const School::Npc& n : pop.npcs) {
            const School::Agent& a = sim.AgentOf(n.id);
            if (a.mode == School::Mode::Offsite) continue;
            School::Vec3 p;
            float yaw;
            School::RoomId room;
            sim.Observe(n.id, p, yaw, room);
            const int level = plan.LevelAt(p.y);
            if (level != view.level && !(room != School::kNone && !plan.room(room).Indoor() && view.level == 1)) continue;
            int sx, sy;
            toScreen(p.x, p.z, sx, sy);
            if (sx < px || sx >= px + pw || sy < py || sy >= py + ph) continue;
            const Color c = n.role == School::Role::Student ? (a.mode == School::Mode::Transit ? Color(255, 120, 60, 255) : Color(60, 130, 255, 255)) : Color(255, 230, 60, 255);
            const int s = scale > 4.0f ? 3 : 2;
            clipRect(sx - s / 2, sy - s / 2, sx - s / 2 + s, sy - s / 2 + s, c);
            if (++shown > 4000) break;
        }
    }
    // The player: a dot with a short heading line.
    {
        int sx, sy;
        toScreen(view.player.X, view.player.Z, sx, sy);
        const float fx = -std::sin(view.playerYaw), fz = -std::cos(view.playerYaw);
        for (int i = 0; i < 8; ++i) clipRect(sx + static_cast<int>(fx * static_cast<float>(i)) - 1, sy + static_cast<int>(fz * static_cast<float>(i)) - 1, sx + static_cast<int>(fx * static_cast<float>(i)) + 1, sy + static_cast<int>(fz * static_cast<float>(i)) + 1, Color(255, 255, 255, 255));
        clipRect(sx - 3, sy - 3, sx + 3, sy + 3, Color(255, 60, 60, 255));
    }
    char label[64];
    const int floorLabel = view.level - 1;
    if (floorLabel < 0) std::snprintf(label, sizeof label, "BASEMENT");
    else std::snprintf(label, sizeof label, "FLOOR %d  %s", floorLabel, plan.floors[static_cast<std::size_t>(view.level)].name.c_str());
    hud_.Text(*s_.font, label, Vector2(static_cast<float>(px) + 6.0f, static_cast<float>(py + ph) - 22.0f), Color(255, 214, 90, 255), 0.6f);
}
