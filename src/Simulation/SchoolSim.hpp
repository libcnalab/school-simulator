#pragma once

// The living school: thousands of logical people following a bell schedule through a hierarchical
// navigation graph, at three levels of detail.
//
//   Abstract  nobody looks: only the event queue runs ("leaves at 08:45:07, arrives at 08:46:31").
//             A walker is a Journey (waypoints + start time + speed); its position at any moment is
//             pure arithmetic, so it can be materialised exactly when somebody looks.
//   Reduced   on the same floor as a focus point: position and room refreshed a few times a second
//             (for crowd counts, audio and the minimap), still no collision.
//   Full      close to a focus point: real movement along the waypoints with separation from
//             neighbours, door gates with throughput, stuck detection.
//
// Everyone stationary (in a lesson, on a break, at a desk) simply stands at a Spot of a room.
// The class is renderer-independent: no CNA, no wall clock, every random choice from the seed.

#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "Navigation/NavGraph.hpp"
#include "Rooms/RoomLayout.hpp"
#include "Simulation/Population.hpp"
#include "Simulation/Schedule.hpp"
#include "World/SchoolPlan.hpp"

namespace School {

enum class Mode : std::uint8_t { Offsite, AtSpot, Transit };
enum class Fidelity : std::uint8_t { Abstract, Reduced, Full };
enum class Activity : std::uint8_t {
    Offsite, Walking, InLesson, Teaching, Idle, Queueing, Eating, Working, Exercising, Evacuating, Assembled, Cleaning, Patrolling, Browsing
};
const char* ActivityName(Activity a);

/** Where somebody is going and what they will do there. */
struct Goal {
    RoomId room = kNone;       // kNone: off campus
    Vec2 pos;
    float yaw = 0.0f;
    Pose pose = Pose::Stand;
    Activity activity = Activity::Idle;
    bool dine = false;         // queue first, then eat (cafeteria)
};

/** A walk: waypoints, cumulative length, when it started and how fast it goes. */
struct Journey {
    std::vector<Waypoint> pts;
    std::vector<float> cum;    // cum[i] = path length up to pts[i]
    double start = 0.0;        // time at pts[0] (shifted when the walker waits)
    float speed = 1.3f;
    [[nodiscard]] float Total() const { return cum.empty() ? 0.0f : cum.back(); }
};

struct Agent {
    Mode mode = Mode::Offsite;
    Fidelity fidelity = Fidelity::Abstract;
    Activity activity = Activity::Offsite;
    RoomId room = kNone;
    Vec3 pos;
    float yaw = 0.0f;
    Pose pose = Pose::Stand;
    Goal goal;
    double departAt = -1.0;     // a pending departure (-1: none)
    std::uint32_t epoch = 0;    // invalidates queued events of an older plan
    std::unique_ptr<Journey> journey;
    // current spot (to leave it along the aisles)
    Spot spot;
    bool hasSpot = false;
    // full-fidelity state
    int leg = 0;
    Vec2 vel;
    float waitTimer = 0.0f;
    float stuckTimer = 0.0f;
    Vec2 stuckRef;
    bool animPhaseSeed = false;
    float walkPhase = 0.0f;     // for the walk cycle
    DoorId doorHeld = kNone;
};

struct DoorRuntime {
    float open = 0.0f;          // 0 closed .. 1 fully open
    double wantOpenUntil = -1.0;
    double nextFree[2] = {0.0, 0.0};   // next time an agent may enter, per direction (a->b, b->a)
    float busy = 0.0f;
    int passes = 0;
};

struct Focus {
    Vec2 pos;
    int level = 1;
    float radius = 40.0f;
};

struct SimSettings {
    std::uint64_t seed = 1;
    Preset preset = Preset::Normal;
    int students = -1;              // overrides the preset when >= 0
    int maxFull = 700;              // cap on full-fidelity walkers
    float fullRadius = 38.0f;       // metres around a focus point
    float reducedRadius = 90.0f;
    bool badWeather = false;        // decides where break time is spent
    bool enableFull = true;         // false: everything runs abstract (tests, soak)
    float fixedStep = 0.05f;        // full-fidelity step in seconds of simulated time
};

struct SimStats {
    int total = 0, offsite = 0, atSpot = 0, transit = 0, abstract = 0, reduced = 0, full = 0;
    int pathBuilds = 0;
    long long events = 0;
    long long unstuck = 0;
    long long doorPasses = 0;
    int bellsRung = 0;
    int unreachable = 0;            // goals that no route led to (should stay 0)
    double lateArrivals = 0;        // students arriving after their lesson began (count)
    int maxQueue = 0;               // longest line at a door gate, ever
    int occupiedRooms = 0;
};

struct DrillStatus {
    bool active = false;
    double startedAt = 0.0;
    int total = 0;                  // people who had to evacuate
    int assembled = 0;
    double allAssembledAt = -1.0;
    double lastDuration = -1.0;     // of the previous drill
};

struct BellEvent {
    double time = 0.0;
    int phase = 0;
    bool alarm = false;
};

class SchoolSim {
public:
    SchoolSim(const SchoolPlan& plan, const NavGraph& nav, const LayoutCache& layouts, BellSchedule schedule, const SimSettings& settings);

    // --- what exists -------------------------------------------------------------------------------------
    [[nodiscard]] const Population& Pop() const { return pop_; }
    [[nodiscard]] const BellSchedule& Schedule() const { return schedule_; }
    [[nodiscard]] const SimSettings& Settings() const { return settings_; }
    [[nodiscard]] const Agent& AgentOf(int npc) const { return agents_[static_cast<std::size_t>(npc)]; }
    [[nodiscard]] const std::vector<DoorRuntime>& Doors() const { return doors_; }

    // --- time --------------------------------------------------------------------------------------------
    [[nodiscard]] double Time() const { return time_; }          // seconds since midnight of the current day
    [[nodiscard]] int Day() const { return day_; }               // days since the start (0 = first Monday)
    [[nodiscard]] int Weekday() const { return day_ % kSchoolDays; }
    [[nodiscard]] int PhaseIndex() const { return phase_; }
    /** Advances by `seconds` of simulated time. Large steps are cut up internally. */
    void Advance(double seconds);
    /** Runs forward without full-fidelity detail until the given time of the current day (never backwards). */
    void FastForwardTo(double timeOfDay);
    /** Jumps to the start of the next phase of a kind (or the next phase of any kind) and settles everybody. */
    void SkipToNextPhase(bool lessonsOnly = false);
    /** Starts the next day (after midnight); everything settles first. */
    void NextDay();
    /** Sets the clock inside the current day by replaying from the start of the day (deterministic). */
    void JumpToTime(double timeOfDay);

    // --- observing --------------------------------------------------------------------------------------------
    void SetFocus(const std::vector<Focus>& focus) { focus_ = focus; }
    /** Position of a walker or sitter right now; valid for every mode (Offsite returns the street edge). */
    [[nodiscard]] Vec3 PositionOf(int npc) const;
    /** Position, facing and room of an NPC right now, whatever its fidelity (walkers are evaluated analytically). */
    void Observe(int npc, Vec3& pos, float& yaw, RoomId& room) const;
    [[nodiscard]] int OccupantsOf(RoomId room) const { return occupancy_[static_cast<std::size_t>(room)] + fullInRoom_[static_cast<std::size_t>(room)]; }
    [[nodiscard]] const std::vector<int>& FullAgents() const { return fullList_; }
    [[nodiscard]] const SimStats& Stats() const { return stats_; }
    /** Bells and alarms since the last call. */
    std::vector<BellEvent> TakeBells();
    /** Pending door passes since the last call: (door, direction) for audio. */
    struct DoorSound {
        DoorId door;
        bool opening;
    };
    std::vector<DoorSound> TakeDoorSounds();
    /** Describes what a room is doing: empty, arriving, lesson starting/active/ending. */
    enum class RoomState { Empty, Arriving, Active, Ending, Busy };
    [[nodiscard]] RoomState StateOf(RoomId room) const;
    /** The class and subject currently in a room (or -1). */
    struct RoomLesson {
        int classId = -1;
        Subject subject = Subject::Library;
        int teacher = -1;
        bool valid = false;
    };
    [[nodiscard]] RoomLesson LessonIn(RoomId room) const;

    // --- doors ---------------------------------------------------------------------------------------------------
    /** Somebody (the player) wants this door open for `seconds`. */
    void RequestDoor(DoorId door, float seconds = 3.0f);

    // --- fire drill ------------------------------------------------------------------------------------------------
    void StartDrill();
    void EndDrill();
    [[nodiscard]] const DrillStatus& Drill() const { return drill_; }

    // --- checks ----------------------------------------------------------------------------------------------------------
    /** Consistency problems of the current state (invalid rooms, impossible mode combinations...). */
    [[nodiscard]] std::vector<std::string> CheckInvariants() const;
    /** A hash of every NPC's state: equal digests mean identical simulations. */
    [[nodiscard]] std::uint64_t Digest() const;
    /** People not off campus who have not moved for a long time while not in a lesson-like rest: stuck suspects. */
    [[nodiscard]] int StuckSuspects() const;

private:
    struct Event {
        double time;
        int npc;
        std::uint32_t epoch;
        enum Type : std::uint8_t { Depart, Arrive, Tick } type;
        bool operator>(const Event& o) const { return time > o.time || (time == o.time && npc > o.npc); }
    };
    using EventQueue = std::priority_queue<Event, std::vector<Event>, std::greater<>>;

    // construction / goals (SchoolSim.cpp)
    void Init();
    void StartDay();
    void OnCheckpoint(int phase, int sub);
    void AssignGoals(int phase, int sub, bool afterDrill = false);
    Goal GoalFor(const Npc& npc, int phase, int sub, double& departOffset);
    Goal SeatGoal(const Npc& npc, const ClassGroup& g, const Lesson& lesson, int role);
    Goal SpotGoal(RoomId room, int which, SpotKind kind, Activity a, std::uint64_t salt);
    Goal IdleGoal(RoomId room, const Npc& npc, std::uint64_t salt, Activity a = Activity::Idle);
    Goal OutdoorBreakGoal(const Npc& npc, std::uint64_t salt);
    Goal CafeteriaGoal(const Npc& npc, std::uint64_t salt);
    Goal WorkGoal(const Npc& npc);
    Goal PatrolGoal(const Npc& npc, std::uint64_t salt);
    Goal ClubGoal(const Npc& npc);
    Goal AssemblyGoal(const Npc& npc);
    const Lesson* LessonOf(const Npc& npc, int period) const;
    const RoomLayout& Layout(RoomId r) const { return layouts_.Get(r); }
    bool GoalDiffers(const Agent& a, const Goal& g) const;
    int NearestAssembly(RoomId from, int salt);

    // trips (AgentMotion.cpp)
    void ScheduleDeparture(int npc, const Goal& goal, double departAt);
    void Depart(int npc);
    bool BuildJourney(int npc, RoomId startRoom, Vec3 startPos, const Goal& goal, double startTime);
    void Arrive(int npc);
    void Retarget(int npc, const Goal& goal, double departAt);
    Vec3 EvaluateJourney(const Agent& a, double t, RoomId* room, float* yaw, float* distance) const;
    void StepFull(int npc, float dt);
    void StepFullAll(float dt);
    void UpdateFidelity();
    void Promote(int npc, Fidelity to);
    void Demote(int npc, Fidelity to);
    void SetSpot(int npc, const Goal& g);
    void UpdateDoors(float dt);
    void LeaveSpot(int npc);
    float Occupancy(RoomId r) const { return static_cast<float>(occupancy_[static_cast<std::size_t>(r)]); }

    void ProcessEventsUntil(double t);
    void CheckPhase();
    void CheckDrills();
    void Settle(double toTime);

    const SchoolPlan& plan_;
    const NavGraph& nav_;
    const LayoutCache& layouts_;
    BellSchedule schedule_;
    SimSettings settings_;
    PathBuilder paths_;
    Population pop_;
    std::vector<Agent> agents_;
    std::vector<DoorRuntime> doors_;
    std::vector<int> occupancy_;      // people standing at spots per room
    std::vector<int> fullInRoom_;
    std::vector<int> fullList_;
    std::vector<int> reducedList_;
    std::vector<Focus> focus_;
    std::vector<BellEvent> bells_;
    std::vector<DoorSound> doorSounds_;
    EventQueue events_;
    double time_ = 0.0;
    double stepRemainder_ = 0.0;
    double fidelityTimer_ = 0.0;
    double reducedTimer_ = 0.0;
    int day_ = 0;
    int phase_ = 0;
    int sub_ = 0;
    int nextDrill_ = 0;
    bool drillToday_ = true;
    DrillStatus drill_;
    SimStats stats_;
    // per-checkpoint allocators (reset at each checkpoint)
    std::unordered_map<int, int> idleCounter_;
    int dineCounter_ = 0;
    std::unordered_map<int, int> doorQueue_;
    std::vector<int> assemblyLoad_;
    std::vector<std::vector<RoomId>> patrolZones_;
    std::vector<int> classRank_;      // student's index inside its class
    std::vector<int> grid_;           // scratch
    std::vector<std::vector<int>> hashCells_;
    struct Checkpoint {
        double time;
        int phase;
        int sub;
    };
    std::vector<Checkpoint> checkpoints_;
    std::size_t nextCheckpoint_ = 0;
    bool inFastForward_ = false;
    const std::vector<std::pair<std::uint64_t, int>>* stepKeys_ = nullptr;   // spatial hash of the current full-fidelity step
};

}  // namespace School
