#pragma once

// Hierarchical navigation.
//
//   school graph : rooms are nodes; doors, openings and stairs are edges (this file)
//   local path   : the waypoints through each room: door approach, doorway, door exit, corridor lane
//                  points, then the room's own aisles to a seat (PathBuilder, Rooms/RoomLayout)
//
// Every room is a rectangle, so a straight line between two points of one room is always walkable;
// that is why no navigation mesh is needed. Routes are found with one Dijkstra per *destination*, run
// the first time somebody goes there and kept: after that "which door next?" is a table lookup.

#include <cstdint>
#include <memory>
#include <vector>

#include "World/SchoolPlan.hpp"

namespace School {

struct NavEdge {
    RoomId from = kNone;
    RoomId to = kNone;
    DoorId door = kNone;     // kNone for a stair edge
    int stair = kNone;       // stair shaft id for a stair edge
    int pairedEdge = kNone;  // the same connection in the other direction
    float cost = 0.0f;       // metres of walking (stairs include a penalty)
    bool upward = false;     // stair edges: climbing
};

class NavGraph {
public:
    explicit NavGraph(const SchoolPlan& plan);

    [[nodiscard]] const SchoolPlan& Plan() const { return plan_; }
    [[nodiscard]] int NodeCount() const { return static_cast<int>(plan_.rooms.size()); }
    [[nodiscard]] const std::vector<NavEdge>& Edges() const { return edges_; }
    [[nodiscard]] const NavEdge& Edge(int id) const { return edges_[static_cast<std::size_t>(id)]; }
    [[nodiscard]] const std::vector<int>& EdgesFrom(RoomId room) const { return adjacency_[static_cast<std::size_t>(room)]; }

    /** Walking cost from a room to a destination room in metres, or a negative value when unreachable. */
    [[nodiscard]] float Distance(RoomId from, RoomId to) const;
    /** The edge to take from `from` on the way to `to`; kNone when there (from == to) or unreachable. */
    [[nodiscard]] int NextEdge(RoomId from, RoomId to) const;
    /** Whole route as edge ids; false when unreachable. Appends to `out` (cleared first). */
    bool Route(RoomId from, RoomId to, std::vector<int>& out) const;
    [[nodiscard]] bool Reachable(RoomId from, RoomId to) const { return Distance(from, to) >= 0.0f; }

    /** Rooms reachable from `from` (a flood fill, not using the destination trees). */
    [[nodiscard]] std::vector<RoomId> ReachableFrom(RoomId from) const;

    [[nodiscard]] int TreesBuilt() const { return treesBuilt_; }
    [[nodiscard]] long long TreeQueries() const { return queries_; }

private:
    struct Tree {
        std::vector<float> dist;        // to the destination; < 0 unreachable
        std::vector<int> nextEdge;      // from each room, the edge towards the destination
    };
    [[nodiscard]] const Tree& TreeTo(RoomId dest) const;

    const SchoolPlan& plan_;
    std::vector<NavEdge> edges_;
    std::vector<std::vector<int>> adjacency_;
    mutable std::vector<std::unique_ptr<Tree>> trees_;
    mutable int treesBuilt_ = 0;
    mutable long long queries_ = 0;
};

/** One point of a walk. */
enum class WaypointKind : std::uint8_t {
    Walk,          // ordinary point inside a room
    DoorApproach,  // wait here if the door is busy
    Doorway,       // in the door itself (opens it)
    DoorExit,      // just past the door
    StairStep,     // on a flight of stairs (height changes)
    Spot           // the final place (a seat, a queue slot, a standing point)
};

struct Waypoint {
    Vec3 pos;
    RoomId room = kNone;      // the room this point lies in
    DoorId door = kNone;      // for the three door kinds
    WaypointKind kind = WaypointKind::Walk;
};

/**
 * Turns a room route into waypoints. `startPos`/`startRoom` is where the walker is; the walk ends at
 * `endPos` in `endRoom` (the caller picks that spot; seats are reached through the room's aisles by the
 * caller appending its own points). Lane offsets keep opposite flows in corridors apart.
 */
class PathBuilder {
public:
    explicit PathBuilder(const NavGraph& graph) : graph_(graph) {}

    /** Appends waypoints to `out`; returns false if the destination is unreachable. */
    bool Build(RoomId startRoom, Vec2 startPos, RoomId endRoom, Vec2 endPos, std::vector<Waypoint>& out, float laneOffset = 0.85f) const;

    /** Height of the floor of a room's first level. */
    [[nodiscard]] float BaseY(RoomId room) const;

    /** Approach point of a door seen from `from`, and the exit point on the far side. */
    [[nodiscard]] static Vec2 Approach(const Door& door, RoomId from, float distance = 0.9f);

private:
    void AddRoomCrossing(const Room& room, Vec2 enter, Vec2 leave, bool enterIsPortal, bool leaveIsPortal, std::vector<Waypoint>& out, float lane) const;
    void AddStairs(const NavEdge& edge, Vec2 enterPos, std::vector<Waypoint>& out) const;
    const NavGraph& graph_;
};

}  // namespace School
