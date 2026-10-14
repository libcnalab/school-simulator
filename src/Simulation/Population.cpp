#include "Simulation/Population.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "World/Rng.hpp"

namespace School {

const char* RoleName(Role r)
{
    static const char* names[] = {"Student", "Teacher", "Receptionist", "Principal", "Administrator", "Nurse", "Counselor",
                                  "IT technician", "Security guard", "Cook", "Librarian", "Janitor", "Maintenance"};
    return names[static_cast<int>(r)];
}

const char* SubjectName(Subject s)
{
    static const char* names[] = {"Mathematics", "Language", "English", "History", "Geography", "Physics", "Chemistry", "Biology",
                                  "Physical education", "Music", "Art", "Computing", "Technology", "Drama", "Library"};
    return names[static_cast<int>(s)];
}

RoomType SubjectRoomType(Subject s)
{
    switch (s) {
    case Subject::English: return RoomType::LanguageRoom;
    case Subject::Physics: return RoomType::PhysicsLab;
    case Subject::Chemistry: return RoomType::ChemistryLab;
    case Subject::Biology: return RoomType::BiologyLab;
    case Subject::PE: return RoomType::Gym;
    case Subject::Music: return RoomType::MusicRoom;
    case Subject::Art: return RoomType::ArtStudio;
    case Subject::IT: return RoomType::ComputerLab;
    case Subject::Technology: return RoomType::Workshop;
    case Subject::Drama: return RoomType::DramaStudio;
    case Subject::Library: return RoomType::Library;
    default: return RoomType::Classroom;
    }
}

const char* PresetName(Preset p)
{
    static const char* names[] = {"Empty", "Light", "Normal", "Busy", "Extreme"};
    return names[static_cast<int>(p)];
}

Preset ParsePreset(const std::string& s, bool* ok)
{
    if (ok) *ok = true;
    std::string l = s;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (l == "empty") return Preset::Empty;
    if (l == "light") return Preset::Light;
    if (l == "normal") return Preset::Normal;
    if (l == "busy") return Preset::Busy;
    if (l == "extreme") return Preset::Extreme;
    if (ok) *ok = false;
    return Preset::Normal;
}

PopulationSpec PopulationSpec::FromPreset(Preset p, int periods, std::uint64_t seed)
{
    PopulationSpec s;
    s.periods = periods;
    s.seed = seed;
    switch (p) {
    case Preset::Empty: s.students = 0; break;
    case Preset::Light: s.students = 100; break;
    case Preset::Normal: s.students = 500; break;
    case Preset::Busy: s.students = 1000; break;
    case Preset::Extreme: s.students = 2400; break;
    }
    return s;
}

int Population::StudentCount() const
{
    int n = 0;
    for (const Npc& p : npcs) n += p.role == Role::Student ? 1 : 0;
    return n;
}

int Population::TeacherIndex(int npcId) const
{
    for (std::size_t i = 0; i < teachers.size(); ++i)
        if (teachers[i] == npcId) return static_cast<int>(i);
    return -1;
}

namespace {

const char* kFirst[] = {"Adam", "Alice", "Anna", "Ben", "Clara", "Daniel", "Eva", "Filip", "Hana", "Jakub", "Karolina", "Lukas", "Marie", "Martin", "Nela",
                        "Ondrej", "Petra", "Quentin", "Rosa", "Sofia", "Tereza", "Tomas", "Uma", "Viktor", "Zoe", "Amir", "Bela", "Chen", "Dara", "Elias",
                        "Fatima", "Goran", "Hugo", "Ines", "Jonas", "Kira", "Leo", "Mia", "Noah", "Olga", "Pavel", "Rita", "Sven", "Tina", "Ugo", "Vera",
                        "Wen", "Xavi", "Yara", "Zdenek", "Emil", "Lena", "Oskar", "Ida", "Milan", "Nina", "Radek", "Sara", "Timo", "Vlad"};
const char* kLast[] = {"Novak", "Svoboda", "Dvorak", "Cerny", "Prochazka", "Kratochvil", "Horak", "Pospisil", "Kral", "Vesely", "Mueller", "Rossi", "Silva",
                       "Kim", "Nguyen", "Garcia", "Jensen", "Kowalski", "Ivanov", "Haddad", "Okafor", "Tanaka", "Lopez", "Berg", "Moreau", "Fischer", "Costa",
                       "Andersen", "Popescu", "Horvat", "Varga", "Marek", "Blaha", "Jelinek", "Kolar", "Navratil", "Urban", "Vlcek", "Soukup", "Beran"};

std::string MakeName(std::uint64_t seed, int id)
{
    const std::uint64_t a = Hash(seed, static_cast<std::uint64_t>(id), 1);
    const std::uint64_t b = Hash(seed, static_cast<std::uint64_t>(id), 2);
    const std::size_t nf = sizeof(kFirst) / sizeof(kFirst[0]);
    const std::size_t nl = sizeof(kLast) / sizeof(kLast[0]);
    return std::string(kFirst[a % nf]) + " " + kLast[b % nl];
}

// Weekly lessons for one class at 7 periods a day; scaled for other day lengths.
struct Quota {
    Subject subject;
    int weekly;
};
constexpr Quota kQuota[] = {
    {Subject::Math, 5},      {Subject::Language, 5}, {Subject::English, 4}, {Subject::History, 2}, {Subject::Geography, 2},
    {Subject::Physics, 2},   {Subject::Chemistry, 2}, {Subject::Biology, 2}, {Subject::PE, 3},       {Subject::Music, 1},
    {Subject::Art, 2},       {Subject::IT, 2},        {Subject::Technology, 1}, {Subject::Drama, 1}, {Subject::Library, 1},
};

bool UsesHomeRoom(Subject s)
{
    return s == Subject::Math || s == Subject::Language || s == Subject::History || s == Subject::Geography;
}

struct RoomOption {
    RoomId room;
    int concurrency;
};

}  // namespace

Population BuildPopulation(const SchoolPlan& plan, const PopulationSpec& spec)
{
    Population pop;
    pop.periods = std::clamp(spec.periods, 1, kMaxPeriods);
    const int periods = pop.periods;
    const std::uint64_t seed = spec.seed;
    Rng rng(Hash(seed, 0xBEEF));
    if (spec.students <= 0) return pop;

    // --- classes and home rooms -------------------------------------------------------------------------
    const int classCount = std::max(1, static_cast<int>(std::lround(static_cast<float>(spec.students) / spec.studentsPerClass)));
    const int years = std::clamp(classCount / 3, 1, 6);
    // Classrooms bucketed by (level, wing) so classes spread over all floors and wings.
    std::map<std::pair<int, int>, std::vector<RoomId>> buckets;
    for (const Room& r : plan.rooms)
        if (r.type == RoomType::Classroom && r.capacity >= 20) buckets[{r.level, r.wing}].push_back(r.id);
    std::vector<std::vector<RoomId>*> order;
    for (auto& [key, rooms] : buckets) order.push_back(&rooms);
    std::vector<std::size_t> cursor(order.size(), 0);
    pop.classes.resize(static_cast<std::size_t>(classCount));
    int studentsLeft = spec.students;
    int nextNpc = 0;
    for (int c = 0; c < classCount; ++c) {
        ClassGroup& g = pop.classes[static_cast<std::size_t>(c)];
        g.id = c;
        g.year = 7 + (c % years);
        const int sameYearBefore = c / years;
        char name[16];
        std::snprintf(name, sizeof name, "%d%c", g.year, 'A' + sameYearBefore % 26);
        g.name = name;
        g.lunchWave = c % 2;
        // next bucket with a free room, walking a stride through the buckets
        for (std::size_t tries = 0; tries < order.size() && g.home == kNone; ++tries) {
            const std::size_t b = (static_cast<std::size_t>(c) * 7 + tries) % order.size();
            if (cursor[b] < order[b]->size()) g.home = (*order[b])[cursor[b]++];
        }
        const int size = std::min(studentsLeft, static_cast<int>(std::lround(spec.studentsPerClass)));
        studentsLeft -= size;
        for (int i = 0; i < size; ++i) {
            Npc n;
            n.id = nextNpc++;
            n.role = Role::Student;
            n.name = MakeName(seed, n.id);
            n.classId = c;
            n.look = static_cast<std::uint32_t>(Hash(seed, static_cast<std::uint64_t>(n.id), 3));
            n.speed = 1.15f + 0.35f * Unit(Hash(seed, static_cast<std::uint64_t>(n.id), 4));
            g.students.push_back(n.id);
            pop.npcs.push_back(std::move(n));
        }
    }
    // Remaining students (rounding) join the last class.
    while (studentsLeft > 0) {
        Npc n;
        n.id = nextNpc++;
        n.role = Role::Student;
        n.name = MakeName(seed, n.id);
        n.classId = classCount - 1;
        n.look = static_cast<std::uint32_t>(Hash(seed, static_cast<std::uint64_t>(n.id), 3));
        n.speed = 1.15f + 0.35f * Unit(Hash(seed, static_cast<std::uint64_t>(n.id), 4));
        pop.classes.back().students.push_back(n.id);
        pop.npcs.push_back(std::move(n));
        --studentsLeft;
    }

    // --- teachers ------------------------------------------------------------------------------------------------
    // Weekly quota scaled to the day length (largest remainder).
    std::array<int, static_cast<int>(Subject::Count)> quota{};
    {
        const int total = periods * kSchoolDays;
        int base = 0;
        for (const Quota& q : kQuota) base += q.weekly;
        std::vector<std::pair<float, int>> remainders;
        int assigned = 0;
        for (const Quota& q : kQuota) {
            const float exact = static_cast<float>(q.weekly) * static_cast<float>(total) / static_cast<float>(base);
            quota[static_cast<int>(q.subject)] = static_cast<int>(exact);
            assigned += static_cast<int>(exact);
            remainders.emplace_back(exact - std::floor(exact), static_cast<int>(q.subject));
        }
        std::sort(remainders.begin(), remainders.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (std::size_t i = 0; assigned < total && i < remainders.size(); ++i, ++assigned) ++quota[static_cast<std::size_t>(remainders[i].second)];
    }
    std::vector<std::vector<int>> teachersOf(static_cast<std::size_t>(Subject::Count));
    for (int s = 0; s < static_cast<int>(Subject::Count); ++s) {
        const Subject subject = static_cast<Subject>(s);
        if (subject == Subject::Library) continue;   // supervised by the librarians
        const int hours = quota[static_cast<std::size_t>(s)] * classCount;
        const int n = std::max(1, static_cast<int>(std::ceil(static_cast<double>(hours) / 22.0)));
        for (int i = 0; i < n; ++i) {
            Npc t;
            t.id = nextNpc++;
            t.role = Role::Teacher;
            t.subject = subject;
            t.name = MakeName(seed, t.id);
            t.look = static_cast<std::uint32_t>(Hash(seed, static_cast<std::uint64_t>(t.id), 3));
            t.speed = 1.1f + 0.3f * Unit(Hash(seed, static_cast<std::uint64_t>(t.id), 4));
            teachersOf[static_cast<std::size_t>(s)].push_back(t.id);
            pop.teachers.push_back(t.id);
            pop.npcs.push_back(std::move(t));
        }
    }

    // --- rooms by subject --------------------------------------------------------------------------------------------
    std::vector<std::vector<RoomOption>> options(static_cast<std::size_t>(Subject::Count));
    for (int s = 0; s < static_cast<int>(Subject::Count); ++s) {
        const Subject subject = static_cast<Subject>(s);
        if (UsesHomeRoom(subject)) continue;
        const RoomType type = SubjectRoomType(subject);
        for (const Room& r : plan.rooms) {
            if (r.type != type) continue;
            int cap = 1;
            if (type == RoomType::Gym) cap = 3;
            if (type == RoomType::Library) cap = 3;
            if (type == RoomType::DramaStudio) cap = 2;
            if (type == RoomType::MusicRoom && r.capacity > 40) cap = 2;
            options[static_cast<std::size_t>(s)].push_back({r.id, cap});
        }
        if (subject == Subject::PE) {
            // PE also happens outside: the football field and the running track.
            for (const Room& r : plan.rooms) {
                if (r.type == RoomType::FootballField) options[static_cast<std::size_t>(s)].push_back({r.id, 4});
                if (r.type == RoomType::Track && r.rect.Width() > 100.0f) options[static_cast<std::size_t>(s)].push_back({r.id, 3});
            }
        }
    }

    // --- timetable ----------------------------------------------------------------------------------------------------------
    const std::size_t slots = static_cast<std::size_t>(kSchoolDays * kMaxPeriods);
    std::vector<unsigned char> roomUse(plan.rooms.size() * slots, 0);
    std::vector<std::array<std::array<char, kMaxPeriods>, kSchoolDays>> teacherBusy(pop.teachers.size());
    for (auto& t : teacherBusy)
        for (auto& d : t) d.fill(0);
    std::vector<int> teacherLoad(pop.teachers.size(), 0);
    std::vector<std::array<int, static_cast<int>(Subject::Count)>> remaining(static_cast<std::size_t>(classCount));
    for (auto& r : remaining) r = quota;

    const auto teacherSlot = [&](int npcId) { return pop.TeacherIndex(npcId); };
    std::map<int, int> teacherIndexOf;
    for (std::size_t i = 0; i < pop.teachers.size(); ++i) teacherIndexOf[pop.teachers[i]] = static_cast<int>(i);
    (void)teacherSlot;

    for (int day = 0; day < kSchoolDays; ++day) {
        std::vector<std::array<bool, static_cast<int>(Subject::Count)>> doneToday(static_cast<std::size_t>(classCount));
        for (auto& d : doneToday) d.fill(false);
        for (int period = 0; period < periods; ++period) {
            for (int k = 0; k < classCount; ++k) {
                const int c = (k + day * 5 + period * 3) % classCount;
                ClassGroup& g = pop.classes[static_cast<std::size_t>(c)];
                const int slotsLeft = (kSchoolDays - day) * periods - period;
                // Score every subject that still has lessons owed; try the best first.
                std::vector<std::pair<float, int>> candidates;
                for (int s = 0; s < static_cast<int>(Subject::Count); ++s) {
                    const int rem = remaining[static_cast<std::size_t>(c)][static_cast<std::size_t>(s)];
                    if (rem <= 0) continue;
                    float score = static_cast<float>(rem) / static_cast<float>(slotsLeft) * 6.0f + rng.Uniform() * 1.2f;
                    if (doneToday[static_cast<std::size_t>(c)][static_cast<std::size_t>(s)]) score -= 4.0f;
                    if (period > 0 && g.timetable[static_cast<std::size_t>(day)][static_cast<std::size_t>(period - 1)].subject == static_cast<Subject>(s) &&
                        !g.timetable[static_cast<std::size_t>(day)][static_cast<std::size_t>(period - 1)].free) score -= 1.5f;
                    candidates.emplace_back(score, s);
                }
                std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return a.first > b.first; });
                Lesson chosen;
                chosen.free = true;
                chosen.room = g.home;
                for (const auto& [score, s] : candidates) {
                    (void)score;
                    const Subject subject = static_cast<Subject>(s);
                    // A room.
                    RoomId room = g.home;
                    int slot = 0;
                    if (!UsesHomeRoom(subject)) {
                        const auto& opts = options[static_cast<std::size_t>(s)];
                        if (opts.empty()) continue;
                        room = kNone;
                        const std::size_t start = Hash(seed, static_cast<std::uint64_t>(c), static_cast<std::uint64_t>(day * 16 + period)) % opts.size();
                        for (std::size_t i = 0; i < opts.size() && room == kNone; ++i) {
                            const RoomOption& o = opts[(start + i) % opts.size()];
                            const unsigned char used = roomUse[static_cast<std::size_t>(o.room) * slots + static_cast<std::size_t>(day * kMaxPeriods + period)];
                            if (used < o.concurrency) {
                                room = o.room;
                                slot = used;
                            }
                        }
                        if (room == kNone) continue;
                    } else if (g.home == kNone) {
                        continue;
                    }
                    // A teacher.
                    int teacher = -1;
                    if (subject != Subject::Library) {
                        int best = -1;
                        for (const int id : teachersOf[static_cast<std::size_t>(s)]) {
                            const int ti = teacherIndexOf[id];
                            if (teacherBusy[static_cast<std::size_t>(ti)][static_cast<std::size_t>(day)][static_cast<std::size_t>(period)] != 0) continue;
                            if (teacherLoad[static_cast<std::size_t>(ti)] >= periods * kSchoolDays - periods) continue;
                            if (best < 0 || teacherLoad[static_cast<std::size_t>(ti)] < teacherLoad[static_cast<std::size_t>(teacherIndexOf[best])]) best = id;
                        }
                        if (best < 0) continue;
                        teacher = best;
                    }
                    chosen.subject = subject;
                    chosen.teacher = teacher;
                    chosen.room = room;
                    chosen.slot = slot;
                    chosen.free = false;
                    if (!UsesHomeRoom(subject)) ++roomUse[static_cast<std::size_t>(room) * slots + static_cast<std::size_t>(day * kMaxPeriods + period)];
                    if (teacher >= 0) {
                        const int ti = teacherIndexOf[teacher];
                        teacherBusy[static_cast<std::size_t>(ti)][static_cast<std::size_t>(day)][static_cast<std::size_t>(period)] = 1;
                        ++teacherLoad[static_cast<std::size_t>(ti)];
                    }
                    --remaining[static_cast<std::size_t>(c)][static_cast<std::size_t>(s)];
                    doneToday[static_cast<std::size_t>(c)][static_cast<std::size_t>(s)] = true;
                    break;
                }
                g.timetable[static_cast<std::size_t>(day)][static_cast<std::size_t>(period)] = chosen;
            }
        }
    }

    // Teachers' view of the same timetable.
    pop.teacherClass.resize(pop.teachers.size());
    for (auto& t : pop.teacherClass)
        for (auto& d : t) d.fill(-1);
    for (const ClassGroup& g : pop.classes)
        for (int d = 0; d < kSchoolDays; ++d)
            for (int p = 0; p < periods; ++p) {
                const Lesson& l = g.timetable[static_cast<std::size_t>(d)][static_cast<std::size_t>(p)];
                if (!l.free && l.teacher >= 0) pop.teacherClass[static_cast<std::size_t>(teacherIndexOf[l.teacher])][static_cast<std::size_t>(d)][static_cast<std::size_t>(p)] = g.id;
            }

    // --- staff --------------------------------------------------------------------------------------------------------------------
    const auto roomsOf = [&](RoomType t) { return plan.RoomsOfType(t); };
    const std::vector<RoomId> offices = roomsOf(RoomType::AdminOffice);
    const std::vector<RoomId> principal = roomsOf(RoomType::PrincipalOffice);
    const std::vector<RoomId> nurse = roomsOf(RoomType::Nurse);
    const std::vector<RoomId> counseling = roomsOf(RoomType::Counseling);
    const std::vector<RoomId> servers = roomsOf(RoomType::ServerRoom);
    const std::vector<RoomId> security = roomsOf(RoomType::SecurityOffice);
    const std::vector<RoomId> kitchens = roomsOf(RoomType::Kitchen);
    const std::vector<RoomId> libraries = roomsOf(RoomType::Library);
    const std::vector<RoomId> boilers = roomsOf(RoomType::Boiler);
    const std::vector<RoomId> janitors = roomsOf(RoomType::Janitor);
    const auto pick = [](const std::vector<RoomId>& v, int i) { return v.empty() ? kNone : v[static_cast<std::size_t>(i) % v.size()]; };
    const auto addStaff = [&](Role role, int count, const std::vector<RoomId>& rooms, bool spreadOverRooms) {
        for (int i = 0; i < count; ++i) {
            Npc n;
            n.id = nextNpc++;
            n.role = role;
            n.name = MakeName(seed, n.id);
            n.look = static_cast<std::uint32_t>(Hash(seed, static_cast<std::uint64_t>(n.id), 3));
            n.speed = 1.1f + 0.3f * Unit(Hash(seed, static_cast<std::uint64_t>(n.id), 4));
            n.base = pick(rooms, spreadOverRooms ? i : 0);
            n.baseSpot = spreadOverRooms ? i / std::max(1, static_cast<int>(rooms.size())) : i;
            n.zone = i;
            pop.npcs.push_back(std::move(n));
        }
    };
    const int students = spec.students;
    addStaff(Role::Receptionist, 2, {plan.mainLobby}, false);
    addStaff(Role::Principal, 1, principal, false);
    addStaff(Role::Admin, std::clamp(students / 150, 2, 8), offices, true);
    addStaff(Role::Nurse, 1, nurse, false);
    addStaff(Role::Counselor, std::clamp(students / 400, 1, 3), counseling, true);
    addStaff(Role::ITStaff, students > 800 ? 2 : 1, servers, true);
    addStaff(Role::Security, students > 800 ? 3 : 2, security, false);
    addStaff(Role::Cook, std::clamp(students / 60, 3, 30), kitchens, false);
    addStaff(Role::Librarian, std::clamp(students / 500, 1, 4), libraries, false);
    addStaff(Role::Janitor, std::clamp(students / 120, 2, 14), janitors, false);
    addStaff(Role::Maintenance, 1, boilers, false);
    // Teachers get their work base in the staff room (free periods).
    const std::vector<RoomId> staffRooms = roomsOf(RoomType::StaffRoom);
    int teacherOrdinal = 0;
    for (const int id : pop.teachers) {
        Npc& n = pop.npcs[static_cast<std::size_t>(id)];
        n.base = pick(staffRooms, teacherOrdinal);
        n.baseSpot = teacherOrdinal;
        ++teacherOrdinal;
    }
    return pop;
}

std::vector<std::string> ValidateTimetable(const SchoolPlan& plan, const Population& pop)
{
    std::vector<std::string> errors;
    const auto fail = [&](std::string m) { errors.push_back(std::move(m)); };
    for (std::size_t i = 0; i < pop.npcs.size(); ++i)
        if (pop.npcs[i].id != static_cast<int>(i)) fail("npc ids are not sequential at index " + std::to_string(i));
    std::vector<int> classOfStudent(pop.npcs.size(), -1);
    for (const ClassGroup& g : pop.classes) {
        if (g.students.empty()) fail("class " + g.name + " has no students");
        if (g.home == kNone || g.home >= static_cast<int>(plan.rooms.size())) fail("class " + g.name + " has no valid home room");
        else if (plan.room(g.home).type != RoomType::Classroom) fail("class " + g.name + " home room is not a classroom");
        for (const int s : g.students) {
            if (s < 0 || s >= static_cast<int>(pop.npcs.size()) || pop.npcs[static_cast<std::size_t>(s)].role != Role::Student) {
                fail("class " + g.name + " lists a non-student");
                continue;
            }
            if (classOfStudent[static_cast<std::size_t>(s)] != -1) fail("student " + std::to_string(s) + " is in two classes");
            classOfStudent[static_cast<std::size_t>(s)] = g.id;
            if (pop.npcs[static_cast<std::size_t>(s)].classId != g.id) fail("student " + std::to_string(s) + " disagrees about its class");
        }
    }
    for (const Npc& n : pop.npcs)
        if (n.role == Role::Student && classOfStudent[static_cast<std::size_t>(n.id)] == -1) fail("student " + std::to_string(n.id) + " is in no class");
    // Double bookings.
    struct Key {
        int day, period;
        bool operator<(const Key& o) const { return day != o.day ? day < o.day : period < o.period; }
    };
    std::map<std::tuple<int, int, int>, int> teacherAt;               // (teacher, day, period) -> class
    std::map<std::tuple<int, int, int>, std::vector<int>> roomAt;     // (room, day, period) -> slots used
    std::vector<int> load(pop.npcs.size(), 0);
    for (const ClassGroup& g : pop.classes) {
        for (int d = 0; d < kSchoolDays; ++d) {
            for (int p = 0; p < pop.periods; ++p) {
                const Lesson& l = g.timetable[static_cast<std::size_t>(d)][static_cast<std::size_t>(p)];
                if (l.free) continue;
                const std::string where = "class " + g.name + " day " + std::to_string(d) + " period " + std::to_string(p + 1);
                if (l.room < 0 || l.room >= static_cast<int>(plan.rooms.size())) {
                    fail(where + ": lesson room does not exist");
                    continue;
                }
                const Room& room = plan.room(l.room);
                const RoomType want = SubjectRoomType(l.subject);
                const bool outdoorPE = l.subject == Subject::PE && (room.type == RoomType::FootballField || room.type == RoomType::Track);
                if (UsesHomeRoom(l.subject)) {
                    if (l.room != g.home) fail(where + ": " + SubjectName(l.subject) + " is not in the class's own room");
                } else if (room.type != want && !outdoorPE) {
                    fail(where + ": " + SubjectName(l.subject) + " scheduled in a " + TypeName(room.type));
                }
                if (l.teacher >= 0) {
                    if (l.teacher >= static_cast<int>(pop.npcs.size()) || pop.npcs[static_cast<std::size_t>(l.teacher)].role != Role::Teacher) {
                        fail(where + ": teacher is not a teacher");
                    } else {
                        if (pop.npcs[static_cast<std::size_t>(l.teacher)].subject != l.subject) fail(where + ": teacher does not teach " + SubjectName(l.subject));
                        const auto key = std::make_tuple(l.teacher, d, p);
                        if (teacherAt.count(key)) fail(where + ": teacher " + pop.npcs[static_cast<std::size_t>(l.teacher)].name + " is in two places");
                        teacherAt[key] = g.id;
                        ++load[static_cast<std::size_t>(l.teacher)];
                    }
                } else if (l.subject != Subject::Library) {
                    fail(where + ": lesson has no teacher");
                }
                std::vector<int>& slotsUsed = roomAt[{l.room, d, p}];
                if (std::find(slotsUsed.begin(), slotsUsed.end(), l.slot) != slotsUsed.end()) fail(where + ": room " + room.name + " slot " + std::to_string(l.slot) + " is double booked");
                slotsUsed.push_back(l.slot);
                const int cap = (room.type == RoomType::Gym || room.type == RoomType::Library) ? 3 : room.type == RoomType::FootballField ? 4 : room.type == RoomType::Track ? 3 : room.type == RoomType::DramaStudio || (room.type == RoomType::MusicRoom && room.capacity > 40) ? 2 : 1;
                if (l.slot >= cap && !UsesHomeRoom(l.subject)) fail(where + ": slot " + std::to_string(l.slot) + " exceeds the room's concurrency");
            }
        }
    }
    for (const Npc& n : pop.npcs)
        if (n.role == Role::Teacher && load[static_cast<std::size_t>(n.id)] > pop.periods * kSchoolDays) fail("teacher " + n.name + " teaches more lessons than there are periods");
    for (const auto& [key, slotsUsed] : roomAt) {
        const auto& [room, d, p] = key;
        (void)d;
        (void)p;
        if (slotsUsed.size() > 4) fail("room " + plan.room(room).name + " hosts more than four classes at once");
    }
    return errors;
}

}  // namespace School
