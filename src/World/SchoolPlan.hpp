#pragma once

// The school as data: floors, wings, rooms, doors, stairs and elevators. The plan is the single
// source of truth: rendering builds its meshes from it, navigation builds its graph from it, the
// timetable picks its rooms from it. It contains no CNA types and no random state.

#include <cstdint>
#include <string>
#include <vector>

#include "World/Geometry.hpp"

namespace School {

using RoomId = int;
using DoorId = int;
constexpr int kNone = -1;

constexpr float kFloorHeight = 4.0f;     // floor level to floor level
constexpr float kCeilingHeight = 3.4f;   // clear height of an ordinary room
constexpr float kDoorHeight = 2.15f;
constexpr float kEyeHeight = 1.65f;

enum class RoomType : std::uint8_t {
    // circulation
    Corridor, Lobby, StairWell, ElevatorCab,
    // teaching
    Classroom, LanguageRoom, ComputerLab, PhysicsLab, ChemistryLab, BiologyLab, ArtStudio, MusicRoom,
    Workshop, Gym, Library, Auditorium, Cafeteria, DramaStudio,
    // staff and administration
    Reception, AdminOffice, PrincipalOffice, StaffRoom, MeetingRoom, Nurse, Counseling, ServerRoom,
    SecurityOffice,
    // support
    Kitchen, Storage, LabPrep, Boiler, Electrical, Toilets, ChangingRoom, LockerHall, Janitor, Archive,
    EquipmentStore, FitnessRoom,
    // outdoors
    Courtyard, Playground, Track, FootballField, Parking, BikeArea, Plaza, AssemblyPoint, Street,
    Count
};

struct RoomTypeInfo {
    const char* name;
    bool indoor;
    bool circulation;   // people pass through; nobody is "assigned" here
    bool teaching;      // a class can have a lesson here
    bool outdoor() const { return !indoor; }
};
const RoomTypeInfo& Info(RoomType type);
inline const char* TypeName(RoomType type) { return Info(type).name; }

enum class DoorKind : std::uint8_t {
    Single,      // ordinary hinged door
    Double,      // two leaves
    External,    // building exit: a heavier double door
    Automatic,   // sliding glass (main entrance)
    Opening,     // no leaf at all: corridor to corridor, courtyard passage
    ElevatorDoor,
    Gate         // fence gate in the campus boundary
};

struct Floor {
    int level = 0;
    std::string name;
    float baseY = 0.0f;   // height of the floor surface above the ground-floor surface
};

/** A wing has a visual identity: tints and floor materials chosen per wing so the building is legible. */
struct Wing {
    int id = 0;
    std::string name;
    float wall[3] = {0.9f, 0.9f, 0.9f};     // wall paint tint (linear 0..1)
    float accent[3] = {0.3f, 0.3f, 0.8f};   // stripe, signs, door frames
    const char* corridorFloor = "floor_tile";
};

struct Room {
    RoomId id = kNone;
    std::string name;     // "Classroom N2.14"
    std::string code;     // "N2.14"
    RoomType type = RoomType::Storage;
    int level = 1;        // lowest floor level occupied
    int levels = 1;       // storeys of height (a gym is two)
    int wing = 0;
    Rect rect;
    int capacity = 0;     // seats for a lesson, or comfortable occupancy
    int stairId = kNone;
    int elevatorId = kNone;
    std::uint8_t exteriorSides = 0;   // bit per Side: this wall has no neighbouring building room (windows)
    std::vector<DoorId> doors;

    [[nodiscard]] bool Indoor() const { return Info(type).indoor; }
    [[nodiscard]] bool IsCorridor() const { return type == RoomType::Corridor; }
    [[nodiscard]] bool HasExterior(Side s) const { return (exteriorSides >> static_cast<int>(s)) & 1; }
    [[nodiscard]] int TopLevel() const { return level + levels - 1; }
    [[nodiscard]] float Height() const { return kFloorHeight * static_cast<float>(levels) - (kFloorHeight - kCeilingHeight); }
};

struct Door {
    DoorId id = kNone;
    RoomId a = kNone;     // the door's normal points from a into b
    RoomId b = kNone;
    int level = 1;
    Vec2 pos;             // centre of the door in plan, on the wall line
    Side sideOfA = Side::North;   // which side of room a holds the door
    float width = 1.1f;
    DoorKind kind = DoorKind::Single;
    bool exit = false;    // leads out of the building (evacuation exit)
    bool hingeLeft = true;

    [[nodiscard]] Vec2 Normal() const { return SideNormal(sideOfA); }
    [[nodiscard]] bool Leaf() const { return kind != DoorKind::Opening; }
    [[nodiscard]] RoomId Other(RoomId from) const { return from == a ? b : a; }
};

/** Local frame of a U-shaped stair: u runs along the entry wall, v away from it. */
struct StairShaft {
    int id = kNone;
    Rect footprint;
    Side entrySide = Side::South;   // the wall the corridor door is in
    int levelLow = 1;               // lowest level served
    int levelHigh = 2;              // highest level served
    std::vector<RoomId> landing;    // one stairwell room per level, index = level - levelLow
    float entryDepth = 2.2f;        // entry landing depth (v)
    float midDepth = 2.4f;          // half-landing depth at the far end
    float flightWidth = 3.4f;
};

struct ElevatorShaft {
    int id = kNone;
    Rect footprint;
    Side entrySide = Side::South;
    int levelLow = 1;
    int levelHigh = 2;
    std::vector<RoomId> cab;        // one shaft room per level
};

/** A place where the floor is not at the room's base height: a flight of stairs (for walking and collision). */
struct WalkSurface {
    int level = 1;        // level of the stair room it belongs to
    Rect rect;
    int slopeAxis = 0;    // 0 flat, 1 slopes along x, 2 along z
    float c0 = 0.0f;      // coordinate where the slope starts (along slopeAxis)
    float c1 = 0.0f;      // coordinate where it ends
    float y0 = 0.0f;      // absolute height at c0
    float y1 = 0.0f;      // absolute height at c1
    [[nodiscard]] float HeightAt(Vec2 p) const
    {
        if (slopeAxis == 0) return y0;
        const float c = slopeAxis == 1 ? p.x : p.z;
        const float t = std::clamp((c - c0) / (c1 - c0), 0.0f, 1.0f);
        return y0 + (y1 - y0) * t;
    }
};

/** Collision-only solid (height-aware) used around stairs: the mass under a flight, the open well between flights. */
struct SolidBox {
    Rect rect;
    float y0 = 0.0f;
    float y1 = 0.0f;
};

class SchoolPlan {
public:
    std::vector<Floor> floors;
    std::vector<Wing> wings;
    std::vector<Room> rooms;
    std::vector<Door> doors;
    std::vector<StairShaft> stairs;
    std::vector<ElevatorShaft> elevators;
    std::vector<WalkSurface> stairSurfaces;
    std::vector<SolidBox> stairSolids;

    RoomId offsite = kNone;               // the street: where everyone comes from and goes to
    RoomId mainLobby = kNone;
    std::vector<RoomId> assemblyPoints;   // fire drill gathering areas (outdoor)
    Rect campus;                          // bounds of everything built

    [[nodiscard]] const Room& room(RoomId id) const { return rooms[static_cast<std::size_t>(id)]; }
    [[nodiscard]] Room& room(RoomId id) { return rooms[static_cast<std::size_t>(id)]; }
    [[nodiscard]] const Door& door(DoorId id) const { return doors[static_cast<std::size_t>(id)]; }
    [[nodiscard]] float FloorY(int level) const { return floors[static_cast<std::size_t>(level)].baseY; }
    [[nodiscard]] int LevelCount() const { return static_cast<int>(floors.size()); }
    /** Level whose floor is nearest below the height y. */
    [[nodiscard]] int LevelAt(float y) const;

    /** The room containing p on the given level (rooms taller than one storey count for each level they span). */
    [[nodiscard]] RoomId RoomAt(Vec2 p, int level, float margin = 0.0f) const;
    /** The floor height under p when standing near height refY (stairs make it vary). */
    [[nodiscard]] float GroundHeight(Vec2 p, float refY) const;

    /** All rooms of a level that are of a given type. */
    [[nodiscard]] std::vector<RoomId> RoomsOfType(RoomType type) const;

    /** Builds the lookup grid, exterior-side flags and per-room door lists. Call once after generation. */
    void BuildIndex();

private:
    struct Grid {
        float cell = 8.0f;
        float minX = 0.0f, minZ = 0.0f;
        int nx = 0, nz = 0;
        std::vector<std::vector<RoomId>> cells;
    } grid_;
    [[nodiscard]] const std::vector<RoomId>* Cell(Vec2 p) const;
};

/** Everything generated from the same description: one call produces the whole school. */
struct PlanOptions {
    std::uint64_t seed = 1;
};
SchoolPlan BuildSchoolPlan(const PlanOptions& options = {});

/** Problems found in a plan (empty = sound): overlaps, doors off their walls, dangling references... */
std::vector<std::string> ValidatePlan(const SchoolPlan& plan);

/** Counts for the HUD, documentation and tests. */
struct PlanStats {
    int rooms = 0, indoorRooms = 0, outdoorZones = 0, corridors = 0, doors = 0, stairs = 0, elevators = 0;
    int perType[static_cast<int>(RoomType::Count)] = {};
    int perLevel[16] = {};
    float indoorAreaM2 = 0.0f;
};
PlanStats ComputeStats(const SchoolPlan& plan);

}  // namespace School
