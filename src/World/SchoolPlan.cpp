#include "World/SchoolPlan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace School {

namespace {
// name, indoor, circulation, teaching
constexpr RoomTypeInfo kInfo[] = {
    {"Corridor", true, true, false},
    {"Lobby", true, true, false},
    {"Stairwell", true, true, false},
    {"Elevator", true, true, false},
    {"Classroom", true, false, true},
    {"Language room", true, false, true},
    {"Computer lab", true, false, true},
    {"Physics lab", true, false, true},
    {"Chemistry lab", true, false, true},
    {"Biology lab", true, false, true},
    {"Art studio", true, false, true},
    {"Music room", true, false, true},
    {"Workshop", true, false, true},
    {"Gymnasium", true, false, true},
    {"Library", true, false, true},
    {"Auditorium", true, false, true},
    {"Cafeteria", true, false, false},
    {"Drama studio", true, false, true},
    {"Reception", true, false, false},
    {"Admin office", true, false, false},
    {"Principal's office", true, false, false},
    {"Staff room", true, false, false},
    {"Meeting room", true, false, false},
    {"Nurse", true, false, false},
    {"Counseling", true, false, false},
    {"Server room", true, false, false},
    {"Security office", true, false, false},
    {"Kitchen", true, false, false},
    {"Storage", true, false, false},
    {"Lab prep room", true, false, false},
    {"Boiler room", true, false, false},
    {"Electrical room", true, false, false},
    {"Toilets", true, false, false},
    {"Changing room", true, false, false},
    {"Locker hall", true, false, false},
    {"Janitor", true, false, false},
    {"Archive", true, false, false},
    {"Equipment store", true, false, false},
    {"Fitness room", true, false, false},
    {"Courtyard", false, false, false},
    {"Playground", false, false, false},
    {"Running track", false, false, false},
    {"Football field", false, false, false},
    {"Parking", false, false, false},
    {"Bicycle area", false, false, false},
    {"Plaza", false, false, false},
    {"Assembly point", false, false, false},
    {"Street", false, false, false},
};
static_assert(sizeof(kInfo) / sizeof(kInfo[0]) == static_cast<std::size_t>(RoomType::Count), "RoomTypeInfo table out of step");
}  // namespace

const RoomTypeInfo& Info(RoomType type)
{
    return kInfo[static_cast<std::size_t>(type)];
}

int SchoolPlan::LevelAt(float y) const
{
    int best = 0;
    for (const Floor& f : floors)
        if (f.baseY <= y + 0.6f) best = f.level;
    return best;
}

const std::vector<RoomId>* SchoolPlan::Cell(Vec2 p) const
{
    const int cx = static_cast<int>(std::floor((p.x - grid_.minX) / grid_.cell));
    const int cz = static_cast<int>(std::floor((p.z - grid_.minZ) / grid_.cell));
    if (cx < 0 || cz < 0 || cx >= grid_.nx || cz >= grid_.nz) return nullptr;
    return &grid_.cells[static_cast<std::size_t>(cz * grid_.nx + cx)];
}

RoomId SchoolPlan::RoomAt(Vec2 p, int level, float margin) const
{
    const std::vector<RoomId>* cell = Cell(p);
    if (cell == nullptr) return kNone;
    RoomId best = kNone;
    float bestArea = std::numeric_limits<float>::max();
    for (const RoomId id : *cell) {
        const Room& r = room(id);
        if (level < r.level || level > r.TopLevel()) continue;
        if (!r.rect.Contains(p, margin)) continue;
        if (r.rect.Area() < bestArea) {
            best = id;
            bestArea = r.rect.Area();
        }
    }
    return best;
}

float SchoolPlan::GroundHeight(Vec2 p, float refY) const
{
    constexpr float kStepUp = 0.6f;
    float bestBelow = -1e9f;     // highest candidate not above refY + step
    float nearest = refY;
    float nearestDist = 1e9f;
    const auto consider = [&](float y) {
        if (y <= refY + kStepUp && y > bestBelow) bestBelow = y;
        const float d = std::fabs(y - refY);
        if (d < nearestDist) {
            nearestDist = d;
            nearest = y;
        }
    };
    for (const WalkSurface& s : stairSurfaces)
        if (s.rect.Contains(p)) consider(s.HeightAt(p));
    for (int level = 0; level < LevelCount(); ++level) {
        const RoomId id = RoomAt(p, level, 0.01f);
        if (id == kNone) continue;
        const Room& r = room(id);
        if (r.type == RoomType::StairWell) continue;   // its floor is entirely in stairSurfaces
        if (level != r.level) continue;                // a tall room has one floor, at its lowest level
        consider(FloorY(level));
    }
    return bestBelow > -1e8f ? bestBelow : nearest;
}

std::vector<RoomId> SchoolPlan::RoomsOfType(RoomType type) const
{
    std::vector<RoomId> out;
    for (const Room& r : rooms)
        if (r.type == type) out.push_back(r.id);
    return out;
}

void SchoolPlan::BuildIndex()
{
    for (Room& r : rooms) r.doors.clear();
    for (const Door& d : doors) {
        room(d.a).doors.push_back(d.id);
        room(d.b).doors.push_back(d.id);
    }

    // Lookup grid over everything.
    Rect bounds = rooms.empty() ? Rect(0, 0, 1, 1) : rooms.front().rect;
    for (const Room& r : rooms) bounds = bounds.United(r.rect);
    campus = bounds;
    grid_.minX = bounds.x0;
    grid_.minZ = bounds.z0;
    grid_.nx = static_cast<int>(std::ceil(bounds.Width() / grid_.cell)) + 1;
    grid_.nz = static_cast<int>(std::ceil(bounds.Depth() / grid_.cell)) + 1;
    grid_.cells.assign(static_cast<std::size_t>(grid_.nx * grid_.nz), {});
    for (const Room& r : rooms) {
        const int x0 = static_cast<int>(std::floor((r.rect.x0 - grid_.minX) / grid_.cell));
        const int x1 = static_cast<int>(std::floor((r.rect.x1 - grid_.minX) / grid_.cell));
        const int z0 = static_cast<int>(std::floor((r.rect.z0 - grid_.minZ) / grid_.cell));
        const int z1 = static_cast<int>(std::floor((r.rect.z1 - grid_.minZ) / grid_.cell));
        for (int z = std::max(0, z0); z <= std::min(grid_.nz - 1, z1); ++z)
            for (int x = std::max(0, x0); x <= std::min(grid_.nx - 1, x1); ++x)
                grid_.cells[static_cast<std::size_t>(z * grid_.nx + x)].push_back(r.id);
    }

    // Exterior sides: a wall with no other indoor room against it (over most of its length) has windows.
    for (Room& r : rooms) {
        r.exteriorSides = 0;
        if (!r.Indoor() || r.type == RoomType::StairWell) {
            // Stairwells have windows too, but their walls are drawn by the stair builder; treat like others.
        }
        if (!r.Indoor()) continue;
        for (int s = 0; s < 4; ++s) {
            const Side side = static_cast<Side>(s);
            const Vec2 n = SideNormal(side);
            const bool alongX = (side == Side::North || side == Side::South);
            const float length = alongX ? r.rect.Width() : r.rect.Depth();
            const int samples = std::max(2, static_cast<int>(length / 0.75f));
            int outside = 0;
            for (int i = 0; i < samples; ++i) {
                const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(samples);
                Vec2 on;
                switch (side) {
                case Side::North: on = {r.rect.x0 + t * length, r.rect.z0}; break;
                case Side::South: on = {r.rect.x0 + t * length, r.rect.z1}; break;
                case Side::West: on = {r.rect.x0, r.rect.z0 + t * length}; break;
                default: on = {r.rect.x1, r.rect.z0 + t * length}; break;
                }
                const Vec2 probe = on + n * 0.3f;
                bool covered = false;
                for (int level = r.level; level <= r.TopLevel() && !covered; ++level) {
                    const RoomId other = RoomAt(probe, level);
                    covered = other != kNone && other != r.id && room(other).Indoor();
                }
                if (!covered) ++outside;
            }
            if (outside * 2 > samples) r.exteriorSides = static_cast<std::uint8_t>(r.exteriorSides | (1 << s));
        }
    }
}

PlanStats ComputeStats(const SchoolPlan& plan)
{
    PlanStats s;
    s.rooms = static_cast<int>(plan.rooms.size());
    s.doors = static_cast<int>(plan.doors.size());
    s.stairs = static_cast<int>(plan.stairs.size());
    s.elevators = static_cast<int>(plan.elevators.size());
    for (const Room& r : plan.rooms) {
        ++s.perType[static_cast<int>(r.type)];
        if (r.Indoor()) {
            ++s.indoorRooms;
            s.indoorAreaM2 += r.rect.Area() * static_cast<float>(r.levels);
            if (r.level >= 0 && r.level < 16) ++s.perLevel[r.level];
        } else {
            ++s.outdoorZones;
        }
        if (r.type == RoomType::Corridor) ++s.corridors;
    }
    return s;
}

}  // namespace School
