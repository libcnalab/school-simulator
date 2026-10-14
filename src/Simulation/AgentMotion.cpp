// Trips, fidelity levels, full-fidelity walking, doors and the fire drill.

#include <algorithm>
#include <cmath>

#include "Simulation/SchoolSim.hpp"
#include "World/Rng.hpp"

namespace School {

namespace {
constexpr float kPi = 3.14159265358979f;

float YawOfDir(Vec2 d) { return std::atan2(-d.x, -d.z); }

float DoorInterval(DoorKind k)
{
    switch (k) {
    case DoorKind::Single: return 0.55f;
    case DoorKind::Double:
    case DoorKind::External:
    case DoorKind::Automatic: return 0.28f;
    case DoorKind::Gate: return 0.15f;
    default: return 0.1f;
    }
}
}  // namespace

// --- departures -----------------------------------------------------------------------------------------------------------------

void SchoolSim::ScheduleDeparture(int npc, const Goal& goal, double departAt)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    a.goal = goal;
    a.departAt = departAt;
    ++a.epoch;
    events_.push({departAt, npc, a.epoch, Event::Depart});
}

void SchoolSim::ProcessEventsUntil(double t)
{
    while (!events_.empty() && events_.top().time <= t + 1e-9) {
        const Event e = events_.top();
        events_.pop();
        Agent& a = agents_[static_cast<std::size_t>(e.npc)];
        if (e.type == Event::Tick) {
            if (a.mode == Mode::Offsite || drill_.active) continue;
            // A new patrol stop, unless the person has a more important goal (it is their goal for this phase).
            const Npc& n = pop_.npcs[static_cast<std::size_t>(e.npc)];
            const Phase& p = schedule_.At(phase_);
            if (p.kind == PhaseKind::Night) continue;
            if (p.kind == PhaseKind::Closing && n.role != Role::Janitor) continue;
            const std::uint64_t salt = Hash(settings_.seed, static_cast<std::uint64_t>(day_), static_cast<std::uint64_t>(t / 60.0), static_cast<std::uint64_t>(e.npc));
            const Goal g = PatrolGoal(n, salt);
            if (a.mode == Mode::AtSpot) ScheduleDeparture(e.npc, g, t);
            else Retarget(e.npc, g, t);
            const double next = t + 240.0 + 420.0 * Unit(Hash(salt, 99));
            events_.push({next, e.npc, a.epoch, Event::Tick});
            continue;
        }
        if (e.epoch != a.epoch) continue;
        ++stats_.events;
        if (e.type == Event::Depart) {
            Depart(e.npc);
        } else if (e.type == Event::Arrive) {
            if (a.mode == Mode::Transit && a.fidelity != Fidelity::Full) Arrive(e.npc);
        }
    }
}

void SchoolSim::LeaveSpot(int npc)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    if (a.mode == Mode::AtSpot && a.room != kNone) --occupancy_[static_cast<std::size_t>(a.room)];
}

void SchoolSim::Depart(int npc)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    a.departAt = -1.0;
    const Goal goal = a.goal;
    if (a.mode == Mode::Offsite) {
        if (goal.room == plan_.offsite || goal.room == kNone) return;
        // Arrive from the street through one of the campus gates.
        std::vector<DoorId> gates;
        for (const DoorId d : plan_.room(plan_.offsite).doors) gates.push_back(d);
        if (gates.empty()) return;
        const DoorId gate = gates[Hash(settings_.seed, static_cast<std::uint64_t>(npc), static_cast<std::uint64_t>(day_)) % gates.size()];
        const Door& d = plan_.door(gate);
        const RoomId inside = d.Other(plan_.offsite);
        const Vec2 start = PathBuilder::Approach(d, inside, 0.9f);
        a.room = inside;
        a.pos = Vec3(start, paths_.BaseY(inside));
        a.hasSpot = false;
        if (BuildJourney(npc, inside, a.pos, goal, time_)) return;
        a.room = kNone;
        return;
    }
    if (a.mode == Mode::AtSpot) {
        LeaveSpot(npc);
        BuildJourney(npc, a.room, a.pos, goal, time_);
    }
}

namespace {
struct InsertPoint {
    std::size_t at;
    std::vector<Vec2> points;
    RoomId room;
};
}  // namespace

bool SchoolSim::BuildJourney(int npc, RoomId startRoom, Vec3 startPos, const Goal& goal, double startTime)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    const Npc& n = pop_.npcs[static_cast<std::size_t>(npc)];
    ++stats_.pathBuilds;
    Goal g = goal;
    RoomId endRoom = g.room;
    Vec2 endPos = g.pos;
    if (endRoom == plan_.offsite || endRoom == kNone) {
        // Leave through a gate of the street: the end of the walk is a point just outside it.
        const std::vector<DoorId>& gates = plan_.room(plan_.offsite).doors;
        if (gates.empty()) return false;
        const DoorId gate = gates[Hash(settings_.seed, static_cast<std::uint64_t>(npc), static_cast<std::uint64_t>(day_), 5) % gates.size()];
        const Door& d = plan_.door(gate);
        endRoom = plan_.offsite;
        endPos = PathBuilder::Approach(d, plan_.offsite, 1.6f);
        g.room = plan_.offsite;
        g.pos = endPos;
    }
    std::vector<Waypoint> pts;
    pts.push_back({startPos, startRoom, kNone, WaypointKind::Walk});
    const PathBuilder& builder = (liftPaths_ && n.usesElevator && !drill_.active) ? *liftPaths_ : paths_;
    if (!builder.Build(startRoom, startPos.XZ(), endRoom, endPos, pts)) {
        ++stats_.unreachable;
        // Fall back so nobody is lost: they appear at their destination.
        a.goal = g;
        Arrive(npc);
        return false;
    }
    // Aisles at the start (out of a seat) and at the end (to the seat), through each room's own lanes.
    if (a.hasSpot && a.mode == Mode::AtSpot) {
        const RoomLayout& l = Layout(startRoom);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            if (pts[i].room != startRoom && pts[i].kind != WaypointKind::DoorApproach) continue;
            const Vec2 towards = pts[i].pos.XZ();
            const std::vector<Vec2> aisle = l.PathFrom(a.spot, towards);
            std::vector<Waypoint> add;
            for (const Vec2& p : aisle) add.push_back({Vec3(p, startPos.y), startRoom, kNone, WaypointKind::Walk});
            pts.insert(pts.begin() + static_cast<std::ptrdiff_t>(i), add.begin(), add.end());
            break;
        }
    }
    if (endRoom != plan_.offsite && pts.size() >= 2) {
        const RoomLayout& l = Layout(endRoom);
        const Spot spot{g.pos, g.yaw, SpotKind::Seat, g.pose};
        const Waypoint last = pts.back();
        pts.pop_back();
        Vec2 entry = pts.back().pos.XZ();
        // If the previous point is not in the goal room, enter at the hub.
        if (pts.back().room != endRoom) entry = l.hub;
        for (const Vec2& p : l.PathTo(entry, spot)) pts.push_back({Vec3(p, last.pos.y), endRoom, kNone, WaypointKind::Walk});
        pts.push_back(last);
    }
    for (const Waypoint& w : pts)
        if (w.kind == WaypointKind::Ride) {
            ++stats_.elevatorJourneys;
            break;
        }
    auto journey = std::make_unique<Journey>();
    journey->pts = std::move(pts);
    journey->cum.assign(journey->pts.size(), 0.0f);
    const float speed = n.speed * (g.activity == Activity::Evacuating || drill_.active ? 1.35f : 1.0f);
    for (std::size_t i = 1; i < journey->pts.size(); ++i) {
        const Vec3 d = journey->pts[i].pos - journey->pts[i - 1].pos;
        const bool ride = journey->pts[i].kind == WaypointKind::Ride && journey->pts[i - 1].kind == WaypointKind::Ride;
        if (ride) {
            // A level by elevator takes a wait plus the travel, counted in metres at this walker's speed.
            const float levels = std::fabs(d.y) / kFloorHeight;
            journey->cum[i] = journey->cum[i - 1] + (12.0f + 6.0f * levels) * speed;
        } else {
            journey->cum[i] = journey->cum[i - 1] + std::sqrt(d.x * d.x + d.z * d.z) + std::fabs(d.y) * 0.3f;   // stairs are slower: climbing counts extra
        }
    }
    journey->start = startTime;
    journey->speed = speed;
    const double end = journey->start + static_cast<double>(journey->Total() / journey->speed);
    a.journey = std::move(journey);
    a.mode = Mode::Transit;
    a.room = startRoom;
    a.activity = drill_.active ? Activity::Evacuating : Activity::Walking;
    a.goal = g;
    a.fidelity = Fidelity::Abstract;
    a.leg = 1;
    a.vel = Vec2{};
    a.waitTimer = a.stuckTimer = 0.0f;
    a.stuckRef = startPos.XZ();
    a.pos = startPos;
    if (a.riding != kNone) {
        ElevatorRuntime& r = elevators_[static_cast<std::size_t>(a.riding)];
        r.passengers = std::max(0, r.passengers - 1);
        a.riding = kNone;
    }
    a.rideTo = -1;
    ++a.epoch;
    events_.push({end, npc, a.epoch, Event::Arrive});
    return true;
}

void SchoolSim::SetSpot(int npc, const Goal& g)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    a.spot = Spot{g.pos, g.yaw, SpotKind::Seat, g.pose};
    a.hasSpot = true;
}

void SchoolSim::Arrive(int npc)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    const Npc& n = pop_.npcs[static_cast<std::size_t>(npc)];
    if (a.fidelity == Fidelity::Full) {
        a.fidelity = Fidelity::Abstract;
        fullList_.erase(std::remove(fullList_.begin(), fullList_.end(), npc), fullList_.end());
    }
    const Goal g = a.goal;
    a.journey.reset();
    if (a.riding != kNone) {
        elevators_[static_cast<std::size_t>(a.riding)].passengers = std::max(0, elevators_[static_cast<std::size_t>(a.riding)].passengers - 1);
        a.riding = kNone;
    }
    ++a.epoch;
    if (g.room == plan_.offsite || g.room == kNone) {
        a.mode = Mode::Offsite;
        a.room = kNone;
        a.activity = Activity::Offsite;
        a.hasSpot = false;
        a.pos = Vec3(0, 0, 0);
        return;
    }
    a.mode = Mode::AtSpot;
    a.room = g.room;
    a.pos = Vec3(g.pos, paths_.BaseY(g.room));
    a.yaw = g.yaw;
    a.pose = g.pose;
    a.activity = g.activity;
    a.vel = Vec2{};
    SetSpot(npc, g);
    ++occupancy_[static_cast<std::size_t>(g.room)];
    if (drill_.active && g.activity == Activity::Assembled) {
        ++drill_.assembled;
        if (drill_.assembled >= drill_.total && drill_.allAssembledAt < 0.0) {
            drill_.allAssembledAt = time_;
            drill_.lastDuration = time_ - drill_.startedAt;
        }
    }
    if (n.role == Role::Student && g.activity == Activity::InLesson && schedule_.At(phase_).kind == PhaseKind::Lesson && time_ - schedule_.At(phase_).start > 30.0 && time_ > drillReturnUntil_)
        ++stats_.lateArrivals;
    if (g.dine) {
        // Served after a wait that depends on the line; then to a table.
        const std::vector<RoomId> cafs = plan_.RoomsOfType(RoomType::Cafeteria);
        const RoomLayout& l = Layout(g.room);
        const int order = dineCounter_++;
        const double wait = 6.0 + static_cast<double>(order) * 0.45 + 8.0 * static_cast<double>(Unit(Hash(settings_.seed, static_cast<std::uint64_t>(npc), 41)));
        Goal eat;
        eat.room = g.room;
        eat.activity = Activity::Eating;
        eat.pose = Pose::Eat;
        if (!l.eat.empty()) {
            const Spot& s = l.eat[static_cast<std::size_t>(order) % l.eat.size()];
            eat.pos = s.pos;
            eat.yaw = s.yaw;
        } else {
            eat.pos = g.pos;
        }
        (void)cafs;
        ScheduleDeparture(npc, eat, time_ + wait);
    }
}

Vec3 SchoolSim::EvaluateJourney(const Agent& a, double t, RoomId* room, float* yaw, float* distance) const
{
    const Journey& j = *a.journey;
    float d = static_cast<float>(std::max(0.0, t - j.start)) * j.speed;
    d = std::min(d, j.Total());
    if (distance) *distance = d;
    const auto it = std::upper_bound(j.cum.begin(), j.cum.end(), d);
    std::size_t seg = it == j.cum.begin() ? 0 : static_cast<std::size_t>(it - j.cum.begin()) - 1;
    if (seg + 1 >= j.pts.size()) seg = j.pts.size() - 2 < j.pts.size() ? j.pts.size() - 2 : 0;
    if (j.pts.size() < 2) {
        if (room) *room = j.pts.front().room;
        return j.pts.front().pos;
    }
    const float len = j.cum[seg + 1] - j.cum[seg];
    const float f = len > 1e-4f ? std::clamp((d - j.cum[seg]) / len, 0.0f, 1.0f) : 1.0f;
    const Vec3 p0 = j.pts[seg].pos, p1 = j.pts[seg + 1].pos;
    if (room) *room = f < 0.5f ? j.pts[seg].room : j.pts[seg + 1].room;
    if (yaw) *yaw = YawOfDir(Vec2{p1.x - p0.x, p1.z - p0.z});
    return p0 + (p1 - p0) * f;
}

void SchoolSim::Retarget(int npc, const Goal& goal, double departAt)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    if (a.mode != Mode::Transit || !a.journey) {
        ScheduleDeparture(npc, goal, departAt);
        return;
    }
    // Where is the walker right now?
    RoomId room = a.room;
    Vec3 pos = a.pos;
    if (a.fidelity != Fidelity::Full) pos = EvaluateJourney(a, time_, &room, nullptr, nullptr);
    else if (a.leg >= 1 && a.leg < static_cast<int>(a.journey->pts.size())) room = a.journey->pts[static_cast<std::size_t>(a.leg)].room;
    if (a.fidelity == Fidelity::Full) {
        a.fidelity = Fidelity::Abstract;
        fullList_.erase(std::remove(fullList_.begin(), fullList_.end(), npc), fullList_.end());
    }
    // Stay inside the room's rectangle.
    const Rect& r = plan_.room(room).rect;
    if (!r.Inflated(0.6f).Contains(pos.XZ())) pos = Vec3(r.Clamp(pos.XZ(), 0.2f), pos.y);
    a.hasSpot = false;
    a.goal = goal;
    a.room = room;
    a.pos = pos;
    a.journey.reset();
    ++a.epoch;
    if (departAt <= time_ + 1e-6) {
        if (!BuildJourney(npc, room, pos, goal, time_)) return;
    } else {
        // pause where they stand (as if stationary), then go
        a.mode = Mode::AtSpot;
        ++occupancy_[static_cast<std::size_t>(room)];
        ScheduleDeparture(npc, goal, departAt);
    }
}

// --- fidelity -----------------------------------------------------------------------------------------------------------------------

void SchoolSim::Promote(int npc, Fidelity to)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    if (a.fidelity == to || !a.journey) return;
    if (to == Fidelity::Full) {
        float dist = 0.0f;
        RoomId room = a.room;
        float yaw = a.yaw;
        a.pos = EvaluateJourney(a, time_, &room, &yaw, &dist);
        a.room = room;
        a.yaw = yaw;
        const auto it = std::upper_bound(a.journey->cum.begin(), a.journey->cum.end(), dist);
        a.leg = std::clamp(static_cast<int>(it - a.journey->cum.begin()), 1, static_cast<int>(a.journey->pts.size()) - 1);
        a.vel = Vec2{};
        a.waitTimer = a.stuckTimer = 0.0f;
        a.stuckRef = a.pos.XZ();
        ++a.epoch;   // the abstract arrival event is void now
        fullList_.push_back(npc);
    }
    a.fidelity = to;
}

void SchoolSim::Demote(int npc, Fidelity to)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    if (a.fidelity == to || !a.journey) {
        if (a.fidelity == Fidelity::Full) fullList_.erase(std::remove(fullList_.begin(), fullList_.end(), npc), fullList_.end());
        a.fidelity = to;
        return;
    }
    if (a.fidelity == Fidelity::Full) {
        if (a.riding != kNone) {
            elevators_[static_cast<std::size_t>(a.riding)].passengers = std::max(0, elevators_[static_cast<std::size_t>(a.riding)].passengers - 1);
            a.riding = kNone;
        }
        // Continue analytically from where the walker is: shift the start so the arithmetic agrees.
        Journey& j = *a.journey;
        const std::size_t prev = static_cast<std::size_t>(std::max(0, a.leg - 1));
        const Vec3& p0 = j.pts[prev].pos;
        const float dist = j.cum[prev] + std::sqrt((a.pos.x - p0.x) * (a.pos.x - p0.x) + (a.pos.z - p0.z) * (a.pos.z - p0.z));
        j.start = time_ - static_cast<double>(dist / j.speed);
        fullList_.erase(std::remove(fullList_.begin(), fullList_.end(), npc), fullList_.end());
        ++a.epoch;
        events_.push({j.start + static_cast<double>(j.Total() / j.speed), npc, a.epoch, Event::Arrive});
    }
    a.fidelity = to;
}

void SchoolSim::UpdateFidelity()
{
    std::fill(fullInRoom_.begin(), fullInRoom_.end(), 0);
    if (!settings_.enableFull || focus_.empty()) {
        for (const int id : std::vector<int>(fullList_)) Demote(id, Fidelity::Abstract);
        for (Agent& a : agents_)
            if (a.fidelity == Fidelity::Reduced) a.fidelity = Fidelity::Abstract;
        return;
    }
    struct Candidate {
        float score;
        int npc;
    };
    std::vector<Candidate> want;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        Agent& a = agents_[i];
        if (a.mode != Mode::Transit || !a.journey) continue;
        RoomId room = a.room;
        Vec3 pos = a.pos;
        if (a.fidelity != Fidelity::Full) {
            pos = EvaluateJourney(a, time_, &room, nullptr, nullptr);
            a.room = room;
            a.pos = pos;
        }
        const int level = plan_.room(a.room).level;
        float best = 1e9f;
        bool sameLevel = false;
        for (const Focus& f : focus_) {
            const float d = Distance(pos.XZ(), f.pos) + 25.0f * static_cast<float>(std::abs(level - f.level));
            if (d < best) {
                best = d;
                sameLevel = level == f.level;
            }
        }
        const float limit = a.fidelity == Fidelity::Full ? settings_.fullRadius * 1.3f : settings_.fullRadius;
        if (best < limit) want.push_back({best, static_cast<int>(i)});
        else if (best < settings_.reducedRadius || sameLevel) {
            if (a.fidelity == Fidelity::Full) Demote(static_cast<int>(i), Fidelity::Reduced);
            a.fidelity = Fidelity::Reduced;
        } else {
            if (a.fidelity == Fidelity::Full) Demote(static_cast<int>(i), Fidelity::Abstract);
            a.fidelity = Fidelity::Abstract;
        }
    }
    std::sort(want.begin(), want.end(), [](const Candidate& x, const Candidate& y) { return x.score < y.score; });
    for (std::size_t k = 0; k < want.size(); ++k) {
        const int id = want[k].npc;
        if (static_cast<int>(k) < settings_.maxFull) {
            Promote(id, Fidelity::Full);
        } else if (agents_[static_cast<std::size_t>(id)].fidelity == Fidelity::Full) {
            Demote(id, Fidelity::Reduced);
        } else {
            agents_[static_cast<std::size_t>(id)].fidelity = Fidelity::Reduced;
        }
    }
}

// --- doors ----------------------------------------------------------------------------------------------------------------------------

void SchoolSim::RequestDoor(DoorId door, float seconds)
{
    DoorRuntime& d = doors_[static_cast<std::size_t>(door)];
    d.wantOpenUntil = std::max(d.wantOpenUntil, time_ + static_cast<double>(seconds));
}

void SchoolSim::UpdateDoors(float dt)
{
    for (std::size_t i = 0; i < doors_.size(); ++i) {
        const Door& def = plan_.doors[i];
        DoorRuntime& d = doors_[i];
        if (!def.Leaf()) {
            d.open = 1.0f;
            continue;
        }
        if (def.kind == DoorKind::ElevatorDoor) continue;   // driven by the elevator
        const float before = d.open;
        const float target = time_ < d.wantOpenUntil ? 1.0f : 0.0f;
        const float rate = def.kind == DoorKind::Automatic ? 2.5f : 2.0f;
        if (d.open < target) d.open = std::min(target, d.open + rate * dt);
        else if (d.open > target) d.open = std::max(target, d.open - rate * 0.6f * dt);
        if (before < 0.02f && d.open >= 0.02f) doorSounds_.push_back({def.id, true});
        if (before > 0.01f && d.open <= 0.01f) doorSounds_.push_back({def.id, false});
        d.busy = std::max(0.0f, d.busy - dt);
    }
}

// --- elevators ---------------------------------------------------------------------------------------------------------------------------

void SchoolSim::UpdateElevators(float dt)
{
    using State = ElevatorRuntime::State;
    constexpr float kSpeed = 1.4f;        // m/s
    constexpr float kDoorTime = 0.9f;     // seconds to open or close
    constexpr float kDwell = 3.5f;
    for (std::size_t i = 0; i < elevators_.size(); ++i) {
        ElevatorRuntime& r = elevators_[i];
        const ElevatorShaft& e = plan_.elevators[i];
        const int levels = static_cast<int>(e.cab.size());
        const int here = r.level - e.levelLow;
        const auto anyCall = [&](int from, int step) {
            for (int k = from; k >= 0 && k < levels; k += step)
                if (r.calls[static_cast<std::size_t>(k)]) return k;
            return -1;
        };
        switch (r.state) {
        case State::Idle: {
            if (r.calls[static_cast<std::size_t>(here)]) {
                r.state = State::Opening;
                if (r.doorIds[static_cast<std::size_t>(here)] != kNone) doorSounds_.push_back({r.doorIds[static_cast<std::size_t>(here)], true});
                break;
            }
            int next = anyCall(here + r.direction, r.direction);
            if (next < 0) {
                r.direction = -r.direction;
                next = anyCall(here + r.direction, r.direction);
            }
            if (next >= 0) {
                r.target = e.levelLow + next;
                r.state = State::Moving;
            }
            break;
        }
        case State::Opening:
            r.door = std::min(1.0f, r.door + dt / kDoorTime);
            if (r.door >= 1.0f) {
                r.state = State::Open;
                r.timer = kDwell;
                r.calls[static_cast<std::size_t>(here)] = 0;
            }
            break;
        case State::Open:
            r.calls[static_cast<std::size_t>(here)] = 0;
            r.timer -= dt;
            if (r.timer <= 0.0f) r.state = State::Closing;
            break;
        case State::Closing:
            r.door = std::max(0.0f, r.door - dt / kDoorTime);
            if (r.door <= 0.0f) {
                r.state = State::Idle;
                if (r.doorIds[static_cast<std::size_t>(here)] != kNone) doorSounds_.push_back({r.doorIds[static_cast<std::size_t>(here)], false});
            }
            break;
        case State::Moving: {
            const float targetY = plan_.FloorY(r.target);
            const float step = kSpeed * dt;
            if (std::fabs(targetY - r.y) <= step) {
                r.y = targetY;
                r.level = r.target;
                r.target = -1;
                r.state = State::Opening;
                const int idx = r.level - e.levelLow;
                if (r.doorIds[static_cast<std::size_t>(idx)] != kNone) doorSounds_.push_back({r.doorIds[static_cast<std::size_t>(idx)], true});
            } else {
                r.y += targetY > r.y ? step : -step;
                // the level the cab is nearest to, for anyone reading it mid-ride
                r.level = plan_.LevelAt(r.y + 0.6f);
                r.level = std::clamp(r.level, e.levelLow, e.levelHigh);
                if (r.target < 0) r.state = State::Idle;
            }
            break;
        }
        }
        // The cab doors on the plan follow the cab.
        for (int k = 0; k < levels; ++k) {
            const DoorId d = r.doorIds[static_cast<std::size_t>(k)];
            if (d == kNone) continue;
            doors_[static_cast<std::size_t>(d)].open = (k == r.level - e.levelLow && r.state != State::Moving) ? r.door : 0.0f;
        }
    }
}

// --- full-fidelity walking -------------------------------------------------------------------------------------------------------------

void SchoolSim::StepFullAll(float dt)
{
    if (fullList_.empty()) return;
    // The walk cycle needs a small step for stability; split a long frame.
    const float h = settings_.fixedStep;
    float left = dt;
    while (left > 1e-6f) {
        const float s = std::min(left, h * 1.5f);
        // iterate over a copy: agents arrive (and leave the list) while stepping
        const std::vector<int> ids = fullList_;
        // spatial hash of this step
        hashCells_.clear();
        std::vector<std::pair<std::uint64_t, int>> keyed;
        keyed.reserve(ids.size());
        for (const int id : ids) {
            const Agent& a = agents_[static_cast<std::size_t>(id)];
            const int cx = static_cast<int>(std::floor(a.pos.x));
            const int cz = static_cast<int>(std::floor(a.pos.z));
            const int cl = static_cast<int>(std::floor((a.pos.y + 0.5f) / kFloorHeight));
            const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx + 4096)) << 32) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cz + 4096)) << 8) | static_cast<std::uint64_t>(cl + 4);
            keyed.emplace_back(key, id);
        }
        std::sort(keyed.begin(), keyed.end());
        grid_.assign(keyed.size(), 0);
        for (std::size_t i = 0; i < keyed.size(); ++i) grid_[i] = keyed[i].second;
        // neighbour lookup closure
        stepKeys_ = &keyed;
        for (const int id : ids) {
            if (agents_[static_cast<std::size_t>(id)].mode == Mode::Transit && agents_[static_cast<std::size_t>(id)].fidelity == Fidelity::Full) StepFull(id, s);
        }
        stepKeys_ = nullptr;
        left -= s;
    }
    std::fill(fullInRoom_.begin(), fullInRoom_.end(), 0);
    for (const int id : fullList_) {
        const Agent& a = agents_[static_cast<std::size_t>(id)];
        if (a.room != kNone) ++fullInRoom_[static_cast<std::size_t>(a.room)];
    }
}

void SchoolSim::StepFull(int npc, float dt)
{
    Agent& a = agents_[static_cast<std::size_t>(npc)];
    Journey& j = *a.journey;
    const Npc& n = pop_.npcs[static_cast<std::size_t>(npc)];
    const std::size_t count = j.pts.size();
    if (a.leg >= static_cast<int>(count)) {
        Arrive(npc);
        return;
    }
    const Waypoint& wp = j.pts[static_cast<std::size_t>(a.leg)];
    const Vec2 target = wp.pos.XZ();
    Vec2 to = target - a.pos.XZ();
    const float dist = to.Length();
    float speed = j.speed;
    const bool onStairs = wp.kind == WaypointKind::StairStep || (a.leg > 0 && j.pts[static_cast<std::size_t>(a.leg - 1)].kind == WaypointKind::StairStep);
    if (onStairs) speed *= 0.8f;

    // --- elevator ride: wait inside the cab until it has carried us to the level of the last ride point
    if (wp.kind == WaypointKind::Ride) {
        const Room& cabRoom = plan_.room(wp.room);
        const int shaft = cabRoom.elevatorId;
        if (shaft != kNone && a.leg > 0 && j.pts[static_cast<std::size_t>(a.leg - 1)].kind == WaypointKind::Ride) {
            // We are in the cab and this point is on another level.
            ElevatorRuntime& r = elevators_[static_cast<std::size_t>(shaft)];
            const ElevatorShaft& e = plan_.elevators[static_cast<std::size_t>(shaft)];
            std::size_t last = static_cast<std::size_t>(a.leg);
            while (last + 1 < count && j.pts[last + 1].kind == WaypointKind::Ride) ++last;
            const int destLevel = plan_.room(j.pts[last].room).level;
            if (a.riding == kNone) {
                a.riding = shaft;
                ++r.passengers;
            }
            if (a.rideTo != destLevel) {
                a.rideTo = destLevel;
                CallElevator(shaft, destLevel);
            }
            // Carried by the cab: stand still at its height, in the cab room of the level it is at.
            a.vel = Vec2{};
            a.pos.y = r.y;
            const RoomId cabHere = e.cab[static_cast<std::size_t>(std::clamp(r.level - e.levelLow, 0, static_cast<int>(e.cab.size()) - 1))];
            if (cabHere != kNone) a.room = cabHere;
            a.stuckTimer = 0.0f;
            if (r.level == destLevel && r.state == ElevatorRuntime::State::Open) {
                a.leg = static_cast<int>(last) + 1;
                a.pos = j.pts[last].pos;
                a.room = j.pts[last].room;
                a.rideTo = -1;
                if (a.leg >= static_cast<int>(count)) Arrive(npc);
            }
            return;
        }
        // The first ride point (the cab we just entered): walk to it like any other point.
    }

    // --- door gate: wait at the approach point until the door is open and it is our turn
    constexpr float kGateReach = 0.8f;   // a crowd at a door: whoever is within this of the approach point is "at" the door
    if (wp.kind == WaypointKind::DoorApproach && dist < kGateReach && plan_.door(wp.door).kind == DoorKind::ElevatorDoor) {
        const DoorId did = wp.door;
        const Door& def = plan_.door(did);
        const RoomId cab = plan_.room(def.a).type == RoomType::ElevatorCab ? def.a : def.b;
        const int shaft = plan_.room(cab).elevatorId;
        ElevatorRuntime& r = elevators_[static_cast<std::size_t>(shaft)];
        const int level = plan_.room(cab).level;
        const bool inside = wp.room == cab;
        if (!inside) CallElevator(shaft, level);
        const bool here = r.level == level && r.state == ElevatorRuntime::State::Open;
        const bool ready = here && (inside || r.passengers < 8);
        if (!ready && a.waitTimer < 60.0f) {
            a.waitTimer += dt;
            a.vel = a.vel * 0.5f;
            a.stuckTimer = 0.0f;
            return;
        }
        if (inside) {
            if (a.riding != kNone) {
                r.passengers = std::max(0, r.passengers - 1);
                a.riding = kNone;
            }
            ++stats_.elevatorRides;
        } else {
            a.riding = shaft;
            ++r.passengers;
        }
        r.timer = std::max(r.timer, 2.0f);   // hold the doors for the person in them
        ++stats_.doorPasses;
        a.waitTimer = 0.0f;
        ++a.leg;
        return;
    }
    if (wp.kind == WaypointKind::DoorApproach && dist < kGateReach) {
        const DoorId did = wp.door;
        const Door& def = plan_.door(did);
        DoorRuntime& d = doors_[static_cast<std::size_t>(did)];
        const int dir = wp.room == def.a ? 0 : 1;
        RequestDoor(did, 2.0f);
        const bool ready = d.open >= 0.7f && time_ >= d.nextFree[dir];
        if (!ready && a.waitTimer < 20.0f) {
            a.waitTimer += dt;
            a.vel = a.vel * 0.5f;
            a.stuckTimer = 0.0f;
            // The line behind the door is as long as the people waiting at it.
            d.busy = 1.0f;
            return;
        }
        d.nextFree[dir] = time_ + static_cast<double>(DoorInterval(def.kind));
        ++d.passes;
        ++stats_.doorPasses;
        a.waitTimer = 0.0f;
        ++a.leg;
        return;
    }
    if (wp.kind == WaypointKind::Doorway && plan_.door(wp.door).kind != DoorKind::ElevatorDoor) RequestDoor(wp.door, 1.2f);

    // --- steer
    Vec2 desired{};
    if (dist > 1e-4f) desired = to * (speed / dist);
    // separation from neighbours (same level, within 0.8 m)
    Vec2 push{};
    if (stepKeys_ != nullptr) {
        const int cx = static_cast<int>(std::floor(a.pos.x));
        const int cz = static_cast<int>(std::floor(a.pos.z));
        const int cl = static_cast<int>(std::floor((a.pos.y + 0.5f) / kFloorHeight));
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx + dx + 4096)) << 32) |
                                          (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cz + dz + 4096)) << 8) | static_cast<std::uint64_t>(cl + 4);
                auto it = std::lower_bound(stepKeys_->begin(), stepKeys_->end(), std::make_pair(key, -1));
                for (; it != stepKeys_->end() && it->first == key; ++it) {
                    if (it->second == npc) continue;
                    const Agent& o = agents_[static_cast<std::size_t>(it->second)];
                    const Vec2 away = a.pos.XZ() - o.pos.XZ();
                    const float d2 = away.LengthSq();
                    if (d2 < 0.64f && d2 > 1e-6f) {
                        const float d = std::sqrt(d2);
                        push += away * ((0.8f - d) / d * 1.6f);
                    }
                }
            }
    }
    // A person in the way in the direction of travel slows us a little: crowds shuffle instead of overlapping.
    const float pushLen = push.Length();
    if (pushLen > 2.0f) push = push * (2.0f / pushLen);
    desired += push;
    // smooth
    const float blend = 1.0f - std::exp(-dt * 7.0f);
    a.vel = a.vel + (desired - a.vel) * blend;
    const float vlen = a.vel.Length();
    const float vmax = speed * 1.25f;
    if (vlen > vmax) a.vel = a.vel * (vmax / vlen);
    Vec2 next = a.pos.XZ() + a.vel * dt;
    // keep inside the current room, except in a doorway
    bool inDoorChannel = false;
    if (wp.door != kNone) inDoorChannel = Distance(next, plan_.door(wp.door).pos) < 1.4f;
    const RoomId prevRoom = a.leg > 0 ? j.pts[static_cast<std::size_t>(a.leg - 1)].room : wp.room;
    if (!inDoorChannel) {
        // still on the previous room's side of a door: stay inside that room; otherwise inside the waypoint's room
        const bool inPrev = prevRoom != wp.room && plan_.room(prevRoom).rect.Inflated(0.05f).Contains(a.pos.XZ());
        next = plan_.room(inPrev ? prevRoom : wp.room).rect.Clamp(next, 0.15f);
    }
    a.room = plan_.room(wp.room).rect.Inflated(0.02f).Contains(next) ? wp.room : prevRoom;
    const Vec2 moved = next - a.pos.XZ();
    a.pos.x = next.x;
    a.pos.z = next.z;
    if (vlen > 0.15f) a.yaw = YawOfDir(a.vel);
    a.walkPhase += vlen * dt * 3.2f;

    // height: follow the segment we are on
    if (a.leg > 0) {
        const Waypoint& p0 = j.pts[static_cast<std::size_t>(a.leg - 1)];
        const Vec2 seg = target - p0.pos.XZ();
        const float segLen = seg.Length();
        const float t = segLen > 1e-3f ? std::clamp(Dot(a.pos.XZ() - p0.pos.XZ(), seg) / (segLen * segLen), 0.0f, 1.0f) : 1.0f;
        a.pos.y = p0.pos.y + (wp.pos.y - p0.pos.y) * t;
    }

    // --- reached the waypoint?
    const float reach = wp.kind == WaypointKind::Spot ? 0.12f : (wp.kind == WaypointKind::Doorway || wp.kind == WaypointKind::DoorExit ? 0.3f : 0.4f);
    if (Distance(a.pos.XZ(), target) < reach || dist < reach) {
        ++a.leg;
        a.stuckTimer = 0.0f;
        a.stuckRef = a.pos.XZ();
        if (a.leg >= static_cast<int>(count)) Arrive(npc);
        return;
    }
    // --- stuck detection: no real progress for a while means a snag; hop to the next waypoint
    if (Distance(a.pos.XZ(), a.stuckRef) > 0.35f) {
        a.stuckRef = a.pos.XZ();
        a.stuckTimer = 0.0f;
    } else {
        a.stuckTimer += dt;
        if (a.stuckTimer > 7.0f) {
            ++stats_.unstuck;
            a.pos = Vec3(target, wp.pos.y);
            ++a.leg;
            a.stuckTimer = 0.0f;
            a.stuckRef = target;
            a.vel = Vec2{};
            if (a.leg >= static_cast<int>(count)) Arrive(npc);
        }
    }
    (void)moved;
    (void)n;
}

// --- fire drill ------------------------------------------------------------------------------------------------------------------------

void SchoolSim::StartDrill()
{
    if (drill_.active) return;
    const double previous = drill_.lastDuration;
    drill_ = DrillStatus{};
    drill_.lastDuration = previous;
    drill_.active = true;
    drill_.startedAt = time_;
    bells_.push_back({time_, phase_, true});
    std::fill(assemblyLoad_.begin(), assemblyLoad_.end(), 0);
    idleCounter_.clear();
    // Everyone on campus, or about to be, goes to an assembly point.
    for (const Npc& n : pop_.npcs) {
        Agent& a = agents_[static_cast<std::size_t>(n.id)];
        if (a.mode == Mode::Offsite) continue;
        ++drill_.total;
        const Goal g = AssemblyGoal(n);
        const double delay = 1.0 + 9.0 * static_cast<double>(Unit(Hash(settings_.seed, static_cast<std::uint64_t>(n.id), static_cast<std::uint64_t>(time_), 51)));
        if (a.mode == Mode::AtSpot) {
            a.goal = g;
            ScheduleDeparture(n.id, g, time_ + delay);
        } else {
            Retarget(n.id, g, time_ + delay * 0.4);
        }
        a.activity = a.mode == Mode::AtSpot ? a.activity : Activity::Evacuating;
    }
    // People already sitting down get the Evacuating activity when they set off; mark it on departure.
}

void SchoolSim::EndDrill()
{
    if (!drill_.active) return;
    const double duration = drill_.allAssembledAt >= 0.0 ? drill_.allAssembledAt - drill_.startedAt : drill_.lastDuration;
    drill_.active = false;
    drill_.lastDuration = duration;
    drillReturnUntil_ = time_ + 900.0;
    // Back to the schedule.
    idleCounter_.clear();
    AssignGoals(phase_, sub_, true);
}

void SchoolSim::CheckDrills()
{
    if (drill_.active) {
        // The drill ends by itself a few minutes after the last person has gathered.
        if (drill_.allAssembledAt >= 0.0 && time_ - drill_.allAssembledAt > 240.0) EndDrill();
        else if (time_ - drill_.startedAt > 3600.0) EndDrill();   // never suspend the day forever
        return;
    }
    for (std::size_t i = 0; i < schedule_.drills.size(); ++i) {
        const DrillEntry& d = schedule_.drills[i];
        if ((d.weekday == -1 || d.weekday == Weekday()) && time_ >= d.time && time_ < d.time + 5.0 && drillToday_ && settings_.preset != Preset::Empty) {
            drillToday_ = false;
            StartDrill();
        }
    }
}

}  // namespace School
