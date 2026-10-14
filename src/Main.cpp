#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "App/SchoolGame.hpp"
#include "App/SchoolOptions.hpp"
#include "Simulation/Population.hpp"
#include "Simulation/Schedule.hpp"
#include "World/SchoolPlan.hpp"

int main(int argc, char** argv)
{
    try {
        const SchoolOptions options = SchoolOptions::Parse(argc, argv);
        if (options.showHelp) {
            std::fputs(SchoolOptions::Usage(), stdout);
            return 0;
        }
        if (options.validate) {
            // Data-only: no window, no device; the exit status is the result.
            const School::SchoolPlan plan = School::BuildSchoolPlan(School::PlanOptions{options.seed});
            std::vector<std::string> errors = School::ValidatePlan(plan);
            School::PopulationSpec spec = School::PopulationSpec::FromPreset(School::ParsePreset(options.preset), 7, options.seed);
            if (options.students >= 0) spec.students = options.students;
            const School::Population pop = School::BuildPopulation(plan, spec);
            for (const std::string& e : School::ValidateTimetable(plan, pop)) errors.push_back("timetable: " + e);
            for (const std::string& e : School::BellSchedule::Default().Validate()) errors.push_back("schedule: " + e);
            for (const std::string& e : errors) std::fprintf(stderr, "invalid: %s\n", e.c_str());
            const School::PlanStats s = School::ComputeStats(plan);
            std::printf("rooms %d (indoor %d, outdoor %d), doors %d, stairs %d, people %zu (%d students in %zu classes): %s\n", s.rooms, s.indoorRooms, s.outdoorZones, s.doors, s.stairs,
                        pop.npcs.size(), pop.StudentCount(), pop.classes.size(), errors.empty() ? "ok" : "INVALID");
            return errors.empty() ? 0 : 1;
        }
        SchoolGame game(options);
        game.Run();
        return 0;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "cna-school-simulator: %s\n", failure.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "cna-school-simulator: unknown error\n");
        return 1;
    }
}
