#include "Navigation/NavGraph.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>

#include "World/StairGeometry.hpp"

namespace School {

namespace {
constexpr float kStairPenalty = 1.5f;   // stairs are slower than level ground: cost per metre of climb
}

NavGraph::NavGraph(const SchoolPlan& plan) : plan_(plan)
{
    adjacency_.resize(plan.rooms.size());
    trees_.resize(plan.rooms.size());
    const auto addPair = [&](NavEdge forward, NavEdge backward) {
        const int a = static_cast<int>(edges_.size());
        forward.pairedEdge = a + 1;
        backward.pairedEdge = a;
        edges_.push_back(forward);
        edges_.push_back(backward);
        adjacency_[static_cast<std::size_t>(forward.from)].push_back(a);
        adjacency_[static_cast<std::size_t>(backward.from)].push_back(a + 1);
    };
    for (const Door& d : plan.doors) {
        const Room& a = plan.room(d.a);
        const Room& b = plan.room(d.b);
        const float cost = School::Distance(a.rect.Center(), d.pos) + School::Distance(d.pos, b.rect.Center());
        NavEdge f;
        f.from = d.a;
        f.to = d.b;
        f.door = d.id;
        f.cost = std::max(0.5f, cost);
        NavEdge r = f;
        r.from = d.b;
        r.to = d.a;
        addPair(f, r);
    }
    for (const StairShaft& s : plan.stairs) {
        for (std::size_t i = 0; i + 1 < s.landing.size(); ++i) {
            const RoomId lower = s.landing[i];
            const RoomId upper = s.landing[i + 1];
            if (lower == kNone || upper == kNone) continue;
            const float length = PathLength(StairClimbPath(plan, s, s.levelLow + static_cast<int>(i), true));
            NavEdge up;
            up.from = lower;
            up.to = upper;
            up.stair = s.id;
            up.upward = true;
            up.cost = length * kStairPenalty;
            NavEdge down = up;
            down.from = upper;
            down.to = lower;
            down.upward = false;
            addPair(up, down);
        }
    }
}

const NavGraph::Tree& NavGraph::TreeTo(RoomId dest) const
{
    ++queries_;
    std::unique_ptr<Tree>& slot = trees_[static_cast<std::size_t>(dest)];
    if (slot) return *slot;
    ++treesBuilt_;
    auto tree = std::make_unique<Tree>();
    const std::size_t n = plan_.rooms.size();
    tree->dist.assign(n, -1.0f);
    tree->nextEdge.assign(n, kNone);
    // Dijkstra outwards from the destination. Edges come in symmetric pairs, so the edge a room uses to go
    // *towards* the destination is the partner of the edge the search arrived over.
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    tree->dist[static_cast<std::size_t>(dest)] = 0.0f;
    open.emplace(0.0f, dest);
    while (!open.empty()) {
        const auto [d, node] = open.top();
        open.pop();
        if (d > tree->dist[static_cast<std::size_t>(node)] + 1e-4f) continue;
        for (const int e : adjacency_[static_cast<std::size_t>(node)]) {
            const NavEdge& edge = edges_[static_cast<std::size_t>(e)];
            const float nd = d + edge.cost;
            float& cur = tree->dist[static_cast<std::size_t>(edge.to)];
            if (cur < 0.0f || nd < cur - 1e-4f) {
                cur = nd;
                tree->nextEdge[static_cast<std::size_t>(edge.to)] = edge.pairedEdge;
                open.emplace(nd, edge.to);
            }
        }
    }
    slot = std::move(tree);
    return *slot;
}

float NavGraph::Distance(RoomId from, RoomId to) const
{
    if (from < 0 || to < 0 || from >= NodeCount() || to >= NodeCount()) return -1.0f;
    return TreeTo(to).dist[static_cast<std::size_t>(from)];
}

int NavGraph::NextEdge(RoomId from, RoomId to) const
{
    if (from < 0 || to < 0 || from >= NodeCount() || to >= NodeCount() || from == to) return kNone;
    return TreeTo(to).nextEdge[static_cast<std::size_t>(from)];
}

bool NavGraph::Route(RoomId from, RoomId to, std::vector<int>& out) const
{
    out.clear();
    if (Distance(from, to) < 0.0f) return false;
    RoomId at = from;
    int guard = 0;
    while (at != to) {
        const int e = NextEdge(at, to);
        if (e == kNone || ++guard > NodeCount()) return false;
        out.push_back(e);
        at = edges_[static_cast<std::size_t>(e)].to;
    }
    return true;
}

std::vector<RoomId> NavGraph::ReachableFrom(RoomId from) const
{
    std::vector<char> seen(plan_.rooms.size(), 0);
    std::vector<RoomId> order;
    std::vector<RoomId> stack = {from};
    seen[static_cast<std::size_t>(from)] = 1;
    while (!stack.empty()) {
        const RoomId r = stack.back();
        stack.pop_back();
        order.push_back(r);
        for (const int e : adjacency_[static_cast<std::size_t>(r)]) {
            const RoomId t = edges_[static_cast<std::size_t>(e)].to;
            if (!seen[static_cast<std::size_t>(t)]) {
                seen[static_cast<std::size_t>(t)] = 1;
                stack.push_back(t);
            }
        }
    }
    return order;
}

// ------------------------------------------------------------------------------------------------------------

float PathBuilder::BaseY(RoomId room) const
{
    return graph_.Plan().FloorY(graph_.Plan().room(room).level);
}

Vec2 PathBuilder::Approach(const Door& door, RoomId from, float distance)
{
    // The door's normal points from a into b; the approach point lies on `from`'s side of the wall.
    const Vec2 n = door.Normal();
    return from == door.a ? door.pos - n * distance : door.pos + n * distance;
}

void PathBuilder::AddRoomCrossing(const Room& room, Vec2 enter, Vec2 leave, bool, bool, std::vector<Waypoint>& out, float lane) const
{
    // In a corridor, walk on the right-hand side: add two lane points between the portals.
    if (!room.IsCorridor() || lane <= 0.0f) return;
    const Vec2 delta = leave - enter;
    const bool alongX = room.rect.Width() >= room.rect.Depth();
    const float axis = alongX ? delta.x : delta.z;
    if (std::fabs(axis) < 4.0f) return;
    const float sign = axis > 0.0f ? 1.0f : -1.0f;
    const Vec2 c = room.rect.Center();
    // Heading east the right-hand side is south (+z); heading south it is west (-x).
    const float laneCoordinate = alongX ? c.z + sign * lane : c.x - sign * lane;
    const float y = BaseY(room.id);
    const auto at = [&](float along) { return alongX ? Vec2{along, laneCoordinate} : Vec2{laneCoordinate, along}; };
    const float from = alongX ? enter.x : enter.z;
    const float to = alongX ? leave.x : leave.z;
    const Vec2 p1 = room.rect.Clamp(at(from + sign * 1.5f), 0.3f);
    const Vec2 p2 = room.rect.Clamp(at(to - sign * 1.5f), 0.3f);
    out.push_back({Vec3(p1, y), room.id, kNone, WaypointKind::Walk});
    out.push_back({Vec3(p2, y), room.id, kNone, WaypointKind::Walk});
}

void PathBuilder::AddStairs(const NavEdge& edge, Vec2, std::vector<Waypoint>& out) const
{
    const SchoolPlan& plan = graph_.Plan();
    const StairShaft& s = plan.stairs[static_cast<std::size_t>(edge.stair)];
    const RoomId lower = edge.upward ? edge.from : edge.to;
    const int level = plan.room(lower).level;
    const std::vector<Vec3> climb = StairClimbPath(plan, s, level, edge.upward);
    // The first point is the centre of the entry landing of the room we are leaving; skip it (we are there).
    for (std::size_t i = 1; i < climb.size(); ++i) {
        const bool last = i + 1 == climb.size();
        out.push_back({climb[i], last ? edge.to : edge.from, kNone, last ? WaypointKind::Walk : WaypointKind::StairStep});
    }
}

bool PathBuilder::Build(RoomId startRoom, Vec2 startPos, RoomId endRoom, Vec2 endPos, std::vector<Waypoint>& out, float laneOffset) const
{
    const SchoolPlan& plan = graph_.Plan();
    std::vector<int> route;
    if (!graph_.Route(startRoom, endRoom, route)) return false;
    Vec2 here = startPos;
    RoomId room = startRoom;
    for (std::size_t i = 0; i < route.size(); ++i) {
        const NavEdge& e = graph_.Edge(route[i]);
        const Room& current = plan.room(room);
        if (e.stair != kNone) {
            // Walk to the middle of the entry landing, then climb.
            const Vec3 landing = Vec3(current.rect.Center(), BaseY(room));
            out.push_back({landing, room, kNone, WaypointKind::Walk});
            AddStairs(e, here, out);
            room = e.to;
            here = plan.room(room).rect.Center();
            continue;
        }
        const Door& d = plan.door(e.door);
        const Vec2 approach = Approach(d, room, 0.9f);
        const float y = BaseY(room);
        AddRoomCrossing(current, here, approach, false, true, out, laneOffset);
        out.push_back({Vec3(approach, y), room, d.id, WaypointKind::DoorApproach});
        out.push_back({Vec3(d.pos, y), room, d.id, WaypointKind::Doorway});
        const RoomId next = e.to;
        const Vec2 exitPoint = Approach(d, next, 0.9f);
        out.push_back({Vec3(exitPoint, BaseY(next)), next, d.id, WaypointKind::DoorExit});
        room = next;
        here = exitPoint;
    }
    AddRoomCrossing(plan.room(room), here, endPos, true, false, out, laneOffset);
    out.push_back({Vec3(endPos, BaseY(room)), room, kNone, WaypointKind::Spot});
    return true;
}

}  // namespace School
