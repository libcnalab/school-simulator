// The school's floor plans, written as compact programs.
//
// A wing is a ring road of corridor segments with two strips of rooms along each corridor. A strip is
// described by a few *cores* (stairs, toilets, passages) that stay at the same place on every level, and
// short *programs* ("CL*" = fill with classrooms, "PH PP CH PP" = labs with prep rooms) for the gaps
// between them. Doors are not typed in: they are found by looking for the wall a room shares with a
// corridor, so the topology always follows the geometry (and ValidatePlan checks it does).
//
// Everything is in metres. The ring is 164 x 124 m with the courtyard in the middle; the sports block
// is attached to the west, the community block (cafeteria, auditorium, library) to the east.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>

#include "World/SchoolPlan.hpp"
#include "World/StairGeometry.hpp"

namespace School {

namespace {

enum WingId { kNorth = 0, kEast, kSouth, kWest, kSports, kCommunity, kCampus };

constexpr int kBasement = 0, kGround = 1, kTop = 5;

struct SlotCode {
    const char* code;
    RoomType type;
    float width;
};

constexpr SlotCode kSlots[] = {
    {"CL", RoomType::Classroom, 8.0f},     {"CS", RoomType::Classroom, 6.5f},   {"LA", RoomType::LanguageRoom, 8.0f},
    {"CO", RoomType::ComputerLab, 10.0f},  {"PH", RoomType::PhysicsLab, 12.0f}, {"CH", RoomType::ChemistryLab, 12.0f},
    {"BI", RoomType::BiologyLab, 12.0f},   {"AR", RoomType::ArtStudio, 12.0f},  {"MU", RoomType::MusicRoom, 10.0f},
    {"WO", RoomType::Workshop, 14.0f},     {"TO", RoomType::Toilets, 6.0f},     {"ST", RoomType::Storage, 4.0f},
    {"JA", RoomType::Janitor, 3.0f},       {"OF", RoomType::AdminOffice, 6.0f}, {"PR", RoomType::PrincipalOffice, 10.0f},
    {"RE", RoomType::Reception, 8.0f},     {"SR", RoomType::StaffRoom, 16.0f},  {"MT", RoomType::MeetingRoom, 10.0f},
    {"NU", RoomType::Nurse, 6.0f},         {"CN", RoomType::Counseling, 6.0f},  {"SV", RoomType::ServerRoom, 8.0f},
    {"SE", RoomType::SecurityOffice, 6.0f},{"PP", RoomType::LabPrep, 6.0f},     {"LK", RoomType::LockerHall, 12.0f},
    {"AC", RoomType::Archive, 6.0f},       {"BO", RoomType::Boiler, 14.0f},     {"EL", RoomType::Electrical, 6.0f},
    {"FI", RoomType::FitnessRoom, 10.0f},  {"EQ", RoomType::EquipmentStore, 8.0f}, {"CG", RoomType::ChangingRoom, 10.0f},
    {"DR", RoomType::DramaStudio, 14.0f},
};

const SlotCode* FindSlot(const std::string& code)
{
    for (const SlotCode& s : kSlots)
        if (code == s.code) return &s;
    return nullptr;
}

int DefaultCapacity(RoomType type, const Rect& r)
{
    const float a = r.Area();
    switch (type) {
    case RoomType::Classroom: return std::clamp(static_cast<int>(a / 2.3f), 12, 32);
    case RoomType::LanguageRoom: return std::clamp(static_cast<int>(a / 2.6f), 12, 28);
    case RoomType::ComputerLab: return std::clamp(static_cast<int>(a / 3.4f), 12, 30);
    case RoomType::PhysicsLab:
    case RoomType::ChemistryLab:
    case RoomType::BiologyLab: return std::clamp(static_cast<int>(a / 4.2f), 12, 30);
    case RoomType::ArtStudio: return std::clamp(static_cast<int>(a / 4.5f), 12, 28);
    case RoomType::MusicRoom: return std::clamp(static_cast<int>(a / 3.4f), 12, 40);
    case RoomType::Workshop: return std::clamp(static_cast<int>(a / 6.0f), 10, 30);
    case RoomType::Gym: return 120;
    case RoomType::Library: return static_cast<int>(a / 3.5f);
    case RoomType::Auditorium: return static_cast<int>(a / 1.8f);
    case RoomType::Cafeteria: return static_cast<int>(a / 1.8f);
    case RoomType::DramaStudio: return std::clamp(static_cast<int>(a / 3.5f), 12, 40);
    case RoomType::StaffRoom: return static_cast<int>(a / 3.0f);
    case RoomType::LockerHall: return static_cast<int>(a / 2.0f);
    case RoomType::ChangingRoom: return static_cast<int>(a / 1.5f);
    case RoomType::MeetingRoom: return static_cast<int>(a / 3.0f);
    case RoomType::Courtyard:
    case RoomType::Playground:
    case RoomType::Track:
    case RoomType::FootballField:
    case RoomType::Plaza:
    case RoomType::AssemblyPoint: return static_cast<int>(a / 1.5f);
    default: return std::max(1, static_cast<int>(a / 8.0f));
    }
}

class Builder {
public:
    SchoolPlan plan;

    Builder()
    {
        const char* names[] = {"Basement", "Ground floor", "First floor", "Second floor", "Third floor", "Fourth floor"};
        for (int i = 0; i < 6; ++i) plan.floors.push_back({i, names[i], static_cast<float>(i - 1) * kFloorHeight});

        // Wings: each gets its own palette so a corridor is recognisable at a glance.
        const auto wing = [&](const char* name, std::array<float, 3> wall, std::array<float, 3> accent, const char* floor) {
            Wing w;
            w.id = static_cast<int>(plan.wings.size());
            w.name = name;
            for (std::size_t i = 0; i < 3; ++i) {
                w.wall[i] = wall[i];
                w.accent[i] = accent[i];
            }
            w.corridorFloor = floor;
            plan.wings.push_back(w);
        };
        wing("North Wing (Humanities)", {0.80f, 0.88f, 0.98f}, {0.15f, 0.35f, 0.80f}, "floor_tile");
        wing("East Wing (Science)", {0.80f, 0.95f, 0.82f}, {0.10f, 0.60f, 0.30f}, "floor_terrazzo");
        wing("South Wing (Main Building)", {0.98f, 0.88f, 0.76f}, {0.85f, 0.40f, 0.12f}, "floor_wood");
        wing("West Wing (Languages & Mathematics)", {0.90f, 0.84f, 0.96f}, {0.50f, 0.25f, 0.70f}, "floor_tile");
        wing("Sports Block", {0.98f, 0.82f, 0.80f}, {0.80f, 0.15f, 0.15f}, "floor_concrete");
        wing("Community Block", {0.98f, 0.94f, 0.76f}, {0.90f, 0.70f, 0.10f}, "floor_wood");
        wing("Campus", {0.85f, 0.85f, 0.85f}, {0.4f, 0.4f, 0.4f}, "floor_concrete");
    }

    // --- rooms -------------------------------------------------------------------------------
    RoomId AddRoom(int level, int wing, RoomType type, const Rect& rect, int levels = 1, const char* nameOverride = nullptr)
    {
        Room r;
        r.id = static_cast<RoomId>(plan.rooms.size());
        r.level = level;
        r.levels = levels;
        r.wing = wing;
        r.type = type;
        r.rect = rect;
        r.capacity = DefaultCapacity(type, rect);
        static const char kLetter[] = {'N', 'E', 'S', 'W', 'G', 'C', 'O'};
        const int number = ++counters_[{wing, level}];
        char code[24];
        const int floorLabel = level - 1;   // ground = 0, basement = -1
        if (Info(type).indoor) {
            if (floorLabel < 0) std::snprintf(code, sizeof code, "%cB.%02d", kLetter[wing], number);
            else std::snprintf(code, sizeof code, "%c%d.%02d", kLetter[wing], floorLabel, number);
            r.code = code;
            r.name = std::string(nameOverride != nullptr ? nameOverride : TypeName(type)) + " " + r.code;
        } else {
            std::snprintf(code, sizeof code, "O.%02d", number);
            r.code = code;
            r.name = nameOverride != nullptr ? nameOverride : TypeName(type);
        }
        plan.rooms.push_back(std::move(r));
        return plan.rooms.back().id;
    }

    Room& R(RoomId id) { return plan.room(id); }

    // --- doors -------------------------------------------------------------------------------
    struct Wall {
        float fixed;     // the coordinate that does not change along the wall
        float lo, hi;    // the interval along it
        bool alongX;
    };
    static Wall WallOf(const Rect& r, Side s)
    {
        switch (s) {
        case Side::North: return {r.z0, r.x0, r.x1, true};
        case Side::South: return {r.z1, r.x0, r.x1, true};
        case Side::West: return {r.x0, r.z0, r.z1, false};
        default: return {r.x1, r.z0, r.z1, false};
        }
    }

    /** Puts a door in wall `side` of room a, as close to `preferred` (a coordinate along the wall) as a neighbour allows. */
    DoorId PlaceDoor(RoomId aId, Side side, float preferred, DoorKind kind, float width, RoomId onlyTo = kNone, bool exitDoor = false,
                     bool outdoorOnly = false)
    {
        const Room a = R(aId);
        const Wall wa = WallOf(a.rect, side);
        const Side opp = Opposite(side);
        float bestPos = 0.0f;
        float bestScore = 1e9f;
        RoomId bestNeighbour = kNone;
        const float half = width * 0.5f + 0.25f;
        for (const Room& b : plan.rooms) {
            if (b.id == aId || (onlyTo != kNone && b.id != onlyTo) || (outdoorOnly && b.Indoor())) continue;
            if (a.level < b.level || a.level > b.TopLevel()) continue;
            const Wall wb = WallOf(b.rect, opp);
            if (std::fabs(wb.fixed - wa.fixed) > 1e-3f) continue;
            const float lo = std::max(wa.lo, wb.lo) + half;
            const float hi = std::min(wa.hi, wb.hi) - half;
            if (hi < lo) continue;
            for (float p = lo; p <= hi + 1e-4f; p += 0.1f) {
                if (DoorConflicts(a, b, side, p, width)) continue;
                const float score = std::fabs(p - preferred);
                if (score < bestScore) {
                    bestScore = score;
                    bestPos = p;
                    bestNeighbour = b.id;
                }
            }
        }
        if (bestNeighbour == kNone) return kNone;
        return AddDoor(aId, bestNeighbour, side, bestPos, kind, width, exitDoor);
    }

    DoorId AddDoor(RoomId a, RoomId b, Side sideOfA, float along, DoorKind kind, float width, bool exitDoor)
    {
        Door d;
        d.id = static_cast<DoorId>(plan.doors.size());
        d.a = a;
        d.b = b;
        d.level = R(a).level;
        d.sideOfA = sideOfA;
        const Wall w = WallOf(R(a).rect, sideOfA);
        d.pos = w.alongX ? Vec2{along, w.fixed} : Vec2{w.fixed, along};
        d.width = width;
        d.kind = kind;
        d.exit = exitDoor;
        d.hingeLeft = (plan.doors.size() % 2) == 0;
        plan.doors.push_back(d);
        R(a).doors.push_back(d.id);
        R(b).doors.push_back(d.id);
        return d.id;
    }

    bool DoorConflicts(const Room& a, const Room& b, Side sideOfA, float p, float width) const
    {
        const Wall wa = WallOf(a.rect, sideOfA);
        for (const RoomId rid : {a.id, b.id}) {
            for (const DoorId did : plan.room(rid).doors) {
                const Door& d = plan.door(did);
                const Wall wd = WallOf(plan.room(d.a).rect, d.sideOfA);
                if (std::fabs(wd.fixed - wa.fixed) > 1e-3f || wd.alongX != wa.alongX) continue;
                const float other = wd.alongX ? d.pos.x : d.pos.z;
                if (std::fabs(other - p) < (width + d.width) * 0.5f + 0.5f) return true;
            }
        }
        return false;
    }

    bool HasDoorBetween(RoomId a, RoomId b) const
    {
        for (const DoorId d : plan.room(a).doors)
            if (plan.door(d).Other(a) == b) return true;
        return false;
    }

    /** Joins every pair of touching circulation rooms (corridor segments, lobbies) with an opening. */
    void ConnectCirculation(int level)
    {
        for (std::size_t i = 0; i < plan.rooms.size(); ++i) {
            for (std::size_t j = i + 1; j < plan.rooms.size(); ++j) {
                const Room a = plan.rooms[i];
                const Room b = plan.rooms[j];
                if (a.level != level || b.level != level) continue;
                if (!(Info(a.type).circulation && Info(b.type).circulation)) continue;
                if (a.type == RoomType::StairWell || b.type == RoomType::StairWell) continue;
                for (int s = 0; s < 4; ++s) {
                    const Side side = static_cast<Side>(s);
                    const Wall wa = WallOf(a.rect, side);
                    const Wall wb = WallOf(b.rect, Opposite(side));
                    if (std::fabs(wa.fixed - wb.fixed) > 1e-3f) continue;
                    const float lo = std::max(wa.lo, wb.lo);
                    const float hi = std::min(wa.hi, wb.hi);
                    if (hi - lo < 3.4f || HasDoorBetween(a.id, b.id)) continue;
                    AddDoor(a.id, b.id, side, (lo + hi) * 0.5f, DoorKind::Opening, std::min(hi - lo, 4.0f), false);
                }
            }
        }
    }

    // --- corridors ---------------------------------------------------------------------------
    /** A corridor cut into segments of roughly `target` metres; ConnectCirculation joins them end to end. */
    void AddCorridor(int level, int wing, const Rect& rect, bool alongX, float target = 18.0f)
    {
        const float length = alongX ? rect.Width() : rect.Depth();
        const int n = std::max(1, static_cast<int>(std::lround(length / target)));
        for (int i = 0; i < n; ++i) {
            const float t0 = length * static_cast<float>(i) / static_cast<float>(n);
            const float t1 = length * static_cast<float>(i + 1) / static_cast<float>(n);
            const Rect seg = alongX ? Rect(rect.x0 + t0, rect.z0, rect.x0 + t1, rect.z1) : Rect(rect.x0, rect.z0 + t0, rect.x1, rect.z0 + t1);
            AddRoom(level, wing, RoomType::Corridor, seg, 1, "Corridor");
        }
    }

    // --- strips ------------------------------------------------------------------------------
    struct Core {
        float at;            // start coordinate along the strip
        float width;
        const char* code;    // a slot code, or "STAIR", "PASSAGE", "LINK", "VOID"
        int levelMask;       // bit per level where it exists
    };
    static constexpr int kAll = 0b111111;
    static int Levels(std::initializer_list<int> l)
    {
        int m = 0;
        for (const int v : l) m |= 1 << v;
        return m;
    }

    struct StripSpec {
        Rect area;
        Side doorSide;     // the side of every room that faces the corridor
        int wing;
        [[nodiscard]] bool AlongX() const { return doorSide == Side::North || doorSide == Side::South; }
    };

    /** Lays rooms in [from, to) along the strip from a program; fixed codes keep their width, "XX*" fills the rest. */
    void FillGap(int level, const StripSpec& strip, float from, float to, const std::string& program)
    {
        if (to - from < 1.0f) return;
        struct Item {
            const SlotCode* slot;
            int count;
            bool filler;
        };
        std::vector<Item> items;
        float fixedWidth = 0.0f;
        bool hasFiller = false;
        std::istringstream in(program);
        std::string tok;
        while (in >> tok) {
            std::string code = tok;
            int count = 1;
            bool filler = false;
            if (const std::size_t star = tok.find('*'); star != std::string::npos) {
                code = tok.substr(0, star);
                const std::string rest = tok.substr(star + 1);
                if (rest.empty()) filler = true;
                else count = std::atoi(rest.c_str());
            }
            const SlotCode* slot = FindSlot(code);
            if (slot == nullptr) continue;
            if (filler) {
                hasFiller = true;
                items.push_back({slot, 0, true});
            } else {
                items.push_back({slot, count, false});
                fixedWidth += slot->width * static_cast<float>(count);
            }
        }
        const float span = to - from;
        std::vector<std::pair<const SlotCode*, float>> placed;   // slot, width
        // A filler only counts when at least about half a room is left for it; otherwise the rest is scaled to fit.
        float fillerWidth = 0.0f;
        for (const Item& it : items)
            if (it.filler) fillerWidth = it.slot->width;
        const float rest = span - fixedWidth;
        if (hasFiller && rest >= fillerWidth * 0.6f) {
            for (const Item& it : items) {
                if (!it.filler) {
                    for (int i = 0; i < it.count; ++i) placed.emplace_back(it.slot, it.slot->width);
                } else {
                    const int n = std::max(1, static_cast<int>(std::lround(rest / it.slot->width)));
                    for (int i = 0; i < n; ++i) placed.emplace_back(it.slot, rest / static_cast<float>(n));
                }
            }
        } else {
            float total = 0.0f;
            for (const Item& it : items)
                if (!it.filler) total += it.slot->width * static_cast<float>(it.count);
            if (total <= 0.0f) {   // only a filler, and too little room: one room takes it all
                for (const Item& it : items) placed.emplace_back(it.slot, span);
            } else {
                const float scale = span / total;
                for (const Item& it : items)
                    if (!it.filler)
                        for (int i = 0; i < it.count; ++i) placed.emplace_back(it.slot, it.slot->width * scale);
            }
        }
        const bool alongX = strip.AlongX();
        float cursor = from;
        int index = 0;
        RoomId previous = kNone;
        for (std::size_t i = 0; i < placed.size(); ++i) {
            const SlotCode* slot = placed[i].first;
            const float end = i + 1 == placed.size() ? to : cursor + placed[i].second;
            const Rect rect = alongX ? Rect(cursor, strip.area.z0, end, strip.area.z1) : Rect(strip.area.x0, cursor, strip.area.x1, end);
            const RoomId id = AddRoom(level, strip.wing, slot->type, rect);
            // The door sits near one end, alternating, so a row of doors does not look like a ruler.
            const float pref = (index % 2) == 0 ? cursor + 1.6f : end - 1.6f;
            const bool wide = slot->type == RoomType::Workshop || slot->type == RoomType::LockerHall;
            PlaceDoor(id, strip.doorSide, pref, wide ? DoorKind::Double : DoorKind::Single, wide ? 1.7f : 1.1f);
            // A prep room is joined to the laboratory before it.
            if (slot->type == RoomType::LabPrep && previous != kNone && Info(R(previous).type).teaching) {
                const Side towardsPrevious = alongX ? Side::West : Side::North;
                PlaceDoor(id, towardsPrevious, (cursor + end) * 0.5f, DoorKind::Single, 0.95f, previous);
            }
            previous = id;
            cursor = end;
            ++index;
        }
    }

    /** A whole strip on a level: cores at fixed places, programs for the gaps between them (empty gaps use no program). */
    void BuildStrip(int level, const StripSpec& strip, std::vector<Core> cores, const std::vector<std::string>& programs)
    {
        const bool alongX = strip.AlongX();
        const float start = alongX ? strip.area.x0 : strip.area.z0;
        const float end = alongX ? strip.area.x1 : strip.area.z1;
        std::erase_if(cores, [&](const Core& c) { return ((c.levelMask >> level) & 1) == 0; });
        std::sort(cores.begin(), cores.end(), [](const Core& a, const Core& b) { return a.at < b.at; });
        float cursor = start;
        std::size_t gap = 0;
        const auto nextProgram = [&]() { return gap < programs.size() ? programs[gap++] : (++gap, std::string("CL*")); };
        for (const Core& c : cores) {
            if (c.at - cursor >= 1.0f) FillGap(level, strip, cursor, c.at, nextProgram());
            const Rect rect = alongX ? Rect(c.at, strip.area.z0, c.at + c.width, strip.area.z1)
                                     : Rect(strip.area.x0, c.at, strip.area.x1, c.at + c.width);
            const std::string code = c.code;
            if (code == "STAIR") {
                AddStairLanding(level, strip.wing, rect, strip.doorSide);
            } else if (code == "PASSAGE") {
                AddRoom(level, strip.wing, RoomType::Corridor, rect, 1, "Passage");
            } else if (code == "LINK") {
                AddRoom(level, strip.wing, RoomType::Corridor, rect, 1, "Link corridor");
            } else if (code == "VOID") {
                // deliberately empty: something larger is placed here by hand
            } else if (code == "ELEV") {
                AddElevatorCab(level, strip.wing, rect, strip.doorSide, alongX ? Side::East : Side::South);
            } else if (const SlotCode* slot = FindSlot(code)) {
                const RoomId id = AddRoom(level, strip.wing, slot->type, rect);
                PlaceDoor(id, strip.doorSide, c.at + c.width * 0.5f, DoorKind::Single, 1.1f);
            }
            cursor = c.at + c.width;
        }
        if (end - cursor >= 1.0f) FillGap(level, strip, cursor, end, nextProgram());
        // Closets behind the elevator shafts open into the room beside them (placed now that it exists).
        for (const auto& [closet, towards] : pendingClosets_) {
            const Rect& cr = R(closet).rect;
            const bool ax = towards == Side::East || towards == Side::West;
            if (PlaceDoor(closet, towards, ax ? cr.Center().z : cr.Center().x, DoorKind::Single, 0.9f) == kNone)
                PlaceDoor(closet, Opposite(towards), ax ? cr.Center().z : cr.Center().x, DoorKind::Single, 0.9f);
        }
        pendingClosets_.clear();
    }

    // --- elevators ---------------------------------------------------------------------------------------------
    /** A shaft core: the cab (2.6 x 2.6) at the corridor side of the core's rectangle, a closet behind it. */
    void AddElevatorCab(int level, int wing, const Rect& core, Side entrySide, Side closetDoorSide)
    {
        constexpr float kCab = 2.6f;
        Rect cab, closet;
        switch (entrySide) {
        case Side::North: cab = {core.x0, core.z0, core.x1, core.z0 + kCab}; closet = {core.x0, core.z0 + kCab, core.x1, core.z1}; break;
        case Side::South: cab = {core.x0, core.z1 - kCab, core.x1, core.z1}; closet = {core.x0, core.z0, core.x1, core.z1 - kCab}; break;
        case Side::West: cab = {core.x0, core.z0, core.x0 + kCab, core.z1}; closet = {core.x0 + kCab, core.z0, core.x1, core.z1}; break;
        default: cab = {core.x1 - kCab, core.z0, core.x1, core.z1}; closet = {core.x0, core.z0, core.x1 - kCab, core.z1}; break;
        }
        int shaft = kNone;
        for (const ElevatorShaft& e : plan.elevators)
            if (std::fabs(e.footprint.x0 - cab.x0) < 1e-3f && std::fabs(e.footprint.z0 - cab.z0) < 1e-3f) shaft = e.id;
        if (shaft == kNone) {
            ElevatorShaft e;
            e.id = static_cast<int>(plan.elevators.size());
            e.footprint = cab;
            e.entrySide = entrySide;
            e.levelLow = e.levelHigh = level;
            plan.elevators.push_back(e);
            shaft = e.id;
        }
        const RoomId id = AddRoom(level, wing, RoomType::ElevatorCab, cab, 1, "Elevator");
        R(id).elevatorId = shaft;
        R(id).capacity = 8;
        ElevatorShaft& e = plan.elevators[static_cast<std::size_t>(shaft)];
        e.levelLow = std::min(e.levelLow, level);
        e.levelHigh = std::max(e.levelHigh, level);
        e.cab.resize(static_cast<std::size_t>(e.levelHigh - e.levelLow + 1), kNone);
        e.cab[static_cast<std::size_t>(level - e.levelLow)] = id;
        const bool alongX = entrySide == Side::North || entrySide == Side::South;
        PlaceDoor(id, entrySide, alongX ? cab.Center().x : cab.Center().z, DoorKind::ElevatorDoor, 1.1f);
        if (closet.Valid() && closet.Width() > 1.0f && closet.Depth() > 1.0f) {
            const RoomId c = AddRoom(level, wing, RoomType::Janitor, closet, 1, "Elevator closet");
            pendingClosets_.emplace_back(c, closetDoorSide);
        }
    }

    // --- stairs ------------------------------------------------------------------------------
    RoomId AddStairLanding(int level, int wing, const Rect& rect, Side entrySide)
    {
        int shaft = kNone;
        for (const StairShaft& s : plan.stairs) {
            if (std::fabs(s.footprint.x0 - rect.x0) < 1e-3f && std::fabs(s.footprint.z0 - rect.z0) < 1e-3f &&
                std::fabs(s.footprint.x1 - rect.x1) < 1e-3f && std::fabs(s.footprint.z1 - rect.z1) < 1e-3f)
                shaft = s.id;
        }
        if (shaft == kNone) {
            StairShaft s;
            s.id = static_cast<int>(plan.stairs.size());
            s.footprint = rect;
            s.entrySide = entrySide;
            s.levelLow = level;
            s.levelHigh = level;
            plan.stairs.push_back(s);
            shaft = s.id;
        }
        const RoomId id = AddRoom(level, wing, RoomType::StairWell, rect, 1, "Stairwell");
        R(id).stairId = shaft;
        StairShaft& s = plan.stairs[static_cast<std::size_t>(shaft)];
        s.levelLow = std::min(s.levelLow, level);
        s.levelHigh = std::max(s.levelHigh, level);
        s.landing.resize(static_cast<std::size_t>(s.levelHigh - s.levelLow + 1), kNone);
        s.landing[static_cast<std::size_t>(level - s.levelLow)] = id;
        const bool alongX = entrySide == Side::North || entrySide == Side::South;
        PlaceDoor(id, entrySide, alongX ? rect.Center().x : rect.Center().z, DoorKind::Double, 1.6f);
        return id;
    }

    void BuildStairSurfaces()
    {
        for (StairShaft& s : plan.stairs) {
            const StairFrame f{s.footprint, s.entrySide};
            const float W = f.W(), D = f.D();
            s.flightWidth = (W - 1.0f) * 0.5f;
            const float Le = s.entryDepth;
            const float run = D - Le - s.midDepth;
            for (int level = s.levelLow; level <= s.levelHigh; ++level) {
                const float base = plan.FloorY(level);
                const float half = kFloorHeight * 0.5f;
                const auto add = [&](const Rect& rect, int axis, float c0, float c1, float y0, float y1) {
                    plan.stairSurfaces.push_back({level, rect, axis, c0, c1, y0, y1});
                };
                add(f.World(0, W, 0, Le), 0, 0, 0, base, base);   // entry landing
                if (level < s.levelHigh) {
                    add(f.World(0, s.flightWidth, Le, Le + run), f.Axis(), f.Along(Le), f.Along(Le + run), base, base + half);                               // flight 1
                    add(f.World(0, W, Le + run, D), 0, 0, 0, base + half, base + half);                                                                    // half landing
                    add(f.World(W - s.flightWidth, W, Le, Le + run), f.Axis(), f.Along(Le), f.Along(Le + run), base + kFloorHeight, base + half);           // flight 2
                    // Only the open well is solid. The masses under the flights and the half landing that used to be here reached down to
                    // the floor and overlapped the flights of the level below, which pass through the same footprint: the player was stopped
                    // 0.3 m short of the half landing and could not use flight 2 at all. What lies under a flight needs no wall: its ground
                    // is higher than a step (GroundHeight), so it cannot be entered.
                    plan.stairSolids.push_back({f.World(s.flightWidth, W - s.flightWidth, Le, Le + run), base - 0.5f, base + kFloorHeight * 2.0f});   // the open well
                } else if (level > s.levelLow) {
                    // The top landing: the hole above flight 1 of the level below is fenced off (the way down is flight 2). The fence starts
                    // just under the floor so the people on the flights below walk under it.
                    plan.stairSolids.push_back({f.World(0, s.flightWidth, Le, Le + run), base - 0.05f, base + 1.2f});
                }
            }
        }
    }

    // --- outdoors ----------------------------------------------------------------------------
    RoomId Zone(RoomType type, const Rect& rect, const char* name) { return AddRoom(kGround, kCampus, type, rect, 1, name); }

    /** Opening portals between two touching outdoor zones, one per `spacing` metres of shared boundary. */
    void JoinZones(RoomId a, RoomId b, float spacing = 30.0f, int maxPortals = 5)
    {
        const Room ra = R(a);
        const Room rb = R(b);
        for (int s = 0; s < 4; ++s) {
            const Side side = static_cast<Side>(s);
            const Wall wa = WallOf(ra.rect, side);
            const Wall wb = WallOf(rb.rect, Opposite(side));
            if (std::fabs(wa.fixed - wb.fixed) > 1e-3f) continue;
            const float lo = std::max(wa.lo, wb.lo);
            const float hi = std::min(wa.hi, wb.hi);
            if (hi - lo < 3.0f) continue;
            const int n = std::clamp(static_cast<int>((hi - lo) / spacing) + 1, 1, maxPortals);
            for (int i = 0; i < n; ++i) {
                const float c = lo + (hi - lo) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
                AddDoor(a, b, side, c, DoorKind::Opening, std::min(6.0f, (hi - lo) / static_cast<float>(n) * 0.8f), false);
            }
        }
    }

    RoomId Find(int level, RoomType type, const Rect& rect) const
    {
        for (const Room& r : plan.rooms)
            if (r.level == level && r.type == type && std::fabs(r.rect.x0 - rect.x0) < 0.01f && std::fabs(r.rect.z0 - rect.z0) < 0.01f &&
                std::fabs(r.rect.x1 - rect.x1) < 0.01f && std::fabs(r.rect.z1 - rect.z1) < 0.01f)
                return r.id;
        return kNone;
    }

private:
    std::map<std::pair<int, int>, int> counters_;
    std::vector<std::pair<RoomId, Side>> pendingClosets_;
};

}  // namespace

// ---------------------------------------------------------------------------------------------------
SchoolPlan BuildSchoolPlan(const PlanOptions& /*options*/)
{
    Builder b;
    using Core = Builder::Core;
    using Strip = Builder::StripSpec;
    constexpr int kAll = Builder::kAll;
    const auto L = [](std::initializer_list<int> l) { return Builder::Levels(l); };

    // --- ring geometry: two strips of rooms along every corridor -----------------------------------
    const Strip NO{{9, 0, 155, 9}, Side::South, kNorth};
    const Strip NI{{13, 13, 151, 22}, Side::North, kNorth};
    const Strip SO{{9, 115, 155, 124}, Side::North, kSouth};
    const Strip SI{{13, 102, 151, 111}, Side::South, kSouth};
    const Strip WO{{0, 13, 9, 111}, Side::East, kWest};
    const Strip WI{{13, 22, 22, 102}, Side::West, kWest};
    const Strip EO{{155, 13, 164, 111}, Side::West, kEast};
    const Strip EI{{142, 22, 151, 102}, Side::East, kEast};

    const int allLevels = L({0, 1, 2, 3, 4, 5});
    (void)allLevels;
    for (int level = kBasement; level <= kTop; ++level) {
        const bool south = level <= 4;
        const bool north = level >= 1;
        const bool sides = level >= 1 && level <= 4;
        const bool ground = level == kGround;

        // Corridors (segments of ~18 m).
        if (north) b.AddCorridor(level, kNorth, {9, 9, 155, 13}, true);
        if (south) {
            if (ground) {   // the main lobby interrupts the south corridor
                b.AddCorridor(level, kSouth, {9, 111, 64, 115}, true);
                b.AddCorridor(level, kSouth, {100, 111, 155, 115}, true);
            } else {
                b.AddCorridor(level, kSouth, {9, 111, 155, 115}, true);
            }
        }
        if (sides) {
            b.AddCorridor(level, kWest, {9, 13, 13, 111}, false);
            b.AddCorridor(level, kEast, {151, 13, 155, 111}, false);
        }

        // Corner towers (stairs) with a small landing each.
        if (north) {
            b.AddRoom(level, kNorth, RoomType::Corridor, {0, 9, 9, 13}, 1, "Corner landing");
            b.AddRoom(level, kNorth, RoomType::Corridor, {155, 9, 164, 13}, 1, "Corner landing");
            b.AddStairLanding(level, kNorth, {0, 0, 9, 9}, Side::South);
            b.AddStairLanding(level, kNorth, {155, 0, 164, 9}, Side::South);
        }
        if (south) {
            b.AddRoom(level, kSouth, RoomType::Corridor, {0, 111, 9, 115}, 1, "Corner landing");
            b.AddRoom(level, kSouth, RoomType::Corridor, {155, 111, 164, 115}, 1, "Corner landing");
            b.AddStairLanding(level, kSouth, {0, 115, 9, 124}, Side::North);
            b.AddStairLanding(level, kSouth, {155, 115, 164, 124}, Side::North);
        }

        // --- North wing: humanities -----------------------------------------------------------------
        if (north) {
            const int nl = L({1, 2, 3, 4, 5});
            const std::vector<Core> noCores = {{38, 8, "STAIR", nl}, {46, 6, "TO", nl}, {52, 6, "TO", nl},
                                               {104, 8, "STAIR", nl}, {112, 6, "TO", nl}, {118, 6, "TO", nl}};
            if (ground) {
                b.BuildStrip(level, NO, noCores, {"CL*", "LA*", "CL*"});
                b.BuildStrip(level, NI, {{80, 4, "PASSAGE", kAll}}, {"CL*", "CL*"});
            } else if (level == kTop) {
                b.BuildStrip(level, NO, noCores, {"CL*", "SR MT CL*", "CL*"});
                b.BuildStrip(level, NI, {{80, 4, "ST", kAll}}, {"CL*", "CO CO CL*"});
            } else {
                b.BuildStrip(level, NO, noCores, {"CL*", "LA*", "CL*"});
                b.BuildStrip(level, NI, {{80, 4, "ST", kAll}}, {"CL*", "CL*"});
            }
        }

        // --- East wing: science ---------------------------------------------------------------------
        if (sides) {
            const std::vector<Core> eoCores = {{55, 4, "LINK", L({1})}, {63, 8, "STAIR", kAll}, {71, 2.6f, "ELEV", kAll}, {73.6f, 3.4f, "TO", kAll}, {77, 6, "TO", kAll}};
            const std::vector<Core> eiCores = {{58, 4, "PASSAGE", L({1})}};
            if (ground) {
                b.BuildStrip(level, EO, eoCores, {"CL*", "OF", "BI PP BI PP"});
                b.BuildStrip(level, EI, eiCores, {"BI PP BI PP CL*", "BI PP BI PP CL*"});
            } else if (level == 2) {
                b.BuildStrip(level, EO, eoCores, {"PH PP PH PP CH PP", "CH PP CL"});
                b.BuildStrip(level, EI, eiCores, {"CH PP CH PP PH PP PH PP CL*"});
            } else if (level == 3) {
                b.BuildStrip(level, EO, eoCores, {"CH PP CH PP BI PP", "BI PP CL"});
                b.BuildStrip(level, EI, eiCores, {"BI PP BI PP CH PP CH PP CL*"});
            } else {
                b.BuildStrip(level, EO, eoCores, {"CO CO CL*", "PH PP CO"});
                b.BuildStrip(level, EI, eiCores, {"CO CO PH PP PH PP CL*"});
            }
        }

        // --- West wing: languages and mathematics ----------------------------------------------------
        if (sides) {
            const std::vector<Core> woCores = {{55, 4, "LINK", L({1})}, {63, 8, "STAIR", kAll}, {71, 2.6f, "ELEV", kAll}, {73.6f, 3.4f, "TO", kAll}, {77, 6, "TO", kAll}, {83, 4, "LINK", L({1})}};
            const std::vector<Core> wiCores = {{58, 4, "PASSAGE", L({1})}};
            if (ground) {
                b.BuildStrip(level, WO, woCores, {"CL*", "LA*"});
                b.BuildStrip(level, WI, wiCores, {"CL*", "CL*"});
            } else {
                b.BuildStrip(level, WO, woCores, {"LA*", "CL*"});
                b.BuildStrip(level, WI, wiCores, {"CL*", "CL*"});
            }
        }

        // --- South wing: administration, arts, computing ------------------------------------------------
        if (south) {
            const int sl = L({0, 1, 2, 3, 4});
            std::vector<Core> soCores = {{38, 8, "STAIR", sl}, {46, 6, "TO", sl}, {52, 6, "TO", sl},
                                         {118, 8, "STAIR", sl}, {126, 6, "TO", sl}, {132, 6, "TO", sl}};
            std::vector<Core> siCores;
            if (ground) {
                soCores.push_back({64, 36, "VOID", L({1})});
                siCores.push_back({64, 36, "VOID", L({1})});
            }
            if (level == kBasement) {
                b.BuildStrip(level, SO, soCores, {"LK*", "BO EL SV AC ST*", "SV ST EQ ST*", "ST*"});
                b.BuildStrip(level, SI, siCores, {"LK LK LK LK LK LK EQ EQ AC AC AC JA ST ST WO"});
            } else if (ground) {
                b.BuildStrip(level, SO, soCores, {"OF OF PR MT", "SE", "RE OF*", "MT OF"});
                b.BuildStrip(level, SI, siCores, {"SR MT NU CN OF*", "CN OF OF MT ST AC*"});
            } else if (level == 2) {
                b.BuildStrip(level, SO, soCores, {"AR AR", "MU MU AR* ", "WO CS*", "AR*"});
                b.BuildStrip(level, SI, siCores, {"AR AR AR CS*"});
            } else if (level == 3) {
                b.BuildStrip(level, SO, soCores, {"CO CO*", "CO*", "CL*", "CO*"});
                b.BuildStrip(level, SI, siCores, {"CL*"});
            } else {
                b.BuildStrip(level, SO, soCores, {"CL*", "LA*", "CL*", "CL*"});
                b.BuildStrip(level, SI, siCores, {"CL*"});
            }
        }
    }

    // --- ground floor: the main lobby ---------------------------------------------------------------
    const RoomId lobby = b.AddRoom(kGround, kSouth, RoomType::Lobby, {64, 102, 100, 124}, 1, "Main lobby");
    b.plan.mainLobby = lobby;

    // --- sports block (west): gymnasium, changing rooms, workshops ------------------------------------
    b.AddCorridor(kGround, kSports, {-44, 55, 0, 59}, true, 15.0f);
    b.AddCorridor(kGround, kSports, {-44, 83, 0, 87}, true, 15.0f);
    const RoomId gym = b.AddRoom(kGround, kSports, RoomType::Gym, {-44, 13, 0, 55}, 2, "Main gymnasium");
    const Strip sportsA{{-44, 59, 0, 83}, Side::North, kSports};
    const Strip sportsB{{-44, 87, 0, 111}, Side::North, kSports};
    b.BuildStrip(kGround, sportsA, {}, {"CG CG FI EQ OF"});
    b.BuildStrip(kGround, sportsB, {}, {"WO WO"});

    // --- community block (east): cafeteria, kitchen, auditorium, library, music, drama ----------------------
    b.AddCorridor(kGround, kCommunity, {164, 55, 236, 59}, true, 18.0f);
    b.AddCorridor(kGround, kCommunity, {200, 59, 204, 111}, false, 18.0f);
    const RoomId kitchen = b.AddRoom(kGround, kCommunity, RoomType::Kitchen, {164, 13, 200, 25}, 1, "Kitchen");
    const RoomId cafeteria = b.AddRoom(kGround, kCommunity, RoomType::Cafeteria, {164, 25, 200, 55}, 2, "Cafeteria");
    const RoomId auditorium = b.AddRoom(kGround, kCommunity, RoomType::Auditorium, {200, 13, 236, 55}, 2, "Auditorium");
    const RoomId library = b.AddRoom(kGround, kCommunity, RoomType::Library, {164, 59, 200, 111}, 2, "Library");
    const RoomId musicHall = b.AddRoom(kGround, kCommunity, RoomType::MusicRoom, {204, 59, 236, 85}, 1, "Music hall");
    const RoomId dance = b.AddRoom(kGround, kCommunity, RoomType::DramaStudio, {204, 85, 236, 111}, 1, "Drama and dance studio");
    b.R(musicHall).capacity = 60;

    // --- outdoor campus -------------------------------------------------------------------------------------
    const RoomId courtyard = b.Zone(RoomType::Courtyard, {22, 22, 142, 102}, "Courtyard");
    const RoomId plaza = b.Zone(RoomType::Plaza, {0, 124, 164, 140}, "Front plaza");
    const RoomId eastYard = b.Zone(RoomType::Plaza, {164, 111, 236, 125}, "East yard");
    const RoomId westYard = b.Zone(RoomType::Plaza, {-44, 111, 0, 140}, "West yard");
    const RoomId playground = b.Zone(RoomType::Playground, {-100, 13, -44, 110}, "Playground");
    const RoomId westLawn = b.Zone(RoomType::Plaza, {-100, -10, -44, 13}, "West lawn");
    const RoomId northPlaza = b.Zone(RoomType::Plaza, {-44, -10, 236, 0}, "North terrace");
    const RoomId serviceYard = b.Zone(RoomType::Plaza, {164, 0, 236, 13}, "Service yard");
    const RoomId eastLawn = b.Zone(RoomType::Plaza, {236, -10, 262, 140}, "East lawn");
    const RoomId parking = b.Zone(RoomType::Parking, {0, 140, 120, 180}, "Car park");
    const RoomId bikes = b.Zone(RoomType::BikeArea, {120, 140, 164, 160}, "Bicycle area");
    const RoomId sideLane = b.Zone(RoomType::Plaza, {120, 160, 164, 180}, "Side entrance");
    const RoomId trackN = b.Zone(RoomType::Track, {10, -110, 150, -95}, "Running track (north)");
    const RoomId trackS = b.Zone(RoomType::Track, {10, -25, 150, -10}, "Running track (south)");
    const RoomId trackW = b.Zone(RoomType::Track, {10, -95, 30, -25}, "Running track (west)");
    const RoomId trackE = b.Zone(RoomType::Track, {130, -95, 150, -25}, "Running track (east)");
    const RoomId field = b.Zone(RoomType::FootballField, {30, -95, 130, -35}, "Football field");
    std::vector<RoomId> assembly;
    const char* letters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I"};
    const Rect assemblyRects[] = {{30, -35, 50, -25}, {50, -35, 70, -25}, {70, -35, 90, -25}, {90, -35, 110, -25}, {110, -35, 130, -25},
                                  {-100, 110, -72, 140}, {-72, 110, -44, 140}, {164, 125, 200, 140}, {200, 125, 236, 140}};
    for (int i = 0; i < 9; ++i) {
        const std::string name = std::string("Assembly point ") + letters[i];
        const RoomId id = b.Zone(RoomType::AssemblyPoint, assemblyRects[i], "");
        b.R(id).name = name;
        assembly.push_back(id);
    }
    b.plan.assemblyPoints = assembly;
    const RoomId street = b.Zone(RoomType::Street, {-110, 180, 300, 200}, "Street (off campus)");
    b.plan.offsite = street;

    // --- doors that need both sides to exist ---------------------------------------------------------------------
    for (int level = kBasement; level <= kTop; ++level) b.ConnectCirculation(level);
    // Outdoor zones touching each other (except the street, which has real gates).
    for (std::size_t i = 0; i < b.plan.rooms.size(); ++i)
        for (std::size_t j = i + 1; j < b.plan.rooms.size(); ++j) {
            const Room& ra = b.plan.rooms[i];
            const Room& rb = b.plan.rooms[j];
            if (ra.Indoor() || rb.Indoor() || ra.type == RoomType::Street || rb.type == RoomType::Street) continue;
            b.JoinZones(ra.id, rb.id);
        }
    b.PlaceDoor(parking, Side::South, 30.0f, DoorKind::Gate, 6.0f, street);
    b.PlaceDoor(parking, Side::South, 85.0f, DoorKind::Gate, 6.0f, street);
    b.PlaceDoor(sideLane, Side::South, 142.0f, DoorKind::Gate, 4.0f, street);

    // The main lobby: automatic doors to the plaza, courtyard doors, and every office around it.
    b.PlaceDoor(lobby, Side::South, 74.0f, DoorKind::Automatic, 2.6f, plaza, true);
    b.PlaceDoor(lobby, Side::South, 82.0f, DoorKind::Automatic, 2.6f, plaza, true);
    b.PlaceDoor(lobby, Side::South, 90.0f, DoorKind::Automatic, 2.6f, plaza, true);
    b.PlaceDoor(lobby, Side::North, 74.0f, DoorKind::Double, 2.0f, courtyard, true);
    b.PlaceDoor(lobby, Side::North, 90.0f, DoorKind::Double, 2.0f, courtyard, true);
    for (const Room& r : std::vector<Room>(b.plan.rooms)) {
        if (r.level != kGround || !r.Indoor() || Info(r.type).circulation || r.id == lobby) continue;
        if (std::fabs(r.rect.x1 - 64.0f) < 1e-3f && r.rect.z0 >= 102.0f - 1e-3f && r.rect.z1 <= 124.0f + 1e-3f)
            b.PlaceDoor(r.id, Side::East, r.rect.Center().z, DoorKind::Single, 1.1f, lobby);
        if (std::fabs(r.rect.x0 - 100.0f) < 1e-3f && r.rect.z0 >= 102.0f - 1e-3f && r.rect.z1 <= 124.0f + 1e-3f)
            b.PlaceDoor(r.id, Side::West, r.rect.Center().z, DoorKind::Single, 1.1f, lobby);
    }

    // Courtyard passages: glass doors from each ring passage into the courtyard.
    for (const Room& r : std::vector<Room>(b.plan.rooms)) {
        if (r.type != RoomType::Corridor || r.level != kGround || r.name.rfind("Passage", 0) != 0) continue;
        Side towards = Side::South;
        if (r.rect.x0 > 100) towards = Side::West;
        else if (r.rect.x1 < 30) towards = Side::East;
        b.PlaceDoor(r.id, towards, towards == Side::South ? r.rect.Center().x : r.rect.Center().z, DoorKind::Double, 2.4f, courtyard, true);
    }

    // Stair towers and mid-wing stairwells have an outside door on the ground floor where there is outdoors beside
    // them: in a side wall, at the entry landing (the rest of the shaft is flights and the open well).
    for (const Room& r : std::vector<Room>(b.plan.rooms)) {
        if (r.type != RoomType::StairWell || r.level != kGround) continue;
        const StairShaft& shaft = b.plan.stairs[static_cast<std::size_t>(r.stairId)];
        const float landing = shaft.entryDepth * 0.5f;
        for (const Side side : {Side::North, Side::South, Side::West, Side::East}) {
            if (side == shaft.entrySide || side == Opposite(shaft.entrySide)) continue;
            float along;
            switch (shaft.entrySide) {
            case Side::South: along = r.rect.z1 - landing; break;
            case Side::North: along = r.rect.z0 + landing; break;
            case Side::West: along = r.rect.x0 + landing; break;
            default: along = r.rect.x1 - landing; break;
            }
            b.PlaceDoor(r.id, side, along, DoorKind::External, 1.6f, kNone, true, true);
        }
    }

    // Sports block: gym doors to the concourse and out to the playground.
    b.PlaceDoor(gym, Side::South, -33.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(gym, Side::South, -12.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(gym, Side::West, 22.0f, DoorKind::External, 2.2f, playground, true);
    b.PlaceDoor(gym, Side::West, 44.0f, DoorKind::External, 2.2f, playground, true);
    for (const Room& r : std::vector<Room>(b.plan.rooms))
        if (r.wing == kSports && r.type == RoomType::ChangingRoom)
            b.PlaceDoor(r.id, Side::West, r.rect.Center().z, DoorKind::External, 1.4f, playground, true);
    for (const Room& r : std::vector<Room>(b.plan.rooms))
        if (r.wing == kSports && r.type == RoomType::Workshop) b.PlaceDoor(r.id, Side::South, r.rect.Center().x, DoorKind::External, 2.0f, westYard, true);

    // Community block.
    b.PlaceDoor(kitchen, Side::South, 182.0f, DoorKind::Opening, 3.4f, cafeteria);   // the serving line
    b.PlaceDoor(kitchen, Side::South, 195.0f, DoorKind::Single, 1.1f, cafeteria);
    b.PlaceDoor(kitchen, Side::North, 170.0f, DoorKind::External, 1.8f, serviceYard, true);
    b.PlaceDoor(cafeteria, Side::South, 172.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(cafeteria, Side::South, 192.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(auditorium, Side::South, 210.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(auditorium, Side::South, 226.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(auditorium, Side::East, 25.0f, DoorKind::External, 2.2f, eastLawn, true);
    b.PlaceDoor(auditorium, Side::East, 45.0f, DoorKind::External, 2.2f, eastLawn, true);
    b.PlaceDoor(library, Side::North, 172.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(library, Side::North, 192.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(library, Side::East, 80.0f, DoorKind::Double, 2.2f);
    b.PlaceDoor(library, Side::South, 175.0f, DoorKind::External, 2.2f, eastYard, true);
    b.PlaceDoor(library, Side::South, 190.0f, DoorKind::External, 2.2f, eastYard, true);
    b.PlaceDoor(musicHall, Side::West, 70.0f, DoorKind::Double, 2.0f);
    b.PlaceDoor(musicHall, Side::East, 62.0f, DoorKind::External, 2.0f, eastLawn, true);
    b.PlaceDoor(dance, Side::West, 98.0f, DoorKind::Double, 2.0f);
    b.PlaceDoor(dance, Side::South, 208.0f, DoorKind::External, 2.0f, eastYard, true);

    b.BuildStairSurfaces();
    (void)trackN; (void)trackS; (void)trackW; (void)trackE; (void)field; (void)playground; (void)westLawn; (void)northPlaza;
    (void)bikes; (void)serviceYard;
    b.plan.BuildIndex();
    return b.plan;
}

}  // namespace School
