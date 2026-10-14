#include "Simulation/Schedule.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace School {

const char* PhaseKindName(PhaseKind k)
{
    switch (k) {
    case PhaseKind::Night: return "night";
    case PhaseKind::Arrival: return "arrival";
    case PhaseKind::Lesson: return "lesson";
    case PhaseKind::Passing: return "passing";
    case PhaseKind::LongBreak: return "longbreak";
    case PhaseKind::Lunch: return "lunch";
    case PhaseKind::Dismissal: return "dismissal";
    case PhaseKind::Clubs: return "clubs";
    case PhaseKind::Closing: return "closing";
    }
    return "?";
}

std::string ClockText(double t, bool seconds)
{
    const int total = static_cast<int>(t) % 86400;
    char buf[16];
    if (seconds) std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    else std::snprintf(buf, sizeof buf, "%02d:%02d", total / 3600, (total / 60) % 60);
    return buf;
}

namespace {
bool ParseClock(const std::string& s, double& out)
{
    int h = 0, m = 0;
    if (std::sscanf(s.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 24 || m < 0 || m > 59) return false;
    out = Hours(h, m);
    return true;
}
bool ParseKind(const std::string& s, PhaseKind& out)
{
    for (const PhaseKind k : {PhaseKind::Night, PhaseKind::Arrival, PhaseKind::Lesson, PhaseKind::Passing, PhaseKind::LongBreak, PhaseKind::Lunch,
                              PhaseKind::Dismissal, PhaseKind::Clubs, PhaseKind::Closing})
        if (s == PhaseKindName(k)) {
            out = k;
            return true;
        }
    return false;
}
bool KindRings(PhaseKind k)
{
    return k == PhaseKind::Lesson || k == PhaseKind::Passing || k == PhaseKind::LongBreak || k == PhaseKind::Lunch || k == PhaseKind::Dismissal;
}
}  // namespace

BellSchedule BellSchedule::Default()
{
    static const char* kText =
        "# time kind lesson name\n"
        "00:00 night - Closed\n"
        "07:30 arrival - Doors open\n"
        "07:50 passing 1 Go to lessons\n"
        "08:00 lesson 1 Lesson 1\n"
        "08:45 passing 2 Change of rooms\n"
        "08:55 lesson 2 Lesson 2\n"
        "09:40 longbreak - Long break\n"
        "09:55 passing 3 Change of rooms\n"
        "10:00 lesson 3 Lesson 3\n"
        "10:45 passing 4 Change of rooms\n"
        "10:55 lesson 4 Lesson 4\n"
        "11:40 passing 5 Change of rooms\n"
        "11:50 lesson 5 Lesson 5\n"
        "12:35 lunch - Lunch\n"
        "13:30 passing 6 Change of rooms\n"
        "13:40 lesson 6 Lesson 6\n"
        "14:25 passing 7 Change of rooms\n"
        "14:30 lesson 7 Lesson 7\n"
        "15:15 dismissal - End of school\n"
        "15:40 clubs - After-school clubs\n"
        "17:30 closing - Closing\n"
        "19:00 night - Closed\n"
        "drill 10:20 wed\n";
    std::vector<std::string> errors;
    return Parse(kText, errors);
}

BellSchedule BellSchedule::Parse(const std::string& text, std::vector<std::string>& errors)
{
    BellSchedule s;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (const std::size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        std::string first;
        if (!(ls >> first)) continue;
        const auto err = [&](const std::string& m) { errors.push_back("schedule line " + std::to_string(lineNo) + ": " + m); };
        if (first == "name") {
            std::string rest;
            std::getline(ls, rest);
            const std::size_t b = rest.find_first_not_of(' ');
            if (b != std::string::npos) s.name = rest.substr(b);
            continue;
        }
        if (first == "drill") {
            std::string time, day;
            ls >> time >> day;
            DrillEntry d;
            if (!ParseClock(time, d.time)) {
                err("bad drill time '" + time + "'");
                continue;
            }
            static const char* names[] = {"mon", "tue", "wed", "thu", "fri"};
            for (int i = 0; i < 5; ++i)
                if (day.substr(0, 3) == names[i]) d.weekday = i;
            s.drills.push_back(d);
            continue;
        }
        Phase p;
        if (!ParseClock(first, p.start)) {
            err("bad time '" + first + "'");
            continue;
        }
        std::string kind, lesson;
        ls >> kind >> lesson;
        if (!ParseKind(kind, p.kind)) {
            err("unknown phase kind '" + kind + "'");
            continue;
        }
        p.lesson = lesson == "-" ? 0 : std::atoi(lesson.c_str());
        std::string rest;
        std::getline(ls, rest);
        const std::size_t b = rest.find_first_not_of(' ');
        p.name = b == std::string::npos ? std::string(PhaseKindName(p.kind)) : rest.substr(b);
        p.bell = KindRings(p.kind);
        if (p.name.find("nobell") != std::string::npos) p.bell = false;
        s.phases.push_back(p);
    }
    std::stable_sort(s.phases.begin(), s.phases.end(), [](const Phase& a, const Phase& b) { return a.start < b.start; });
    for (std::size_t i = 0; i < s.phases.size(); ++i) s.phases[i].end = i + 1 < s.phases.size() ? s.phases[i + 1].start : 86400.0;
    return s;
}

int BellSchedule::LessonCount() const
{
    int n = 0;
    for (const Phase& p : phases)
        if (p.kind == PhaseKind::Lesson) n = std::max(n, p.lesson);
    return n;
}

int BellSchedule::PhaseAt(double t) const
{
    int found = 0;
    for (std::size_t i = 0; i < phases.size(); ++i)
        if (phases[i].start <= t) found = static_cast<int>(i);
    return found;
}

int BellSchedule::Find(PhaseKind kind, int lesson) const
{
    for (std::size_t i = 0; i < phases.size(); ++i)
        if (phases[i].kind == kind && (lesson == 0 || phases[i].lesson == lesson)) return static_cast<int>(i);
    return -1;
}

std::vector<std::string> BellSchedule::Validate() const
{
    std::vector<std::string> errors;
    if (phases.empty()) {
        errors.push_back("schedule has no phases");
        return errors;
    }
    if (phases.front().start > 0.0) errors.push_back("schedule does not start at 00:00 (the day before the first phase is undefined)");
    int lastLesson = 0;
    bool seenDismissal = false;
    for (std::size_t i = 0; i < phases.size(); ++i) {
        const Phase& p = phases[i];
        if (p.end <= p.start) errors.push_back("phase '" + p.name + "' at " + ClockText(p.start) + " has no duration (two phases start at the same time)");
        if (p.start < 0.0 || p.end > 86400.0) errors.push_back("phase '" + p.name + "' lies outside the day");
        if (p.kind == PhaseKind::Lesson) {
            if (p.lesson != lastLesson + 1)
                errors.push_back("lesson numbers must run 1, 2, 3...: '" + p.name + "' is lesson " + std::to_string(p.lesson) + " after lesson " + std::to_string(lastLesson));
            lastLesson = p.lesson;
            if (p.end - p.start < 600.0) errors.push_back("lesson '" + p.name + "' is shorter than ten minutes");
        }
        if (p.kind == PhaseKind::Passing && p.lesson != 0 && i + 1 < phases.size() &&
            !(phases[i + 1].kind == PhaseKind::Lesson && phases[i + 1].lesson == p.lesson))
            errors.push_back("passing period before '" + phases[i + 1].name + "' names lesson " + std::to_string(p.lesson) + ", which does not follow it");
        if (p.kind == PhaseKind::Dismissal) seenDismissal = true;
        if (p.kind == PhaseKind::Arrival && i > 0 && phases[i - 1].kind != PhaseKind::Night) errors.push_back("arrival should follow a night phase");
    }
    if (lastLesson == 0) errors.push_back("schedule has no lessons");
    if (!seenDismissal) errors.push_back("schedule has no dismissal: students would never go home");
    if (Find(PhaseKind::Lunch) < 0) errors.push_back("schedule has no lunch");
    for (const DrillEntry& d : drills)
        if (d.time < 0.0 || d.time >= 86400.0) errors.push_back("a drill is scheduled outside the day");
    return errors;
}

}  // namespace School
