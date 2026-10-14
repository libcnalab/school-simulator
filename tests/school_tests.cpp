// Unit and simulation tests for the pure-logic half of school-simulator (no CNA, no window).
// A deliberately tiny harness: CHECK records a failure and carries on, so one run reports everything.
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "Navigation/NavGraph.hpp"
#include "Rooms/RoomLayout.hpp"
#include "Simulation/Population.hpp"
#include "Simulation/SchoolSim.hpp"
#include "World/SchoolPlan.hpp"
#include "World/StairGeometry.hpp"

using namespace School;

namespace {
int g_checks = 0;
int g_failures = 0;
std::string g_test;

#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failures;                                                                             \
            std::printf("FAIL [%s] %s:%d: %s\n", g_test.c_str(), __FILE__, __LINE__, #cond);          \
        }                                                                                             \
    } while (0)
#define CHECK_MSG(cond, ...)                                                                          \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failures;                                                                             \
            std::printf("FAIL [%s] %s:%d: %s -- ", g_test.c_str(), __FILE__, __LINE__, #cond);        \
            std::printf(__VA_ARGS__);                                                                 \
            std::printf("\n");                                                                        \
        }                                                                                             \
    } while (0)

bool AnyContains(const std::vector<std::string>& v, const char* needle)
{
    for (const std::string& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

// Shared fixtures: the plan and graph are expensive enough to build once.
const SchoolPlan& Plan()
{
    static const SchoolPlan plan = BuildSchoolPlan();
    return plan;
}
const NavGraph& Nav()
{
    static const NavGraph nav(Plan());
    return nav;
}
const NavGraph& LiftNav()
{
    static const NavGraph nav(Plan(), true);
    return nav;
}
const LayoutCache& Layouts()
{
    static const LayoutCache layouts(Plan());
    return layouts;
}

// --- plan ------------------------------------------------------------------------------------------------------------

void TestPlanIsValid()
{
    const std::vector<std::string> errors = ValidatePlan(Plan());
    for (const std::string& e : errors) std::printf("  plan problem: %s\n", e.c_str());
    CHECK(errors.empty());
}

void TestPlanScale()
{
    const PlanStats s = ComputeStats(Plan());
    CHECK(s.rooms >= 700);
    CHECK(s.indoorRooms >= 650);
    CHECK(s.doors >= 800);
    CHECK(s.stairs >= 8);
    CHECK(Plan().LevelCount() >= 6);
    CHECK(s.perType[static_cast<int>(RoomType::Classroom)] >= 200);
    CHECK(s.perType[static_cast<int>(RoomType::Gym)] == 1);
    CHECK(s.perType[static_cast<int>(RoomType::Cafeteria)] == 1);
    CHECK(s.perType[static_cast<int>(RoomType::Library)] == 1);
    CHECK(s.perType[static_cast<int>(RoomType::Auditorium)] == 1);
    CHECK(s.perType[static_cast<int>(RoomType::AssemblyPoint)] >= 6);
    CHECK(s.indoorAreaM2 > 50000.0f);
}

void TestPlanRejectsBrokenData()
{
    {   // a door that refers to a room that does not exist
        SchoolPlan p = Plan();
        p.doors[5].b = static_cast<RoomId>(p.rooms.size()) + 40;
        CHECK(AnyContains(ValidatePlan(p), "does not exist"));
    }
    {   // a door pushed off its wall
        SchoolPlan p = Plan();
        p.doors[7].pos.z += 3.0f;
        p.doors[7].pos.x += 3.0f;
        CHECK(AnyContains(ValidatePlan(p), "off the wall"));
    }
    {   // duplicate room codes
        SchoolPlan p = Plan();
        p.rooms[11].code = p.rooms[10].code;
        CHECK(AnyContains(ValidatePlan(p), "duplicate room code"));
    }
    {   // overlapping rooms
        SchoolPlan p = Plan();
        p.rooms[20].rect = p.rooms[21].rect;
        CHECK(AnyContains(ValidatePlan(p), "overlap"));
    }
    {   // a stair shaft with a missing landing (an invalid floor connection)
        SchoolPlan p = Plan();
        p.stairs[0].landing[1] = kNone;
        CHECK(AnyContains(ValidatePlan(p), "no landing"));
    }
    {   // a room nobody can enter
        SchoolPlan p = Plan();
        p.rooms.back().doors.clear();
        const RoomId victim = p.rooms.back().id;
        for (Door& d : p.doors)
            if (d.a == victim || d.b == victim) d.width = 0.0f;
        CHECK(!ValidatePlan(p).empty());
    }
    {   // an exit door that leads nowhere outdoors
        SchoolPlan p = Plan();
        for (Door& d : p.doors)
            if (!d.exit && p.room(d.a).Indoor() && p.room(d.b).Indoor()) {
                d.exit = true;
                break;
            }
        CHECK(AnyContains(ValidatePlan(p), "exit but does not lead outdoors"));
    }
}

void TestRoomLookup()
{
    const SchoolPlan& plan = Plan();
    for (const Room& r : plan.rooms) {
        if (r.type == RoomType::Street) continue;
        const RoomId found = plan.RoomAt(r.rect.Center(), r.level);
        CHECK_MSG(found == r.id, "centre of %s resolved to %s", r.name.c_str(), found == kNone ? "nothing" : plan.room(found).name.c_str());
    }
    CHECK(plan.RoomAt({-5000, -5000}, 1) == kNone);
    // tall rooms answer for every level they span
    const std::vector<RoomId> gyms = plan.RoomsOfType(RoomType::Gym);
    CHECK(!gyms.empty());
    CHECK(plan.RoomAt(plan.room(gyms[0]).rect.Center(), 1) == gyms[0]);
    CHECK(plan.RoomAt(plan.room(gyms[0]).rect.Center(), 2) == gyms[0]);
}

void TestStairHeights()
{
    const SchoolPlan& plan = Plan();
    for (const StairShaft& s : plan.stairs) {
        const StairFrame f{s.footprint, s.entrySide};
        // Walking the stair path, the ground height under every point is within a step of the path's height.
        for (int level = s.levelLow; level < s.levelHigh; ++level) {
            const std::vector<Vec3> path = StairClimbPath(plan, s, level, true);
            CHECK(path.size() >= 5);
            CHECK(std::fabs(path.front().y - plan.FloorY(level)) < 0.01f);
            CHECK(std::fabs(path.back().y - plan.FloorY(level + 1)) < 0.01f);
            for (std::size_t i = 0; i + 1 < path.size(); ++i)
                for (int k = 0; k <= 10; ++k) {
                    const float t = static_cast<float>(k) / 10.0f;
                    const Vec3 p = path[i] + (path[i + 1] - path[i]) * t;
                    const float ground = plan.GroundHeight(p.XZ(), p.y);
                    CHECK_MSG(std::fabs(ground - p.y) < 0.5f, "stair %d level %d: path y %.2f ground %.2f", s.id, level, static_cast<double>(p.y), static_cast<double>(ground));
                }
        }
        (void)f;
    }
    // flat floors
    const RoomId lobby = plan.mainLobby;
    CHECK(std::fabs(plan.GroundHeight(plan.room(lobby).rect.Center(), 0.0f)) < 0.01f);
}

// --- navigation -------------------------------------------------------------------------------------------------------------

void TestEverythingReachable()
{
    const SchoolPlan& plan = Plan();
    const NavGraph& nav = Nav();
    const std::vector<RoomId> reach = nav.ReachableFrom(plan.offsite);
    CHECK(reach.size() == plan.rooms.size());
    std::set<RoomId> seen(reach.begin(), reach.end());
    for (const Room& r : plan.rooms) CHECK_MSG(seen.count(r.id) == 1, "%s is unreachable from the street", r.name.c_str());
}

void TestEveryRoomHasAnEvacuationRoute()
{
    const SchoolPlan& plan = Plan();
    const NavGraph& nav = Nav();
    for (const Room& r : plan.rooms) {
        if (!r.Indoor()) continue;
        bool ok = false;
        for (const RoomId a : plan.assemblyPoints) ok |= nav.Reachable(r.id, a);
        CHECK_MSG(ok, "%s has no route to any assembly point", r.name.c_str());
    }
}

void TestRoutesAreConsistent()
{
    const SchoolPlan& plan = Plan();
    const NavGraph& nav = Nav();
    std::vector<int> route;
    int checked = 0;
    for (std::size_t i = 0; i < plan.rooms.size(); i += 37) {
        for (std::size_t j = 0; j < plan.rooms.size(); j += 53) {
            const RoomId a = static_cast<RoomId>(i), b = static_cast<RoomId>(j);
            CHECK(nav.Route(a, b, route));
            RoomId at = a;
            float cost = 0.0f;
            for (const int e : route) {
                CHECK(nav.Edge(e).from == at);
                at = nav.Edge(e).to;
                cost += nav.Edge(e).cost;
            }
            CHECK(at == b);
            CHECK(std::fabs(cost - nav.Distance(a, b)) < 0.05f * std::max(1.0f, cost));
            CHECK(std::fabs(nav.Distance(a, b) - nav.Distance(b, a)) < 0.05f * std::max(1.0f, cost));
            ++checked;
        }
    }
    CHECK(checked > 100);
    CHECK(nav.Distance(0, 0) == 0.0f);
}

void TestUnreachableDestination()
{
    SchoolPlan p = Plan();
    Room island;
    island.id = static_cast<RoomId>(p.rooms.size());
    island.name = "Island";
    island.code = "X.99";
    island.type = RoomType::Storage;
    island.rect = Rect(500, 500, 504, 504);
    island.level = 1;
    p.rooms.push_back(island);
    p.BuildIndex();
    const NavGraph nav(p);
    CHECK(nav.Distance(0, island.id) < 0.0f);
    CHECK(nav.Distance(island.id, 0) < 0.0f);
    std::vector<int> route;
    CHECK(!nav.Route(0, island.id, route));
    CHECK(nav.NextEdge(0, island.id) == kNone);
    CHECK(!ValidatePlan(p).empty());   // the island has no door
    CHECK(nav.ReachableFrom(p.offsite).size() + 1 == p.rooms.size());
}

void TestMultiFloorRoute()
{
    const SchoolPlan& plan = Plan();
    const NavGraph& nav = Nav();
    // From the basement to the highest classroom: must climb.
    RoomId basement = kNone, top = kNone;
    for (const Room& r : plan.rooms) {
        if (r.level == 0 && r.type == RoomType::Storage && basement == kNone) basement = r.id;
        if (r.level == 5 && r.type == RoomType::Classroom) top = r.id;
    }
    CHECK(basement != kNone && top != kNone);
    std::vector<int> route;
    CHECK(nav.Route(basement, top, route));
    int stairEdges = 0;
    for (const int e : route) stairEdges += nav.Edge(e).stair != kNone ? 1 : 0;
    CHECK(stairEdges >= 5);
    // Waypoints of the walk: contiguous heights, every point inside its room.
    PathBuilder pb(nav);
    std::vector<Waypoint> pts;
    CHECK(pb.Build(basement, plan.room(basement).rect.Center(), top, plan.room(top).rect.Center(), pts));
    CHECK(pts.size() > 20);
    for (std::size_t i = 1; i < pts.size(); ++i) CHECK(std::fabs(pts[i].pos.y - pts[i - 1].pos.y) <= 2.1f);
    CHECK(std::fabs(pts.back().pos.y - plan.FloorY(5)) < 0.01f);
}

void TestWaypointsStayInsideRooms()
{
    const SchoolPlan& plan = Plan();
    PathBuilder pb(Nav());
    int bad = 0;
    for (std::size_t i = 3; i < plan.rooms.size(); i += 41) {
        for (std::size_t j = 5; j < plan.rooms.size(); j += 67) {
            std::vector<Waypoint> pts;
            const Room& a = plan.rooms[i];
            const Room& b = plan.rooms[j];
            if (!pb.Build(a.id, a.rect.Center(), b.id, b.rect.Center(), pts)) continue;
            for (const Waypoint& w : pts) {
                const Room& r = plan.room(w.room);
                if (!r.rect.Inflated(0.06f).Contains(w.pos.XZ())) {
                    if (bad++ < 5) std::printf("  waypoint (%.2f, %.2f) outside %s (route %s -> %s)\n", static_cast<double>(w.pos.x), static_cast<double>(w.pos.z), r.name.c_str(), a.name.c_str(), b.name.c_str());
                }
            }
        }
    }
    CHECK(bad == 0);
}

// --- schedule ----------------------------------------------------------------------------------------------------------------

void TestDefaultScheduleValid()
{
    const BellSchedule s = BellSchedule::Default();
    const std::vector<std::string> errors = s.Validate();
    for (const std::string& e : errors) std::printf("  schedule problem: %s\n", e.c_str());
    CHECK(errors.empty());
    CHECK(s.LessonCount() == 7);
    CHECK(s.At(s.PhaseAt(Hours(8, 10))).kind == PhaseKind::Lesson);
    CHECK(s.At(s.PhaseAt(Hours(8, 47))).kind == PhaseKind::Passing);
    CHECK(s.At(s.PhaseAt(Hours(12, 40))).kind == PhaseKind::Lunch);
    CHECK(s.At(s.PhaseAt(Hours(3))).kind == PhaseKind::Night);
    CHECK(s.At(s.PhaseAt(Hours(23, 59))).kind == PhaseKind::Night);
    CHECK(!s.drills.empty());
}

void TestScheduleParsing()
{
    std::vector<std::string> errors;
    const BellSchedule s = BellSchedule::Parse("00:00 night - Closed\n07:00 arrival - In\n08:00 lesson 1 One\n09:00 passing 2 Move\n09:10 lesson 2 Two\n10:00 lunch - Lunch\n10:30 dismissal - Home\n", errors);
    CHECK(errors.empty());
    CHECK(s.Validate().empty());
    CHECK(s.LessonCount() == 2);
    // bad lines are reported and skipped
    errors.clear();
    BellSchedule::Parse("25:99 lesson 1 Bad\nnonsense\n08:00 dance 1 Bad\n", errors);
    CHECK(errors.size() >= 2);
}

void TestInvalidSchedulesAreRejected()
{
    std::vector<std::string> e;
    CHECK(BellSchedule::Parse("", e).Validate().size() >= 1);
    // lessons out of order
    BellSchedule a = BellSchedule::Parse("00:00 night - N\n08:00 lesson 2 Two\n09:00 lesson 1 One\n10:00 lunch - L\n11:00 dismissal - D\n", e);
    CHECK(AnyContains(a.Validate(), "lesson numbers"));
    // two phases at the same time
    BellSchedule b = BellSchedule::Parse("00:00 night - N\n08:00 lesson 1 A\n08:00 lunch - B\n09:00 dismissal - D\n", e);
    CHECK(AnyContains(b.Validate(), "no duration"));
    // no dismissal, no lunch
    BellSchedule c = BellSchedule::Parse("00:00 night - N\n08:00 lesson 1 A\n09:00 night - N\n", e);
    CHECK(AnyContains(c.Validate(), "no dismissal"));
    CHECK(AnyContains(c.Validate(), "no lunch"));
    // does not start at midnight
    BellSchedule d = BellSchedule::Parse("08:00 lesson 1 A\n09:00 lunch - L\n10:00 dismissal - D\n", e);
    CHECK(AnyContains(d.Validate(), "00:00"));
    // a lesson shorter than ten minutes
    BellSchedule f = BellSchedule::Parse("00:00 night - N\n08:00 lesson 1 A\n08:05 lunch - L\n09:00 dismissal - D\n", e);
    CHECK(AnyContains(f.Validate(), "shorter than ten minutes"));
    // a passing period naming the wrong next lesson
    BellSchedule g = BellSchedule::Parse("00:00 night - N\n08:00 lesson 1 A\n09:00 passing 5 P\n09:10 lesson 2 B\n10:00 lunch - L\n11:00 dismissal - D\n", e);
    CHECK(AnyContains(g.Validate(), "does not follow"));
}

// --- population and timetable --------------------------------------------------------------------------------------------------

void TestTimetables()
{
    for (const int students : {0, 50, 100, 500, 1000, 2400}) {
        PopulationSpec spec;
        spec.students = students;
        const Population pop = BuildPopulation(Plan(), spec);
        const std::vector<std::string> errors = ValidateTimetable(Plan(), pop);
        for (std::size_t i = 0; i < errors.size() && i < 5; ++i) std::printf("  timetable problem (%d students): %s\n", students, errors[i].c_str());
        CHECK_MSG(errors.empty(), "%d students: %zu problems", students, errors.size());
        CHECK(pop.StudentCount() == students);
        if (students > 0) {
            CHECK(!pop.teachers.empty());
            // nearly all lessons are real lessons
            int free = 0, total = 0;
            for (const ClassGroup& g : pop.classes)
                for (int d = 0; d < kSchoolDays; ++d)
                    for (int p = 0; p < pop.periods; ++p) {
                        ++total;
                        free += g.timetable[static_cast<std::size_t>(d)][static_cast<std::size_t>(p)].free ? 1 : 0;
                    }
            CHECK_MSG(free * 20 < total, "%d students: %d of %d lessons are free", students, free, total);
        }
    }
}

void TestTimetableDetectsConflicts()
{
    PopulationSpec spec;
    spec.students = 200;
    Population pop = BuildPopulation(Plan(), spec);
    CHECK(ValidateTimetable(Plan(), pop).empty());
    {   // two classes with the same teacher in the same period
        Population p = pop;
        bool done = false;
        for (std::size_t c = 0; c < p.classes.size() && !done; ++c)
            for (std::size_t d = 0; d < kSchoolDays && !done; ++d)
                for (std::size_t q = 0; q < static_cast<std::size_t>(p.periods) && !done; ++q) {
                    Lesson& l = p.classes[c].timetable[d][q];
                    if (l.free || l.teacher < 0) continue;
                    for (std::size_t c2 = c + 1; c2 < p.classes.size() && !done; ++c2) {
                        Lesson& l2 = p.classes[c2].timetable[d][q];
                        if (!l2.free) {
                            l2.teacher = l.teacher;
                            l2.subject = l.subject;
                            done = true;
                        }
                    }
                }
        CHECK(AnyContains(ValidateTimetable(Plan(), p), "two places") || AnyContains(ValidateTimetable(Plan(), p), "does not teach"));
    }
    {   // a lesson in a room that does not exist
        Population p = pop;
        for (Lesson& l : p.classes[0].timetable[0])
            if (!l.free) {
                l.room = 99999;
                break;
            }
        CHECK(AnyContains(ValidateTimetable(Plan(), p), "does not exist"));
    }
    {   // a class with no students
        Population p = pop;
        p.classes[0].students.clear();
        CHECK(AnyContains(ValidateTimetable(Plan(), p), "no students"));
    }
    {   // a physics lesson in a chemistry lab
        Population p = pop;
        bool done = false;
        const std::vector<RoomId> chem = Plan().RoomsOfType(RoomType::ChemistryLab);
        for (ClassGroup& g : p.classes)
            for (auto& day : g.timetable)
                for (Lesson& l : day)
                    if (!done && !l.free && l.subject == Subject::Physics) {
                        l.room = chem[0];
                        done = true;
                    }
        CHECK(done);
        CHECK(AnyContains(ValidateTimetable(Plan(), p), "scheduled in a"));
    }
}

// --- layouts ---------------------------------------------------------------------------------------------------------------------

void TestLayouts()
{
    const SchoolPlan& plan = Plan();
    int classrooms = 0;
    for (const Room& r : plan.rooms) {
        const RoomLayout& l = Layouts().Get(r.id);
        const Rect box = r.rect.Inflated(0.05f);
        const auto inside = [&](const std::vector<Spot>& spots, const char* what) {
            for (const Spot& s : spots) CHECK_MSG(box.Contains(s.pos), "%s: %s spot (%.2f, %.2f) outside the room", r.name.c_str(), what, static_cast<double>(s.pos.x), static_cast<double>(s.pos.z));
        };
        inside(l.seats, "seat");
        inside(l.teacher, "teacher");
        inside(l.work, "work");
        inside(l.idle, "idle");
        inside(l.queue, "queue");
        inside(l.eat, "eat");
        inside(l.browse, "browse");
        for (const Fixture& f : l.fixtures)
            CHECK_MSG(r.rect.Inflated(1.2f).Contains(f.pos), "%s: fixture %d at (%.1f, %.1f) is outside the room", r.name.c_str(), static_cast<int>(f.type), static_cast<double>(f.pos.x), static_cast<double>(f.pos.z));
        if (r.type == RoomType::Classroom) {
            ++classrooms;
            CHECK_MSG(l.seats.size() >= 12, "%s has only %zu seats", r.name.c_str(), l.seats.size());
            CHECK(!l.teacher.empty());
            // seats do not coincide
            for (std::size_t a = 0; a < l.seats.size(); ++a)
                for (std::size_t b = a + 1; b < l.seats.size(); ++b) CHECK(DistanceSq(l.seats[a].pos, l.seats[b].pos) > 0.04f);
        }
        if (r.type == RoomType::Cafeteria) {
            CHECK(l.queue.size() >= 16);
            CHECK(l.eat.size() >= 100);
        }
        if (r.type == RoomType::Library) CHECK(l.browse.size() >= 10 && l.seats.size() >= 40);
        if (r.type == RoomType::Auditorium) CHECK(l.seats.size() >= 200);
        if (r.type == RoomType::Gym) CHECK(l.seats.size() >= 30);
        if (r.type == RoomType::Corridor) CHECK(!l.idle.empty());
    }
    CHECK(classrooms > 200);
    CHECK(Layouts().Built() == static_cast<int>(plan.rooms.size()));
}

// --- the simulation -------------------------------------------------------------------------------------------------------------------

struct Rig {
    SimSettings settings;
    std::unique_ptr<SchoolSim> sim;
    explicit Rig(int students, bool full = false, std::uint64_t seed = 1, float radius = 38.0f)
    {
        settings.students = students;
        settings.fullRadius = radius;
        settings.reducedRadius = radius * 2.0f;
        settings.seed = seed;
        settings.enableFull = full;
        sim = std::make_unique<SchoolSim>(Plan(), Nav(), Layouts(), BellSchedule::Default(), settings, &LiftNav());
    }
};

int CountMode(const SchoolSim& sim, Mode m)
{
    int n = 0;
    for (const Npc& p : sim.Pop().npcs) n += sim.AgentOf(p.id).mode == m ? 1 : 0;
    return n;
}

void TestPresets()
{
    CHECK(PopulationSpec::FromPreset(Preset::Empty).students == 0);
    CHECK(PopulationSpec::FromPreset(Preset::Light).students == 100);
    CHECK(PopulationSpec::FromPreset(Preset::Normal).students == 500);
    CHECK(PopulationSpec::FromPreset(Preset::Busy).students == 1000);
    CHECK(PopulationSpec::FromPreset(Preset::Extreme).students >= 2000);
    bool ok = false;
    CHECK(ParsePreset("BUSY", &ok) == Preset::Busy && ok);
    ParsePreset("huge", &ok);
    CHECK(!ok);
}

void TestEmptySchoolRuns()
{
    Rig rig(0);
    CHECK(rig.sim->Pop().npcs.empty());
    rig.sim->Advance(86400.0 / 2);
    CHECK(rig.sim->CheckInvariants().empty());
    rig.sim->NextDay();
    CHECK(rig.sim->Day() == 1);
}

void TestOneDayAbstract()
{
    Rig rig(300);
    SchoolSim& sim = *rig.sim;
    CHECK(CountMode(sim, Mode::Offsite) == static_cast<int>(sim.Pop().npcs.size()));
    // Before the bell nobody walks; right after, students are on the move.
    sim.FastForwardTo(Hours(8, 30));
    CHECK(CountMode(sim, Mode::Transit) <= 6);   // staff on rounds
    CHECK(sim.Stats().unreachable == 0);
    const int bells0 = sim.Stats().bellsRung;
    sim.FastForwardTo(Hours(8, 45) + 20.0);
    CHECK(sim.Stats().bellsRung == bells0 + 1);
    const int walking = CountMode(sim, Mode::Transit);
    CHECK_MSG(walking > 100, "only %d walking 20 s after the bell", walking);
    sim.FastForwardTo(Hours(9, 1));
    CHECK(CountMode(sim, Mode::Transit) < walking / 3);
    // Mid-lesson, every student is at the room the timetable says.
    sim.FastForwardTo(Hours(10, 20));
    int wrong = 0, students = 0;
    for (const Npc& n : sim.Pop().npcs) {
        if (n.role != Role::Student) continue;
        ++students;
        const ClassGroup& g = sim.Pop().classes[static_cast<std::size_t>(n.classId)];
        const Lesson& l = g.timetable[static_cast<std::size_t>(sim.Weekday())][2];   // lesson 3
        const RoomId want = l.free ? g.home : l.room;
        const Agent& a = sim.AgentOf(n.id);
        if (a.mode != Mode::AtSpot || a.room != want) ++wrong;
    }
    CHECK_MSG(wrong == 0, "%d of %d students are not where lesson 3 is", wrong, students);
    CHECK(sim.CheckInvariants().empty());
    // The whole day, then everyone is gone.
    sim.NextDay();
    CHECK(sim.Day() == 1);
    CHECK(CountMode(sim, Mode::Offsite) == static_cast<int>(sim.Pop().npcs.size()));
    CHECK(sim.Stats().unreachable == 0);
}

void TestDeterminism()
{
    std::uint64_t a, b, c;
    {
        Rig rig(200, false, 7);
        rig.sim->FastForwardTo(Hours(11, 3));
        a = rig.sim->Digest();
    }
    {
        Rig rig(200, false, 7);
        rig.sim->FastForwardTo(Hours(11, 3));
        b = rig.sim->Digest();
    }
    {
        Rig rig(200, false, 8);
        rig.sim->FastForwardTo(Hours(11, 3));
        c = rig.sim->Digest();
    }
    CHECK(a == b);
    CHECK(a != c);
    // Jumping back replays the same state as running straight there.
    Rig rig(200, false, 7);
    rig.sim->FastForwardTo(Hours(14, 0));
    rig.sim->JumpToTime(Hours(11, 3));
    CHECK(rig.sim->Digest() == a);
}

void TestFullFidelityWalking()
{
    Rig rig(200, true, 1, 500.0f);   // everybody who walks is at full detail
    SchoolSim& sim = *rig.sim;
    sim.FastForwardTo(Hours(8, 40));
    // Focus the main lobby; run through the bell at 08:45 with full detail.
    sim.SetFocus({Focus{Plan().room(Plan().mainLobby).rect.Center(), 1, 60.0f}});
    double t = sim.Time();
    int maxFull = 0;
    while (sim.Time() < Hours(9, 5)) {
        sim.Advance(0.1);
        maxFull = std::max(maxFull, static_cast<int>(sim.FullAgents().size()));
        if (static_cast<int>((sim.Time() - t) * 10) % 50 == 0) CHECK(sim.CheckInvariants().empty());
    }
    CHECK_MSG(maxFull > 50, "never more than %d people at full detail", maxFull);
    CHECK(sim.Stats().doorPasses > 150);
    CHECK(sim.Stats().unstuck == 0);
    CHECK(sim.Stats().unreachable == 0);
    // Everyone ends up in their lesson-2 room.
    int wrong = 0;
    for (const Npc& n : sim.Pop().npcs) {
        if (n.role != Role::Student) continue;
        const ClassGroup& g = sim.Pop().classes[static_cast<std::size_t>(n.classId)];
        const Lesson& l = g.timetable[static_cast<std::size_t>(sim.Weekday())][1];
        const RoomId want = l.free ? g.home : l.room;
        const Agent& a = sim.AgentOf(n.id);
        if (a.mode != Mode::AtSpot || a.room != want) ++wrong;
    }
    CHECK_MSG(wrong == 0, "%d students are not in their lesson-2 room", wrong);
}

void TestFullAndAbstractAgree()
{
    // Whatever the level of detail, everybody reaches the same room by the end of the passing period.
    std::vector<RoomId> abstractRooms, fullRooms;
    for (const bool full : {false, true}) {
        Rig rig(150, full, 1, 500.0f);
        SchoolSim& sim = *rig.sim;
        sim.FastForwardTo(Hours(10, 40));
        if (full) sim.SetFocus({Focus{Plan().room(Plan().mainLobby).rect.Center(), 1, 70.0f}});
        while (sim.Time() < Hours(10, 55)) sim.Advance(full ? 0.1 : 1.0);
        for (const Npc& n : sim.Pop().npcs)
            if (n.role == Role::Student) (full ? fullRooms : abstractRooms).push_back(sim.AgentOf(n.id).room);
    }
    CHECK(abstractRooms.size() == fullRooms.size());
    int differ = 0;
    for (std::size_t i = 0; i < abstractRooms.size(); ++i) differ += abstractRooms[i] != fullRooms[i] ? 1 : 0;
    CHECK_MSG(differ == 0, "%d students are in different rooms at full and abstract detail", differ);
}

void TestSkipAndBackwards()
{
    Rig rig(100);
    SchoolSim& sim = *rig.sim;
    sim.SkipToNextPhase();
    CHECK(sim.Time() > Hours(7, 29));
    sim.SkipToNextPhase(true);
    CHECK(sim.Schedule().At(sim.PhaseIndex()).kind == PhaseKind::Lesson);
    CHECK(sim.Schedule().At(sim.PhaseIndex()).lesson == 1);
    sim.SkipToNextPhase(true);
    CHECK(sim.Schedule().At(sim.PhaseIndex()).lesson == 2);
    CHECK(sim.CheckInvariants().empty());
}

void TestFireDrill()
{
    Rig rig(300);
    SchoolSim& sim = *rig.sim;
    sim.FastForwardTo(Hours(10, 21) - 600 + 600);   // 10:21 lesson 3, drill (wednesday) not started by day 0 monday
    CHECK(!sim.Drill().active);
    int onCampus = 0;
    for (const Npc& n : sim.Pop().npcs) onCampus += sim.AgentOf(n.id).mode != Mode::Offsite ? 1 : 0;
    CHECK(onCampus > 300);
    sim.StartDrill();
    CHECK(sim.Drill().active);
    CHECK(sim.Drill().total == onCampus);
    CHECK(!sim.TakeBells().empty());
    double t0 = sim.Time();
    while (sim.Drill().allAssembledAt < 0.0 && sim.Time() < t0 + 1500.0) sim.Advance(1.0);
    CHECK(sim.Drill().allAssembledAt >= 0.0);
    CHECK(sim.Drill().assembled == sim.Drill().total);
    CHECK_MSG(sim.Drill().allAssembledAt - t0 < 900.0, "evacuation took %.0f s", sim.Drill().allAssembledAt - t0);
    // Everyone is at an assembly point.
    int wrong = 0;
    for (const Npc& n : sim.Pop().npcs) {
        const Agent& a = sim.AgentOf(n.id);
        if (a.mode == Mode::Offsite) continue;
        if (a.mode != Mode::AtSpot || Plan().room(a.room).type != RoomType::AssemblyPoint) ++wrong;
    }
    CHECK(wrong == 0);
    sim.EndDrill();
    CHECK(!sim.Drill().active);
    sim.FastForwardTo(sim.Time() + 900.0);
    CHECK(sim.CheckInvariants().empty());
}

void TestElevators()
{
    const SchoolPlan& plan = Plan();
    CHECK_MSG(plan.elevators.size() >= 2, "only %zu elevators", plan.elevators.size());
    for (const ElevatorShaft& e : plan.elevators) {
        CHECK(e.levelHigh - e.levelLow >= 2);
        for (const RoomId cab : e.cab) CHECK(cab != kNone && plan.room(cab).type == RoomType::ElevatorCab);
    }
    // The plain graph never rides; the lift graph rides between distant levels of the same wing.
    const NavGraph& walk = Nav();
    const NavGraph& lift = LiftNav();
    for (const NavEdge& e : walk.Edges()) CHECK(e.elevator == kNone);
    const ElevatorShaft& e0 = plan.elevators.front();
    const RoomId low = e0.cab.front(), high = e0.cab.back();
    CHECK(walk.Reachable(low, high));
    CHECK(lift.Distance(low, high) < walk.Distance(low, high));
    // A route from a classroom next to the shaft's bottom to one next to its top uses the elevator.
    RoomId from = kNone, to = kNone;
    for (const Room& r : plan.rooms) {
        if (!Info(r.type).teaching || r.wing != plan.room(low).wing) continue;
        if (r.level == plan.room(low).level && Distance(r.rect.Center(), plan.room(low).rect.Center()) < 25.0f && from == kNone) from = r.id;
        if (r.level == plan.room(high).level && Distance(r.rect.Center(), plan.room(high).rect.Center()) < 25.0f && to == kNone) to = r.id;
    }
    CHECK(from != kNone && to != kNone);
    std::vector<int> route;
    CHECK(lift.Route(from, to, route));
    int rides = 0;
    for (const int e : route) rides += lift.Edge(e).elevator != kNone ? 1 : 0;
    CHECK_MSG(rides >= 2, "the route rides %d levels: %s -> %s, lift %.1f walk %.1f, cab-to-cab lift %.1f walk %.1f", rides, plan.room(from).name.c_str(), plan.room(to).name.c_str(),
              static_cast<double>(lift.Distance(from, to)), static_cast<double>(walk.Distance(from, to)), static_cast<double>(lift.Distance(low, high)), static_cast<double>(walk.Distance(low, high)));
    PathBuilder pb(lift);
    std::vector<Waypoint> pts;
    CHECK(pb.Build(from, plan.room(from).rect.Center(), to, plan.room(to).rect.Center(), pts));
    int ridePoints = 0;
    for (const Waypoint& w : pts) ridePoints += w.kind == WaypointKind::Ride ? 1 : 0;
    CHECK_MSG(ridePoints >= 3, "%d ride points", ridePoints);

    // Full fidelity around the shaft through a change of rooms: people ride and nobody is left waiting forever.
    Rig rig(400, true, 3, 500.0f);
    SchoolSim& sim = *rig.sim;
    sim.FastForwardTo(Hours(8, 40));
    sim.SetFocus({Focus{plan.room(low).rect.Center(), plan.room(low).level, 60.0f}});
    int maxPassengers = 0;
    bool moved = false;
    while (sim.Time() < Hours(9, 10)) {
        sim.Advance(0.1);
        for (int i = 0; i < sim.ElevatorCount(); ++i) {
            const ElevatorStatus st = sim.Elevator(i);
            maxPassengers = std::max(maxPassengers, st.passengers);
            moved |= st.moving;
            CHECK(st.passengers >= 0 && st.passengers <= 12);
        }
    }
    CHECK(sim.CheckInvariants().empty());
    CHECK_MSG(moved, "no cab ever moved");
    CHECK_MSG(sim.Stats().elevatorRides > 0, "nobody rode");
    CHECK(sim.Stats().unreachable == 0);
    CHECK_MSG(maxPassengers > 0, "no passenger counted");
    // Everyone ends up in their lesson-2 room, including the riders.
    int wrong = 0;
    for (const Npc& n : sim.Pop().npcs) {
        if (n.role != Role::Student) continue;
        const ClassGroup& g = sim.Pop().classes[static_cast<std::size_t>(n.classId)];
        const Lesson& l = g.timetable[static_cast<std::size_t>(sim.Weekday())][1];
        const RoomId want = l.free ? g.home : l.room;
        const Agent& a = sim.AgentOf(n.id);
        if (a.mode != Mode::AtSpot || a.room != want) ++wrong;
    }
    CHECK_MSG(wrong == 0, "%d students are not in their lesson-2 room", wrong);
}

void TestSoak()
{
    Rig rig(200);
    SchoolSim& sim = *rig.sim;
    std::size_t baselineEvents = 0;
    for (int d = 0; d < 6; ++d) {
        const int target = sim.Day() + 1;
        while (sim.Day() < target) sim.Advance(1.0);
        int stillThere = 0;
        for (const Npc& n : sim.Pop().npcs) stillThere += sim.AgentOf(n.id).mode != Mode::Offsite ? 1 : 0;
        CHECK_MSG(stillThere == 0, "day %d ended with %d people on campus", d, stillThere);
        CHECK(sim.CheckInvariants().empty());
        (void)baselineEvents;
    }
    CHECK(sim.Day() == 6);
    CHECK(sim.Stats().unreachable == 0);
}

}  // namespace

int main()
{
    struct Test {
        const char* name;
        void (*fn)();
    };
    const Test tests[] = {
        {"plan is valid", TestPlanIsValid},
        {"plan scale", TestPlanScale},
        {"plan rejects broken data", TestPlanRejectsBrokenData},
        {"room lookup", TestRoomLookup},
        {"stair heights", TestStairHeights},
        {"everything reachable", TestEverythingReachable},
        {"evacuation routes", TestEveryRoomHasAnEvacuationRoute},
        {"routes consistent", TestRoutesAreConsistent},
        {"unreachable destination", TestUnreachableDestination},
        {"multi-floor route", TestMultiFloorRoute},
        {"elevators", TestElevators},
        {"waypoints stay inside rooms", TestWaypointsStayInsideRooms},
        {"default schedule valid", TestDefaultScheduleValid},
        {"schedule parsing", TestScheduleParsing},
        {"invalid schedules rejected", TestInvalidSchedulesAreRejected},
        {"timetables", TestTimetables},
        {"timetable conflicts detected", TestTimetableDetectsConflicts},
        {"layouts", TestLayouts},
        {"presets", TestPresets},
        {"empty school", TestEmptySchoolRuns},
        {"one day, abstract", TestOneDayAbstract},
        {"determinism", TestDeterminism},
        {"full-fidelity walking", TestFullFidelityWalking},
        {"full and abstract agree", TestFullAndAbstractAgree},
        {"skipping phases", TestSkipAndBackwards},
        {"fire drill", TestFireDrill},
        {"soak (six days)", TestSoak},
    };
    for (const Test& t : tests) {
        g_test = t.name;
        const int before = g_failures;
        t.fn();
        std::printf("%-34s %s\n", t.name, g_failures == before ? "ok" : "FAILED");
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
