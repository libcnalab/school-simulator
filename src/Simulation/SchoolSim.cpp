#include "Simulation/SchoolSim.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "World/Rng.hpp"

namespace School {

const char* ActivityName(Activity a)
{
    static const char* names[] = {"off campus", "walking", "in a lesson", "teaching", "relaxing", "queueing", "eating", "working",
                                  "exercising", "evacuating", "at the assembly point", "cleaning", "patrolling", "browsing"};
    return names[static_cast<int>(a)];
}

namespace {

std::uint64_t H(std::uint64_t a, std::uint64_t b, std::uint64_t c = 0, std::uint64_t d = 0) { return Hash(a, b, c, d); }

PopulationSpec SpecOf(const SimSettings& s, int periods)
{
    PopulationSpec spec = PopulationSpec::FromPreset(s.preset, periods, s.seed);
    if (s.students >= 0) spec.students = s.students;
    return spec;
}

}  // namespace

SchoolSim::SchoolSim(const SchoolPlan& plan, const NavGraph& nav, const LayoutCache& layouts, BellSchedule schedule, const SimSettings& settings, const NavGraph* liftNav)
    : plan_(plan), nav_(nav), layouts_(layouts), schedule_(std::move(schedule)), settings_(settings), paths_(nav)
{
    if (liftNav != nullptr && liftNav->HasElevators()) liftPaths_ = std::make_unique<PathBuilder>(*liftNav);
    Init();
}

ElevatorStatus SchoolSim::Elevator(int shaft) const
{
    ElevatorStatus st;
    if (shaft < 0 || shaft >= static_cast<int>(elevators_.size())) return st;
    const ElevatorRuntime& r = elevators_[static_cast<std::size_t>(shaft)];
    st.shaft = shaft;
    st.y = r.y;
    st.level = r.level;
    st.target = r.target;
    st.doorOpen = r.door;
    st.moving = r.state == ElevatorRuntime::State::Moving;
    st.passengers = r.passengers;
    return st;
}

void SchoolSim::CallElevator(int shaft, int level)
{
    if (shaft < 0 || shaft >= static_cast<int>(elevators_.size())) return;
    const ElevatorShaft& e = plan_.elevators[static_cast<std::size_t>(shaft)];
    if (level < e.levelLow || level > e.levelHigh) return;
    ElevatorRuntime& r = elevators_[static_cast<std::size_t>(shaft)];
    const std::size_t idx = static_cast<std::size_t>(level - e.levelLow);
    // A call for the level the cab is standing at with its doors open just holds them a little longer.
    if (level == r.level && r.state == ElevatorRuntime::State::Open) {
        r.timer = std::max(r.timer, 2.0f);
        return;
    }
    r.calls[idx] = 1;
}

void SchoolSim::Init()
{
    pop_ = BuildPopulation(plan_, SpecOf(settings_, std::max(1, schedule_.LessonCount())));
    agents_.resize(pop_.npcs.size());
    doors_.assign(plan_.doors.size(), DoorRuntime{});
    occupancy_.assign(plan_.rooms.size(), 0);
    fullInRoom_.assign(plan_.rooms.size(), 0);
    assemblyLoad_.assign(plan_.assemblyPoints.size(), 0);
    classRank_.assign(pop_.npcs.size(), 0);
    elevators_.clear();
    for (const ElevatorShaft& e : plan_.elevators) {
        ElevatorRuntime r;
        r.y = plan_.FloorY(e.levelLow);
        r.level = e.levelLow;
        r.calls.assign(e.cab.size(), 0);
        for (const RoomId cab : e.cab) {
            DoorId door = kNone;
            if (cab != kNone)
                for (const DoorId d : plan_.room(cab).doors)
                    if (plan_.door(d).kind == DoorKind::ElevatorDoor) door = d;
            r.doorIds.push_back(door);
        }
        elevators_.push_back(std::move(r));
    }
    for (const ClassGroup& g : pop_.classes)
        for (std::size_t i = 0; i < g.students.size(); ++i) classRank_[static_cast<std::size_t>(g.students[i])] = static_cast<int>(i);
    // Patrol zones: the indoor rooms of each (level, wing) a janitor sweeps.
    std::map<std::pair<int, int>, std::vector<RoomId>> zones;
    for (const Room& r : plan_.rooms)
        if (r.Indoor() && r.type != RoomType::StairWell && r.type != RoomType::Storage && r.type != RoomType::Janitor && r.type != RoomType::Boiler &&
            r.type != RoomType::Electrical && r.type != RoomType::ServerRoom)
            zones[{r.level, r.wing}].push_back(r.id);
    for (auto& [key, rooms] : zones) patrolZones_.push_back(std::move(rooms));
    // Checkpoints: every phase start; lunch has a second one halfway (the second wave eats).
    for (std::size_t i = 0; i < schedule_.phases.size(); ++i) {
        const Phase& p = schedule_.phases[i];
        checkpoints_.push_back({p.start, static_cast<int>(i), 0});
        if (p.kind == PhaseKind::Lunch) checkpoints_.push_back({(p.start + p.end) * 0.5, static_cast<int>(i), 1});
    }
    std::sort(checkpoints_.begin(), checkpoints_.end(), [](const Checkpoint& a, const Checkpoint& b) { return a.time < b.time; });
    StartDay();
}

void SchoolSim::StartDay()
{
    time_ = 0.0;
    nextCheckpoint_ = 0;
    nextDrill_ = 0;
    phase_ = 0;
    sub_ = 0;
    drillToday_ = true;
    CheckPhase();
}

// --- time --------------------------------------------------------------------------------------------------------------

void SchoolSim::Advance(double seconds)
{
    double remaining = seconds;
    while (remaining > 1e-9) {
        const double maxStep = fullList_.empty() ? 1.0 : static_cast<double>(settings_.fixedStep) * 2.0;
        double step = std::min(remaining, maxStep);
        if (time_ + step >= 86400.0) step = 86400.0 - time_;   // never run across midnight in one step
        time_ += step;
        remaining -= step;
        CheckPhase();
        ProcessEventsUntil(time_);
        CheckDrills();
        UpdateDoors(static_cast<float>(step));
        UpdateElevators(static_cast<float>(step));
        StepFullAll(static_cast<float>(step));
        fidelityTimer_ += step;
        if (fidelityTimer_ >= 0.5) {
            fidelityTimer_ = 0.0;
            UpdateFidelity();
        }
        if (time_ >= 86400.0 - 1e-9) {
            ++day_;
            StartDay();
        }
    }
}

void SchoolSim::Settle(double toTime)
{
    // Runs the simulation with no full-fidelity detail until `toTime`, or until the day changes (used for jumps).
    const bool wasFull = settings_.enableFull;
    settings_.enableFull = false;
    for (const int id : std::vector<int>(fullList_)) Demote(id, Fidelity::Abstract);
    const int startDay = day_;
    while (day_ == startDay && time_ < toTime - 1e-9) Advance(std::min(1.0, toTime - time_));
    settings_.enableFull = wasFull;
}

void SchoolSim::FastForwardTo(double timeOfDay)
{
    if (timeOfDay <= time_) return;
    Settle(timeOfDay);
}

void SchoolSim::SkipToNextPhase(bool lessonsOnly)
{
    for (int i = phase_ + 1; i < static_cast<int>(schedule_.phases.size()); ++i) {
        if (lessonsOnly && schedule_.phases[static_cast<std::size_t>(i)].kind != PhaseKind::Lesson) continue;
        FastForwardTo(schedule_.phases[static_cast<std::size_t>(i)].start + 0.5);
        return;
    }
    NextDay();
}

void SchoolSim::NextDay()
{
    const int target = day_ + 1;
    Settle(86400.0);
    // Settle stops when the day changes; make sure of it.
    while (day_ < target) Advance(1.0);
}

void SchoolSim::JumpToTime(double timeOfDay)
{
    if (timeOfDay >= time_) {
        FastForwardTo(timeOfDay);
        return;
    }
    // Backwards: replay today from midnight. Everything is deterministic, so the state is the same as before.
    for (const int id : std::vector<int>(fullList_)) Demote(id, Fidelity::Abstract);
    for (Agent& a : agents_) {
        a = Agent{};
    }
    std::fill(occupancy_.begin(), occupancy_.end(), 0);
    std::fill(fullInRoom_.begin(), fullInRoom_.end(), 0);
    events_ = EventQueue();
    idleCounter_.clear();
    dineCounter_ = 0;
    std::fill(assemblyLoad_.begin(), assemblyLoad_.end(), 0);
    drill_ = DrillStatus{};
    for (std::size_t i = 0; i < elevators_.size(); ++i) {
        ElevatorRuntime& r = elevators_[i];
        r.y = plan_.FloorY(plan_.elevators[i].levelLow);
        r.level = plan_.elevators[i].levelLow;
        r.target = -1;
        r.state = ElevatorRuntime::State::Idle;
        r.door = 0.0f;
        r.passengers = 0;
        std::fill(r.calls.begin(), r.calls.end(), 0);
    }
    StartDay();
    FastForwardTo(timeOfDay);
}

// --- phases and goals ---------------------------------------------------------------------------------------------------------

void SchoolSim::CheckPhase()
{
    while (nextCheckpoint_ < checkpoints_.size() && checkpoints_[nextCheckpoint_].time <= time_ + 1e-9) {
        const Checkpoint cp = checkpoints_[nextCheckpoint_++];
        OnCheckpoint(cp.phase, cp.sub);
    }
}

void SchoolSim::OnCheckpoint(int phase, int sub)
{
    phase_ = phase;
    sub_ = sub;
    const Phase& p = schedule_.At(phase);
    if (sub == 0 && p.bell && p.kind != PhaseKind::Night) {
        bells_.push_back({time_, phase, false});
        ++stats_.bellsRung;
    }
    idleCounter_.clear();
    dineCounter_ = 0;
    if (drill_.active) return;   // goals are suspended while evacuating; EndDrill resumes them
    AssignGoals(phase, sub);
    // Janitors and guards walk a new round every few minutes.
    for (const Npc& n : pop_.npcs)
        if (n.role == Role::Janitor || n.role == Role::Security || n.role == Role::Maintenance) {
            const double first = time_ + 120.0 + 480.0 * Unit(H(settings_.seed, static_cast<std::uint64_t>(n.id), static_cast<std::uint64_t>(day_), static_cast<std::uint64_t>(phase)));
            events_.push({first, n.id, agents_[static_cast<std::size_t>(n.id)].epoch, Event::Tick});
        }
}

const Lesson* SchoolSim::LessonOf(const Npc& npc, int period) const
{
    if (period < 1 || period > pop_.periods) return nullptr;
    const int d = Weekday();
    if (npc.role == Role::Student) return &pop_.classes[static_cast<std::size_t>(npc.classId)].timetable[static_cast<std::size_t>(d)][static_cast<std::size_t>(period - 1)];
    if (npc.role == Role::Teacher) {
        const int ti = pop_.TeacherIndex(npc.id);
        if (ti < 0) return nullptr;
        const int c = pop_.teacherClass[static_cast<std::size_t>(ti)][static_cast<std::size_t>(d)][static_cast<std::size_t>(period - 1)];
        if (c < 0) return nullptr;
        return &pop_.classes[static_cast<std::size_t>(c)].timetable[static_cast<std::size_t>(d)][static_cast<std::size_t>(period - 1)];
    }
    return nullptr;
}

Goal SchoolSim::SpotGoal(RoomId room, int which, SpotKind kind, Activity a, std::uint64_t)
{
    const RoomLayout& l = Layout(room);
    Goal g;
    g.room = room;
    g.activity = a;
    const std::vector<Spot>* list = kind == SpotKind::Teacher && !l.teacher.empty() ? &l.teacher : &l.seats;
    if (list->empty()) list = &l.idle;
    if (list->empty()) {
        g.pos = plan_.room(room).rect.Center();
        return g;
    }
    const Spot& s = (*list)[static_cast<std::size_t>(which) % list->size()];
    g.pos = s.pos;
    g.yaw = s.yaw;
    g.pose = s.pose;
    return g;
}

Goal SchoolSim::SeatGoal(const Npc& npc, const ClassGroup& g, const Lesson& lesson, int role)
{
    Goal goal;
    const RoomId room = lesson.free || lesson.room == kNone ? g.home : lesson.room;
    const RoomLayout& l = Layout(room);
    goal.room = room;
    if (role == 0) {
        const int rank = classRank_[static_cast<std::size_t>(npc.id)] + (lesson.free ? 0 : lesson.slot * 28);
        const std::vector<Spot>& list = l.seats.empty() ? l.idle : l.seats;
        if (list.empty()) {
            goal.pos = plan_.room(room).rect.Center();
        } else {
            const Spot& s = list[static_cast<std::size_t>(rank) % list.size()];
            goal.pos = s.pos;
            goal.yaw = s.yaw;
            goal.pose = s.pose;
            if (static_cast<std::size_t>(rank) >= list.size()) {
                // More people than seats: stand in rings around the seat, never outside the room.
                const int ring = static_cast<int>(static_cast<std::size_t>(rank) / list.size());
                const float ang = static_cast<float>(ring) * 2.399f + static_cast<float>(rank % 7) * 0.9f;
                goal.pos = plan_.room(room).rect.Clamp(goal.pos + Vec2{std::cos(ang), std::sin(ang)} * (0.45f * static_cast<float>(ring)), 0.4f);
            }
        }
        goal.activity = goal.pose == Pose::Exercise ? Activity::Exercising : Activity::InLesson;
    } else {
        const std::vector<Spot>& list = l.teacher.empty() ? l.work : l.teacher;
        if (list.empty()) {
            goal.pos = plan_.room(room).rect.Center();
        } else {
            const Spot& s = list[static_cast<std::size_t>(lesson.free ? 0 : lesson.slot) % list.size()];
            goal.pos = s.pos;
            goal.yaw = s.yaw;
            goal.pose = s.pose == Pose::Work ? Pose::Stand : s.pose;
        }
        goal.activity = Activity::Teaching;
    }
    return goal;
}

Goal SchoolSim::IdleGoal(RoomId room, const Npc& npc, std::uint64_t salt, Activity a)
{
    const RoomLayout& l = Layout(room);
    Goal g;
    g.room = room;
    g.activity = a;
    const int n = static_cast<int>(l.idle.size());
    const int counter = idleCounter_[room]++;
    if (n == 0) {
        const Rect& r = plan_.room(room).rect;
        const Vec2 jitter{Unit(H(salt, static_cast<std::uint64_t>(npc.id), 1)) - 0.5f, Unit(H(salt, static_cast<std::uint64_t>(npc.id), 2)) - 0.5f};
        g.pos = r.Clamp(r.Center() + Vec2{jitter.x * r.Width() * 0.5f, jitter.z * r.Depth() * 0.5f}, 0.6f);
        return g;
    }
    // Start somewhere pseudo-random in the list so crowds do not fill it from one end.
    const int offset = static_cast<int>(H(salt, static_cast<std::uint64_t>(room)) % static_cast<std::uint64_t>(n));
    const Spot& s = l.idle[static_cast<std::size_t>((offset + counter) % n)];
    const int ring = counter / n;
    g.pos = s.pos;
    if (ring > 0) {
        const float ang = Unit(H(salt, static_cast<std::uint64_t>(npc.id), 3)) * 6.2831853f;
        g.pos = plan_.room(room).rect.Clamp(s.pos + Vec2{std::cos(ang), std::sin(ang)} * (0.45f * static_cast<float>(ring)), 0.3f);
    }
    g.yaw = s.yaw;
    g.pose = s.pose;
    return g;
}

Goal SchoolSim::OutdoorBreakGoal(const Npc& npc, std::uint64_t salt)
{
    static const RoomType kOutdoor[] = {RoomType::Courtyard, RoomType::Playground, RoomType::FootballField, RoomType::Plaza, RoomType::Courtyard};
    const std::uint64_t h = H(salt, static_cast<std::uint64_t>(npc.classId >= 0 ? npc.classId : npc.id), 11);
    const RoomType want = kOutdoor[h % 5];
    std::vector<RoomId> candidates = plan_.RoomsOfType(want);
    if (want == RoomType::Plaza) {   // only the open plazas, not the lawns far away
        std::erase_if(candidates, [&](RoomId r) { return plan_.room(r).rect.Area() < 600.0f || plan_.room(r).rect.z0 > 100.0f; });
        if (candidates.empty()) candidates = plan_.RoomsOfType(RoomType::Courtyard);
    }
    const RoomId room = candidates[static_cast<std::size_t>(H(salt, static_cast<std::uint64_t>(npc.id), 12) % candidates.size())];
    return IdleGoal(room, npc, salt);
}

Goal SchoolSim::CafeteriaGoal(const Npc& npc, std::uint64_t salt)
{
    const std::vector<RoomId> cafs = plan_.RoomsOfType(RoomType::Cafeteria);
    Goal g;
    if (cafs.empty()) return IdleGoal(plan_.mainLobby, npc, salt);
    const RoomLayout& l = Layout(cafs[0]);
    g.room = cafs[0];
    g.dine = true;
    g.activity = Activity::Queueing;
    if (!l.queue.empty()) {
        const int idx = idleCounter_[g.room]++;
        const Spot& s = l.queue[static_cast<std::size_t>(idx) % l.queue.size()];
        g.pos = s.pos;
        g.yaw = s.yaw;
        g.pose = Pose::Stand;
        if (static_cast<std::size_t>(idx) >= l.queue.size()) {
            // The line is longer than its marked slots: wait in loose rows behind it, inside the room.
            const int ring = static_cast<int>(static_cast<std::size_t>(idx) / l.queue.size());
            const float ang = static_cast<float>(ring) * 2.399f + static_cast<float>(idx % 5) * 1.2f;
            g.pos = plan_.room(g.room).rect.Clamp(g.pos + Vec2{std::cos(ang), std::sin(ang)} * (0.5f * static_cast<float>(ring)), 0.5f);
        }
    } else {
        g.pos = plan_.room(g.room).rect.Center();
    }
    return g;
}

Goal SchoolSim::WorkGoal(const Npc& npc)
{
    Goal g;
    RoomId room = npc.base;
    if (room == kNone) room = plan_.mainLobby;
    const RoomLayout& l = Layout(room);
    g.room = room;
    g.activity = Activity::Working;
    const std::vector<Spot>& list = l.work.empty() ? l.idle : l.work;
    if (list.empty()) {
        g.pos = plan_.room(room).rect.Center();
        return g;
    }
    const Spot& s = list[static_cast<std::size_t>(npc.baseSpot) % list.size()];
    g.pos = s.pos;
    g.yaw = s.yaw;
    g.pose = s.pose;
    return g;
}

Goal SchoolSim::PatrolGoal(const Npc& npc, std::uint64_t salt)
{
    if (npc.role == Role::Security) {
        // Lobby, a corridor on the ground floor, the courtyard, in turn.
        const std::uint64_t h = H(salt, static_cast<std::uint64_t>(npc.id), 21);
        std::vector<RoomId> stops = {plan_.mainLobby};
        for (const Room& r : plan_.rooms)
            if ((r.type == RoomType::Corridor && r.level == 1 && r.rect.Width() * r.rect.Depth() > 40.0f) || r.type == RoomType::Courtyard) stops.push_back(r.id);
        Goal g = IdleGoal(stops[static_cast<std::size_t>(h % stops.size())], npc, salt, Activity::Patrolling);
        return g;
    }
    const std::vector<RoomId>& zone = patrolZones_[static_cast<std::size_t>(npc.zone) % patrolZones_.size()];
    const std::uint64_t h = H(salt, static_cast<std::uint64_t>(npc.id), 22);
    const RoomId room = zone[static_cast<std::size_t>(h % zone.size())];
    Goal g = IdleGoal(room, npc, salt, Activity::Cleaning);
    g.pose = Pose::Sweep;
    return g;
}

Goal SchoolSim::ClubGoal(const Npc& npc)
{
    // Deterministic for the day (no counters), so the club at Dismissal and at Clubs time is the same place.
    static const RoomType kClubs[] = {RoomType::Library, RoomType::Gym, RoomType::MusicRoom, RoomType::ComputerLab, RoomType::ArtStudio, RoomType::FootballField};
    const std::uint64_t h = H(settings_.seed, static_cast<std::uint64_t>(npc.id), static_cast<std::uint64_t>(day_), 31);
    const RoomType want = kClubs[h % 6];
    const std::vector<RoomId> rooms = plan_.RoomsOfType(want);
    const RoomId room = rooms[static_cast<std::size_t>((h >> 8) % rooms.size())];
    const RoomLayout& l = Layout(room);
    Goal g;
    g.room = room;
    g.activity = want == RoomType::Gym || want == RoomType::FootballField ? Activity::Exercising : want == RoomType::Library ? Activity::Browsing : Activity::InLesson;
    const std::vector<Spot>& list = l.seats.empty() ? l.idle : l.seats;
    if (list.empty()) {
        g.pos = plan_.room(room).rect.Center();
        return g;
    }
    const Spot& s = list[static_cast<std::size_t>((h >> 20) % list.size())];
    g.pos = s.pos;
    g.yaw = s.yaw;
    g.pose = s.pose;
    return g;
}

int SchoolSim::NearestAssembly(RoomId from, int salt)
{
    int best = 0;
    float bestScore = 1e30f;
    for (std::size_t i = 0; i < plan_.assemblyPoints.size(); ++i) {
        const float d = nav_.Distance(from, plan_.assemblyPoints[i]);
        if (d < 0.0f) continue;
        // a crowded point is a little farther: keeps one zone from taking everybody
        const float score = d + static_cast<float>(assemblyLoad_[i]) * 0.04f + static_cast<float>(salt % 7) * 0.01f;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

Goal SchoolSim::AssemblyGoal(const Npc& npc)
{
    const Agent& a = agents_[static_cast<std::size_t>(npc.id)];
    // Everybody in a class goes to the same point: students follow their class, teachers follow the class they teach.
    RoomId from = a.room != kNone ? a.room : plan_.mainLobby;
    const int salt = npc.classId >= 0 ? npc.classId : npc.id;
    const int idx = NearestAssembly(from, salt);
    ++assemblyLoad_[static_cast<std::size_t>(idx)];
    const RoomId room = plan_.assemblyPoints[static_cast<std::size_t>(idx)];
    Goal g = IdleGoal(room, npc, 77, Activity::Assembled);
    g.activity = Activity::Assembled;
    return g;
}

bool SchoolSim::GoalDiffers(const Agent& a, const Goal& g) const
{
    // "Off campus" is a goal like any other: nothing to do for somebody who is already there or already on the way.
    const bool headingOff = a.mode == Mode::Offsite || a.goal.room == plan_.offsite;
    if (g.room == plan_.offsite || g.room == kNone) return !headingOff;
    if (a.goal.room != g.room) return true;
    return DistanceSq(a.goal.pos, g.pos) > 0.0025f || a.goal.dine != g.dine;
}

Goal SchoolSim::GoalFor(const Npc& npc, int phase, int sub, double& departOffset)
{
    const Phase& p = schedule_.At(phase);
    const double dur = p.end - p.start;
    const std::uint64_t salt = H(settings_.seed, static_cast<std::uint64_t>(day_), static_cast<std::uint64_t>(phase) * 4 + static_cast<std::uint64_t>(sub));
    const std::uint64_t pick = H(salt, static_cast<std::uint64_t>(npc.id), 5);
    const float u = Unit(pick);
    departOffset = 1.0 + 7.0 * Unit(H(salt, static_cast<std::uint64_t>(npc.id), 6));   // reaction time to a bell
    Goal off;   // off campus
    off.room = plan_.offsite;
    off.activity = Activity::Offsite;
    const auto leave = [&](double within) {
        departOffset = within * Unit(H(salt, static_cast<std::uint64_t>(npc.id), 7));
        Goal g;
        g.room = plan_.offsite;
        g.activity = Activity::Offsite;
        return g;
    };

    const bool night = p.kind == PhaseKind::Night || p.kind == PhaseKind::Closing;
    switch (npc.role) {
    case Role::Student: {
        const ClassGroup& g = pop_.classes[static_cast<std::size_t>(npc.classId)];
        switch (p.kind) {
        case PhaseKind::Night:
        case PhaseKind::Closing: return leave(300.0);
        case PhaseKind::Arrival: {
            departOffset = dur * 0.62 * static_cast<double>(u);
            const Lesson* first = LessonOf(npc, 1);
            if (u < 0.55f && first != nullptr) return SeatGoal(npc, g, *first, 0);                  // straight to the first lesson's room
            if (u < 0.70f) {                                                                       // a visit to the lockers first
                const std::vector<RoomId> lockers = plan_.RoomsOfType(RoomType::LockerHall);
                if (!lockers.empty()) return IdleGoal(lockers[static_cast<std::size_t>(H(salt, static_cast<std::uint64_t>(npc.classId), 13) % lockers.size())], npc, salt);
            }
            if (settings_.badWeather) return IdleGoal(plan_.mainLobby, npc, salt);
            return OutdoorBreakGoal(npc, salt);
        }
        case PhaseKind::Lesson:
        case PhaseKind::Passing: {
            const Lesson* l = LessonOf(npc, p.lesson);
            if (l == nullptr) return IdleGoal(g.home, npc, salt);
            return SeatGoal(npc, g, *l, 0);
        }
        case PhaseKind::LongBreak: {
            const float roll = Unit(H(salt, static_cast<std::uint64_t>(npc.id), 8));
            if (roll < 0.14f) return CafeteriaGoal(npc, salt);
            if (roll < 0.28f) {
                const std::vector<RoomId> libs = plan_.RoomsOfType(RoomType::Library);
                return IdleGoal(libs[0], npc, salt, Activity::Browsing);
            }
            if (roll < 0.68f && !settings_.badWeather) return OutdoorBreakGoal(npc, salt);
            // corridor or lobby on the floor of the next lesson
            const Lesson* l = LessonOf(npc, p.lesson > 0 ? p.lesson : 1);
            RoomId near = l != nullptr && l->room != kNone ? l->room : g.home;
            // choose a corridor next to the room (or the room's own neighbour)
            RoomId target = plan_.mainLobby;
            for (const DoorId d : plan_.room(near).doors) {
                const Room& other = plan_.room(plan_.door(d).Other(near));
                if (other.IsCorridor()) {
                    target = other.id;
                    break;
                }
            }
            return IdleGoal(target, npc, salt);
        }
        case PhaseKind::Lunch: {
            const bool myWave = (sub == g.lunchWave);
            if (myWave) return CafeteriaGoal(npc, salt);
            const float roll = Unit(H(salt, static_cast<std::uint64_t>(npc.id), 9));
            if (roll < 0.25f) {
                const std::vector<RoomId> libs = plan_.RoomsOfType(RoomType::Library);
                return IdleGoal(libs[0], npc, salt, Activity::Browsing);
            }
            if (roll < 0.75f && !settings_.badWeather) return OutdoorBreakGoal(npc, salt);
            return IdleGoal(plan_.mainLobby, npc, salt);
        }
        case PhaseKind::Dismissal: {
            if (Unit(H(settings_.seed, static_cast<std::uint64_t>(npc.id), static_cast<std::uint64_t>(day_), 32)) < 0.15f) return ClubGoal(npc);
            return leave(dur * 0.8);
        }
        case PhaseKind::Clubs: {
            // Club members (15%) stay; everybody else has gone home.
            if (Unit(H(settings_.seed, static_cast<std::uint64_t>(npc.id), static_cast<std::uint64_t>(day_), 32)) < 0.15f) return ClubGoal(npc);
            return leave(300.0);
        }
        }
        return off;
    }
    case Role::Teacher: {
        const RoomId staff = npc.base;
        const auto staffRoom = [&]() { return WorkGoal(npc); };
        switch (p.kind) {
        case PhaseKind::Night:
        case PhaseKind::Closing: return leave(600.0);
        case PhaseKind::Arrival: {
            departOffset = dur * 0.6 * static_cast<double>(u);
            return staffRoom();
        }
        case PhaseKind::Lesson:
        case PhaseKind::Passing: {
            const Lesson* l = LessonOf(npc, p.lesson);
            if (l != nullptr && !l->free) {
                const ClassGroup& g = pop_.classes[static_cast<std::size_t>(pop_.teacherClass[static_cast<std::size_t>(pop_.TeacherIndex(npc.id))][static_cast<std::size_t>(Weekday())][static_cast<std::size_t>(p.lesson - 1)])];
                return SeatGoal(npc, g, *l, 1);
            }
            (void)staff;
            // free period: staff room, or corridor duty now and then
            if (u < 0.12f) return IdleGoal(plan_.mainLobby, npc, salt);
            return staffRoom();
        }
        case PhaseKind::LongBreak:
        case PhaseKind::Lunch: {
            if (p.kind == PhaseKind::Lunch && u < 0.2f) return CafeteriaGoal(npc, salt);
            if (u < 0.35f) {
                const Lesson* l = LessonOf(npc, p.lesson > 0 ? p.lesson : 1);
                (void)l;
                return IdleGoal(plan_.mainLobby, npc, salt);   // duty near the lobby
            }
            return staffRoom();
        }
        case PhaseKind::Dismissal: return staffRoom();
        case PhaseKind::Clubs: {
            if (u < 0.15f && sub == 0) {
                // supervise a club
                const std::uint64_t h = H(settings_.seed, static_cast<std::uint64_t>(npc.id), static_cast<std::uint64_t>(day_), 33);
                static const RoomType kClubs[] = {RoomType::Library, RoomType::Gym, RoomType::MusicRoom, RoomType::ComputerLab};
                const std::vector<RoomId> rooms = plan_.RoomsOfType(kClubs[h % 4]);
                const Goal g = SpotGoal(rooms[static_cast<std::size_t>((h >> 8) % rooms.size())], 0, SpotKind::Teacher, Activity::Teaching, h);
                return g;
            }
            if (u < 0.55f) return staffRoom();
            return leave(2400.0);
        }
        }
        return off;
    }
    default: break;
    }

    // --- non-teaching staff ---------------------------------------------------------------------------------------------
    const bool dayShift = p.kind != PhaseKind::Night && p.kind != PhaseKind::Closing;
    switch (npc.role) {
    case Role::Receptionist:
    case Role::Admin:
    case Role::Counselor:
    case Role::Nurse:
    case Role::ITStaff:
    case Role::Principal:
    case Role::Librarian: {
        if (!dayShift) return leave(300.0);
        if (p.kind == PhaseKind::Arrival) departOffset = dur * 0.5 * static_cast<double>(u);
        if (p.kind == PhaseKind::Clubs && npc.role != Role::Librarian && npc.role != Role::Receptionist) return leave(1200.0 * static_cast<double>(u) + 60.0);
        if (p.kind == PhaseKind::Lunch && npc.role != Role::Receptionist && u < 0.5f) {
            const std::vector<RoomId> rooms = plan_.RoomsOfType(RoomType::StaffRoom);
            return IdleGoal(rooms[0], npc, salt);
        }
        return WorkGoal(npc);
    }
    case Role::Cook: {
        if (p.kind == PhaseKind::Night || p.kind == PhaseKind::Closing || p.kind == PhaseKind::Dismissal || p.kind == PhaseKind::Clubs) return leave(600.0);
        if (p.kind == PhaseKind::Arrival) departOffset = dur * 0.5 * static_cast<double>(u);
        if (p.kind == PhaseKind::Lunch || p.kind == PhaseKind::LongBreak) {
            // serve at the counter in the cafeteria
            const std::vector<RoomId> cafs = plan_.RoomsOfType(RoomType::Cafeteria);
            if (!cafs.empty() && npc.zone % 2 == 0) {
                const RoomLayout& l = Layout(cafs[0]);
                Goal g;
                g.room = cafs[0];
                g.activity = Activity::Working;
                if (!l.work.empty()) {
                    const Spot& s = l.work[static_cast<std::size_t>(npc.zone / 2) % l.work.size()];
                    g.pos = s.pos;
                    g.yaw = s.yaw;
                    g.pose = s.pose;
                }
                return g;
            }
        }
        return WorkGoal(npc);
    }
    case Role::Security:
    case Role::Janitor:
    case Role::Maintenance: {
        if (npc.role == Role::Janitor && (p.kind == PhaseKind::Night)) return leave(600.0);
        if (npc.role != Role::Janitor && (p.kind == PhaseKind::Night || p.kind == PhaseKind::Closing)) return leave(300.0);
        if (p.kind == PhaseKind::Arrival) departOffset = dur * 0.4 * static_cast<double>(u);
        return PatrolGoal(npc, salt);
    }
    default: break;
    }
    (void)night;
    return off;
}

void SchoolSim::AssignGoals(int phase, int sub, bool afterDrill)
{
    // Two passes: first every goal, then departure times with door-throughput ranks per room.
    struct Leaver {
        int npc;
        double offset;
    };
    std::map<RoomId, std::vector<Leaver>> byRoom;
    const double base = time_;
    for (const Npc& n : pop_.npcs) {
        Agent& a = agents_[static_cast<std::size_t>(n.id)];
        double offset = 0.0;
        const Goal g = GoalFor(n, phase, sub, offset);
        if (!GoalDiffers(a, g) && !afterDrill) continue;
        if (afterDrill && !GoalDiffers(a, g) && a.mode == Mode::AtSpot) continue;
        if (a.mode == Mode::AtSpot) {
            byRoom[a.room].push_back({n.id, offset});
            a.goal = g;           // remember the destination now; the departure is scheduled below
            a.departAt = -2.0;    // marker: ranked below
        } else if (a.mode == Mode::Transit) {
            Retarget(n.id, g, base + offset * 0.3);
        } else {
            ScheduleDeparture(n.id, g, base + offset);
        }
    }
    for (auto& [room, list] : byRoom) {
        std::sort(list.begin(), list.end(), [&](const Leaver& x, const Leaver& y) {
            return H(settings_.seed, static_cast<std::uint64_t>(x.npc), static_cast<std::uint64_t>(phase), static_cast<std::uint64_t>(sub)) <
                   H(settings_.seed, static_cast<std::uint64_t>(y.npc), static_cast<std::uint64_t>(phase), static_cast<std::uint64_t>(sub));
        });
        int lanes = 0;
        for (const DoorId d : plan_.room(room).doors)
            if (plan_.door(d).kind != DoorKind::Opening || plan_.room(room).type == RoomType::Courtyard) ++lanes;
        lanes = std::max(1, lanes);
        if (!plan_.room(room).Indoor()) lanes = 8;   // open ground: nobody queues
        const double interval = 0.6 / static_cast<double>(lanes);
        for (std::size_t k = 0; k < list.size(); ++k) {
            const Goal g = agents_[static_cast<std::size_t>(list[k].npc)].goal;
            ScheduleDeparture(list[k].npc, g, base + list[k].offset + static_cast<double>(k) * interval);
        }
    }
}

// --- observing -----------------------------------------------------------------------------------------------------------------

std::vector<BellEvent> SchoolSim::TakeBells()
{
    std::vector<BellEvent> out;
    out.swap(bells_);
    return out;
}

std::vector<SchoolSim::DoorSound> SchoolSim::TakeDoorSounds()
{
    std::vector<DoorSound> out;
    out.swap(doorSounds_);
    return out;
}

SchoolSim::RoomLesson SchoolSim::LessonIn(RoomId room) const
{
    RoomLesson out;
    const Phase& p = schedule_.At(phase_);
    int period = 0;
    if (p.kind == PhaseKind::Lesson) period = p.lesson;
    else if (p.kind == PhaseKind::Passing) period = p.lesson;
    if (period < 1) return out;
    for (const ClassGroup& g : pop_.classes) {
        const Lesson& l = g.timetable[static_cast<std::size_t>(Weekday())][static_cast<std::size_t>(period - 1)];
        const RoomId r = l.free ? g.home : l.room;
        if (r == room) {
            out.classId = g.id;
            out.subject = l.subject;
            out.teacher = l.teacher;
            out.valid = true;
            return out;
        }
    }
    return out;
}

SchoolSim::RoomState SchoolSim::StateOf(RoomId room) const
{
    const int n = OccupantsOf(room);
    if (n == 0) return RoomState::Empty;
    const Phase& p = schedule_.At(phase_);
    if (p.kind == PhaseKind::Passing) {
        return time_ - p.start < (p.end - p.start) * 0.5 ? RoomState::Ending : RoomState::Arriving;
    }
    if (p.kind == PhaseKind::Lesson) {
        const double into = time_ - p.start;
        if (into < 90.0) return RoomState::Arriving;
        if (p.end - time_ < 120.0) return RoomState::Ending;
        return RoomState::Active;
    }
    return RoomState::Busy;
}

Vec3 SchoolSim::PositionOf(int npc) const
{
    const Agent& a = agents_[static_cast<std::size_t>(npc)];
    if (a.mode == Mode::Transit && a.fidelity != Fidelity::Full && a.journey) return EvaluateJourney(a, time_, nullptr, nullptr, nullptr);
    return a.pos;
}

void SchoolSim::Observe(int npc, Vec3& pos, float& yaw, RoomId& room) const
{
    const Agent& a = agents_[static_cast<std::size_t>(npc)];
    pos = a.pos;
    yaw = a.yaw;
    room = a.room;
    if (a.mode == Mode::Transit && a.fidelity != Fidelity::Full && a.journey) pos = EvaluateJourney(a, time_, &room, &yaw, nullptr);
}

std::uint64_t SchoolSim::Digest() const
{
    std::uint64_t h = H(static_cast<std::uint64_t>(day_), static_cast<std::uint64_t>(time_));
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        const Agent& a = agents_[i];
        const Vec3 p = a.mode == Mode::Transit && a.fidelity != Fidelity::Full && a.journey ? EvaluateJourney(a, time_, nullptr, nullptr, nullptr) : a.pos;
        h = H(h, static_cast<std::uint64_t>(a.mode), static_cast<std::uint64_t>(a.room + 1),
              static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(p.x * 10.0f))) ^ (static_cast<std::uint64_t>(static_cast<std::int64_t>(std::lround(p.z * 10.0f))) << 20));
    }
    return h;
}

int SchoolSim::StuckSuspects() const
{
    int n = 0;
    for (const Agent& a : agents_)
        if (a.mode == Mode::Transit && a.fidelity == Fidelity::Full && a.stuckTimer > 4.0f) ++n;
    return n;
}

std::vector<std::string> SchoolSim::CheckInvariants() const
{
    std::vector<std::string> errors;
    const int rooms = static_cast<int>(plan_.rooms.size());
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        const Agent& a = agents_[i];
        const std::string who = "npc " + std::to_string(i) + " (" + pop_.npcs[i].name + ")";
        switch (a.mode) {
        case Mode::Offsite:
            if (a.room != kNone) errors.push_back(who + " is off campus but has a room");
            if (a.journey) errors.push_back(who + " is off campus but has a journey");
            break;
        case Mode::AtSpot:
            if (a.room < 0 || a.room >= rooms) errors.push_back(who + " stands in an invalid room");
            else if (!plan_.room(a.room).rect.Inflated(1.6f).Contains(a.pos.XZ())) errors.push_back(who + " stands outside its room " + plan_.room(a.room).name);
            if (a.journey) errors.push_back(who + " stands at a spot but still has a journey");
            break;
        case Mode::Transit:
            if (!a.journey) errors.push_back(who + " is walking without a journey");
            if (a.goal.room < 0 || a.goal.room >= rooms) errors.push_back(who + " is walking to an invalid room");
            break;
        }
        if (a.mode != Mode::Offsite && a.pos.y < -5.0f) errors.push_back(who + " is below the building");
        if (!std::isfinite(a.pos.x) || !std::isfinite(a.pos.z) || !std::isfinite(a.pos.y)) errors.push_back(who + " has a non-finite position");
    }
    return errors;
}

}  // namespace School
