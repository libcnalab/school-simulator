// Headless command line for the simulation core: validation, plan statistics, simulated days, soak runs.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Navigation/NavGraph.hpp"
#include "Rooms/RoomLayout.hpp"
#include "Simulation/Population.hpp"
#include "Simulation/SchoolSim.hpp"
#include <chrono>
#include "World/SchoolPlan.hpp"

using namespace School;

int main(int argc, char** argv)
{
    bool validate = false, stats = false, population = false, day = false, check = false, full = false;
    int days = 1;
    double focusX = 82, focusZ = 112;
    int students = -1;
    Preset preset = Preset::Normal;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--validate")) validate = true;
        else if (!std::strcmp(argv[i], "--stats")) stats = true;
        else if (!std::strcmp(argv[i], "--population")) population = true;
        else if (!std::strcmp(argv[i], "--students") && i + 1 < argc) students = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--day")) day = true;
        else if (!std::strcmp(argv[i], "--days") && i + 1 < argc) { day = true; days = std::atoi(argv[++i]); }
        else if (!std::strcmp(argv[i], "--check")) check = true;
        else if (!std::strcmp(argv[i], "--full")) full = true;
        else if (!std::strcmp(argv[i], "--preset") && i + 1 < argc) {
            bool ok = false;
            preset = ParsePreset(argv[++i], &ok);
            if (!ok) std::fprintf(stderr, "unknown preset '%s'\n", argv[i]);
        }
    }
    const SchoolPlan plan = BuildSchoolPlan();
    int status = 0;
    if (validate || stats) {
        const std::vector<std::string> errors = ValidatePlan(plan);
        for (std::size_t i = 0; i < errors.size() && i < 60; ++i) std::fprintf(stderr, "invalid: %s\n", errors[i].c_str());
        std::printf("plan: %s (%zu problems)\n", errors.empty() ? "ok" : "INVALID", errors.size());
        if (!errors.empty()) status = 1;
    }
    if (stats) {
        const PlanStats s = ComputeStats(plan);
        std::printf("rooms %d (indoor %d, outdoor zones %d, corridor segments %d), doors %d, stairs %d, elevators %d, indoor area %.0f m2\n", s.rooms,
                    s.indoorRooms, s.outdoorZones, s.corridors, s.doors, s.stairs, s.elevators, static_cast<double>(s.indoorAreaM2));
    }
    if (population) {
        PopulationSpec spec = PopulationSpec::FromPreset(preset);
        if (students >= 0) spec.students = students;
        const Population pop = BuildPopulation(plan, spec);
        const std::vector<std::string> errors = ValidateTimetable(plan, pop);
        for (std::size_t i = 0; i < errors.size() && i < 30; ++i) std::fprintf(stderr, "invalid: %s\n", errors[i].c_str());
        int freeLessons = 0, lessons = 0;
        for (const ClassGroup& g : pop.classes)
            for (int d = 0; d < kSchoolDays; ++d)
                for (int p = 0; p < pop.periods; ++p) {
                    ++lessons;
                    freeLessons += g.timetable[static_cast<std::size_t>(d)][static_cast<std::size_t>(p)].free ? 1 : 0;
                }
        std::printf("population: %zu npcs (%d students in %zu classes, %zu teachers), free lessons %d of %d; timetable %s (%zu problems)\n", pop.npcs.size(),
                    pop.StudentCount(), pop.classes.size(), pop.teachers.size(), freeLessons, lessons, errors.empty() ? "ok" : "INVALID", errors.size());
        if (!errors.empty()) status = 1;
    }
    if (day) {
        const NavGraph nav(plan);
        const NavGraph liftNav(plan, true);
        const LayoutCache layouts(plan);
        SimSettings settings;
        settings.preset = preset;
        settings.students = students;
        settings.enableFull = full;
        SchoolSim sim(plan, nav, layouts, BellSchedule::Default(), settings, &liftNav);
        std::printf("simulating %d day(s): %zu npcs\n", days, sim.Pop().npcs.size());
        const auto t0 = std::chrono::steady_clock::now();
        for (int d = 0; d < days; ++d) {
            if (full) sim.SetFocus({Focus{Vec2{static_cast<float>(focusX), static_cast<float>(focusZ)}, 1, 40.0f}});
            double nextReport = 0;
            int lastPhase = -1;
            double lastLate = 0;
            while (sim.Day() == d) {
                sim.Advance(full ? 0.1 : 1.0);
                if (days == 1 && sim.PhaseIndex() != lastPhase) {
                    if (lastPhase >= 0) std::printf("   [late arrivals in '%s': %.0f]\n", sim.Schedule().At(lastPhase).name.c_str(), sim.Stats().lateArrivals - lastLate);
                    lastLate = sim.Stats().lateArrivals;
                    lastPhase = sim.PhaseIndex();
                }
                if (sim.Time() >= nextReport) {
                    nextReport += 900;
                    const SimStats& st = sim.Stats();
                    int off = 0, spot = 0, transit = 0;
                    for (const Npc& n : sim.Pop().npcs) {
                        const Agent& a = sim.AgentOf(n.id);
                        off += a.mode == Mode::Offsite;
                        spot += a.mode == Mode::AtSpot;
                        transit += a.mode == Mode::Transit;
                    }
                    if (days == 1) std::printf("%s  %-18s off %4d  seated %4d  walking %4d  full %3zu  doorpasses %lld  unstuck %lld  unreachable %d\n", ClockText(sim.Time()).c_str(),
                                               sim.Schedule().At(sim.PhaseIndex()).name.c_str(), off, spot, transit, sim.FullAgents().size(), st.doorPasses, st.unstuck, st.unreachable);
                }
                if (check) {
                    const std::vector<std::string> problems = sim.CheckInvariants();
                    if (!problems.empty()) {
                        std::fprintf(stderr, "invariant broken at %s: %s (+%zu more)\n", ClockText(sim.Time()).c_str(), problems[0].c_str(), problems.size() - 1);
                        return 1;
                    }
                }
            }
            int stillThere = 0;
            for (const Npc& n : sim.Pop().npcs) stillThere += sim.AgentOf(n.id).mode != Mode::Offsite;
            std::printf("day %d done: %d still on campus, digest %016llx\n", d, stillThere, static_cast<unsigned long long>(sim.Digest()));
            if (check && stillThere > 0) status = 1;
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const SimStats& st = sim.Stats();
        std::printf("%.2f s wall; path builds %d, events %lld, door passes %lld, elevator rides %lld, unstuck %lld, unreachable %d, bells %d, late arrivals %.0f\n", secs,
                    st.pathBuilds, st.events, st.doorPasses, st.elevatorRides, st.unstuck, st.unreachable, st.bellsRung, st.lateArrivals);
        if (check && (st.unreachable > 0)) status = 1;
    }
    return status;
}
