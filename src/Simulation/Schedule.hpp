#pragma once

// The shape of a school day: an ordered list of phases, each starting when the previous one ends.
// The default schedule is built in; a schedule file (Content/Data/schedule.cfg) replaces it:
//
//     # time  kind       [lesson number]  name
//     07:30   arrival    -                Doors open
//     08:00   lesson     1                First lesson
//     08:45   passing    -                Change of rooms
//     09:40   longbreak  -                Long break
//     12:35   lunch      -                Lunch
//     15:15   dismissal  -                End of school
//     15:40   clubs      -                After-school clubs
//     17:30   closing    -                Closing
//     19:00   night      -                Closed
//     drill   10:20      -                (a fire drill at that time on the given weekday, e.g. "drill 10:20 wed")
//
// Times are hours:minutes of the school day. A phase without a "bell" word rings its bell at its start
// when its kind says so (lesson, passing, longbreak, lunch, dismissal).

#include <string>
#include <vector>

namespace School {

enum class PhaseKind { Night, Arrival, Lesson, Passing, LongBreak, Lunch, Dismissal, Clubs, Closing };

const char* PhaseKindName(PhaseKind k);

struct Phase {
    double start = 0.0;     // seconds since midnight
    double end = 0.0;       // set from the next phase
    PhaseKind kind = PhaseKind::Night;
    int lesson = 0;         // 1-based lesson number for Lesson phases; for Passing: the lesson that comes next
    bool bell = false;
    std::string name;
};

struct DrillEntry {
    double time = 0.0;
    int weekday = -1;       // 0 = Monday ... 4 = Friday, -1 = every day
};

class BellSchedule {
public:
    std::string name = "Standard school day";
    std::vector<Phase> phases;
    std::vector<DrillEntry> drills;

    static BellSchedule Default();
    /** Parses the configuration text; problems are appended to `errors` and the line is skipped. */
    static BellSchedule Parse(const std::string& text, std::vector<std::string>& errors);

    /** Problems that make the schedule unusable: overlaps, a lesson number out of order, no lessons... */
    [[nodiscard]] std::vector<std::string> Validate() const;

    [[nodiscard]] int LessonCount() const;
    /** Index of the phase active at `t` (seconds since midnight); the last phase runs to midnight. */
    [[nodiscard]] int PhaseAt(double t) const;
    [[nodiscard]] const Phase& At(int index) const { return phases[static_cast<std::size_t>(index)]; }
    /** First phase of a given kind and lesson number, or -1. */
    [[nodiscard]] int Find(PhaseKind kind, int lesson = 0) const;
    /** Phase index of the lesson with this number (1-based), or -1. */
    [[nodiscard]] int LessonPhase(int lesson) const { return Find(PhaseKind::Lesson, lesson); }
};

constexpr double Hours(int h, int m = 0) { return h * 3600.0 + m * 60.0; }
std::string ClockText(double secondsSinceMidnight, bool seconds = false);

}  // namespace School
