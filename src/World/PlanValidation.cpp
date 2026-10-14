#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "World/SchoolPlan.hpp"

namespace School {

namespace {

std::string Fmt(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    return buffer;
}

float WallCoordinate(const Rect& r, Side s)
{
    switch (s) {
    case Side::North: return r.z0;
    case Side::South: return r.z1;
    case Side::West: return r.x0;
    default: return r.x1;
    }
}

}  // namespace

std::vector<std::string> ValidatePlan(const SchoolPlan& plan)
{
    std::vector<std::string> errors;
    const auto fail = [&](std::string message) { errors.push_back(std::move(message)); };
    const int roomCount = static_cast<int>(plan.rooms.size());

    // --- rooms ---------------------------------------------------------------------------------------------
    std::set<std::string> codes;
    for (int i = 0; i < roomCount; ++i) {
        const Room& r = plan.rooms[static_cast<std::size_t>(i)];
        if (r.id != i) fail(Fmt("room index %d has id %d", i, r.id));
        if (!r.rect.Valid()) fail(Fmt("room %d (%s) has an empty rectangle", i, r.name.c_str()));
        if (r.name.empty() || r.code.empty()) fail(Fmt("room %d has no name or code", i));
        if (r.level < 0 || r.level >= plan.LevelCount() || r.TopLevel() >= plan.LevelCount())
            fail(Fmt("room %s is on a level that does not exist", r.name.c_str()));
        if (r.wing < 0 || r.wing >= static_cast<int>(plan.wings.size())) fail(Fmt("room %s has no wing", r.name.c_str()));
        if (!codes.insert(r.code).second) fail(Fmt("duplicate room code %s (%s)", r.code.c_str(), r.name.c_str()));
        if (r.capacity < 0) fail(Fmt("room %s has a negative capacity", r.name.c_str()));
        if (r.type == RoomType::StairWell && (r.stairId < 0 || r.stairId >= static_cast<int>(plan.stairs.size())))
            fail(Fmt("stairwell %s names no stair shaft", r.name.c_str()));
    }

    // --- overlaps: nothing may share floor area on the same level ------------------------------------------------------
    for (int i = 0; i < roomCount; ++i) {
        for (int j = i + 1; j < roomCount; ++j) {
            const Room& a = plan.rooms[static_cast<std::size_t>(i)];
            const Room& b = plan.rooms[static_cast<std::size_t>(j)];
            if (a.TopLevel() < b.level || b.TopLevel() < a.level) continue;
            if (a.type == RoomType::Street || b.type == RoomType::Street) continue;
            if (a.rect.Overlaps(b.rect)) fail(Fmt("rooms overlap: %s and %s", a.name.c_str(), b.name.c_str()));
        }
    }

    // --- doors ---------------------------------------------------------------------------------------------------
    for (int i = 0; i < static_cast<int>(plan.doors.size()); ++i) {
        const Door& d = plan.doors[static_cast<std::size_t>(i)];
        if (d.id != i) fail(Fmt("door index %d has id %d", i, d.id));
        if (d.a < 0 || d.a >= roomCount || d.b < 0 || d.b >= roomCount) {
            fail(Fmt("door %d refers to a room that does not exist (%d, %d)", i, d.a, d.b));
            continue;
        }
        const Room& a = plan.room(d.a);
        const Room& b = plan.room(d.b);
        if (d.a == d.b) fail(Fmt("door %d joins a room to itself (%s)", i, a.name.c_str()));
        if (d.width <= 0.3f || d.width > 12.0f) fail(Fmt("door %d has an unusable width %.2f", i, static_cast<double>(d.width)));
        if (d.level < a.level || d.level > a.TopLevel() || d.level < b.level || d.level > b.TopLevel())
            fail(Fmt("door %d (%s - %s) is on level %d outside one of its rooms", i, a.name.c_str(), b.name.c_str(), d.level));
        // The door must sit on a's wall, inside the shared part with b's opposite wall.
        const bool alongX = d.sideOfA == Side::North || d.sideOfA == Side::South;
        const float wallA = WallCoordinate(a.rect, d.sideOfA);
        const float wallB = WallCoordinate(b.rect, Opposite(d.sideOfA));
        const float fixedPos = alongX ? d.pos.z : d.pos.x;
        const float along = alongX ? d.pos.x : d.pos.z;
        if (std::fabs(wallA - fixedPos) > 0.01f) fail(Fmt("door %d is %.2f m off the wall of %s", i, static_cast<double>(std::fabs(wallA - fixedPos)), a.name.c_str()));
        if (std::fabs(wallA - wallB) > 0.01f)
            fail(Fmt("door %d: %s and %s do not touch (walls %.2f vs %.2f)", i, a.name.c_str(), b.name.c_str(), static_cast<double>(wallA), static_cast<double>(wallB)));
        const float loA = alongX ? a.rect.x0 : a.rect.z0, hiA = alongX ? a.rect.x1 : a.rect.z1;
        const float loB = alongX ? b.rect.x0 : b.rect.z0, hiB = alongX ? b.rect.x1 : b.rect.z1;
        const float half = d.width * 0.5f;
        if (along - half < std::max(loA, loB) - 0.01f || along + half > std::min(hiA, hiB) + 0.01f)
            fail(Fmt("door %d (%s - %s) is not within the wall the rooms share", i, a.name.c_str(), b.name.c_str()));
        if (d.exit && a.Indoor() == b.Indoor()) fail(Fmt("door %d is an exit but does not lead outdoors (%s - %s)", i, a.name.c_str(), b.name.c_str()));
        bool listedA = false, listedB = false;
        for (const DoorId x : a.doors) listedA |= x == i;
        for (const DoorId x : b.doors) listedB |= x == i;
        if (!listedA || !listedB) fail(Fmt("door %d is not in the door lists of both rooms", i));
    }
    // Two doors on the same wall must not overlap.
    for (const Room& r : plan.rooms) {
        for (std::size_t x = 0; x < r.doors.size(); ++x) {
            for (std::size_t y = x + 1; y < r.doors.size(); ++y) {
                const Door& d1 = plan.door(r.doors[x]);
                const Door& d2 = plan.door(r.doors[y]);
                if (d1.level != d2.level) continue;
                // Only doors in the same wall can collide (a corridor's end opening is not in its side walls).
                const Room& a1 = plan.room(d1.a);
                const Room& a2 = plan.room(d2.a);
                const bool alongX1 = d1.sideOfA == Side::North || d1.sideOfA == Side::South;
                const bool alongX2 = d2.sideOfA == Side::North || d2.sideOfA == Side::South;
                if (alongX1 != alongX2) continue;
                const float fixed1 = alongX1 ? d1.pos.z : d1.pos.x;
                const float fixed2 = alongX2 ? d2.pos.z : d2.pos.x;
                if (std::fabs(fixed1 - fixed2) > 0.05f) continue;
                (void)a1; (void)a2;
                const float p1 = alongX1 ? d1.pos.x : d1.pos.z;
                const float p2 = alongX2 ? d2.pos.x : d2.pos.z;
                if (std::fabs(p1 - p2) < (d1.width + d2.width) * 0.5f)
                    fail(Fmt("doors %d and %d overlap in %s", d1.id, d2.id, r.name.c_str()));
            }
        }
    }

    // --- every room is reachable by a door of its own -----------------------------------------------------------------------
    for (const Room& r : plan.rooms) {
        if (r.id == plan.offsite) continue;
        if (r.doors.empty()) fail(Fmt("room %s has no door", r.name.c_str()));
    }

    // --- stairs ----------------------------------------------------------------------------------------------------------
    for (const StairShaft& s : plan.stairs) {
        if (s.levelHigh <= s.levelLow) fail(Fmt("stair shaft %d serves a single level", s.id));
        if (static_cast<int>(s.landing.size()) != s.levelHigh - s.levelLow + 1) fail(Fmt("stair shaft %d has a wrong landing count", s.id));
        for (std::size_t i = 0; i < s.landing.size(); ++i) {
            const RoomId id = s.landing[i];
            if (id == kNone) {
                fail(Fmt("stair shaft %d has no landing on level %d", s.id, s.levelLow + static_cast<int>(i)));
                continue;
            }
            const Room& r = plan.room(id);
            if (r.type != RoomType::StairWell || r.stairId != s.id || r.level != s.levelLow + static_cast<int>(i))
                fail(Fmt("stair shaft %d landing %zu is inconsistent", s.id, i));
            bool entryDoor = false;
            for (const DoorId d : r.doors) {
                const Door& door = plan.door(d);
                if (door.a == id && door.sideOfA == s.entrySide) entryDoor = true;
            }
            if (!entryDoor) fail(Fmt("stairwell %s has no door on its entry side (%s)", r.name.c_str(), SideName(s.entrySide)));
        }
    }

    // --- campus ---------------------------------------------------------------------------------------------------------------
    if (plan.offsite == kNone) fail("the plan has no off-campus street");
    if (plan.mainLobby == kNone) fail("the plan has no main lobby");
    if (plan.assemblyPoints.empty()) fail("the plan has no assembly point for a fire drill");
    for (const RoomId a : plan.assemblyPoints)
        if (a < 0 || a >= roomCount || plan.room(a).type != RoomType::AssemblyPoint) fail("an assembly point is not an assembly-point room");
    return errors;
}

}  // namespace School
