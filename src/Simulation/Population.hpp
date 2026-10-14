#pragma once

// Who is in the school and what they teach and learn: students in classes, teachers, staff, and the
// weekly timetable that sends every class to a room every lesson. All of it is generated from a seed
// and validated by ValidateTimetable().

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Simulation/Schedule.hpp"
#include "World/SchoolPlan.hpp"

namespace School {

enum class Role : std::uint8_t {
    Student, Teacher, Receptionist, Principal, Admin, Nurse, Counselor, ITStaff, Security, Cook, Librarian, Janitor, Maintenance
};
const char* RoleName(Role r);
inline bool IsStaff(Role r) { return r != Role::Student; }

enum class Subject : std::uint8_t {
    Math, Language, English, History, Geography, Physics, Chemistry, Biology, PE, Music, Art, IT, Technology, Drama, Library, Count
};
const char* SubjectName(Subject s);
/** Room type a subject needs; Classroom means "the class's own room". */
RoomType SubjectRoomType(Subject s);

constexpr int kMaxPeriods = 10;
constexpr int kSchoolDays = 5;   // Monday to Friday

struct Lesson {
    Subject subject = Subject::Library;
    int teacher = -1;           // npc id; -1 for self-study
    RoomId room = kNone;
    int slot = 0;               // which of the classes sharing this room in this period (0, 1, 2)
    bool free = true;           // no lesson was found: the class studies on its own in its home room
};

struct ClassGroup {
    int id = 0;
    int year = 7;               // school year
    std::string name;           // "9B"
    RoomId home = kNone;
    std::vector<int> students;  // npc ids
    int lunchWave = 0;
    std::array<std::array<Lesson, kMaxPeriods>, kSchoolDays> timetable;
};

struct Npc {
    int id = 0;
    Role role = Role::Student;
    std::string name;
    int classId = -1;           // students
    std::uint32_t look = 0;     // appearance seed
    Subject subject = Subject::Math;   // teachers: what they teach (the one they mainly teach)
    RoomId base = kNone;        // staff: where they work
    int baseSpot = 0;           // staff: which work spot of that room
    int zone = 0;               // janitors, guards: index of the patrol zone
    float speed = 1.3f;         // walking speed in m/s
};

enum class Preset { Empty, Light, Normal, Busy, Extreme };
const char* PresetName(Preset p);
Preset ParsePreset(const std::string& s, bool* ok = nullptr);

struct PopulationSpec {
    int students = 500;
    int periods = 7;
    float studentsPerClass = 25.0f;
    std::uint64_t seed = 1;
    static PopulationSpec FromPreset(Preset p, int periods = 7, std::uint64_t seed = 1);
};

struct Population {
    std::vector<Npc> npcs;
    std::vector<ClassGroup> classes;
    std::vector<int> teachers;      // npc ids
    /** teacherClass[teacherIndex][day][period] = class id or -1 (derived from the class timetables). */
    std::vector<std::array<std::array<int, kMaxPeriods>, kSchoolDays>> teacherClass;
    int periods = 7;

    [[nodiscard]] int StudentCount() const;
    [[nodiscard]] int StaffCount() const { return static_cast<int>(npcs.size()) - StudentCount(); }
    /** Index into `teachers` for a teacher npc id, or -1. */
    [[nodiscard]] int TeacherIndex(int npcId) const;
};

Population BuildPopulation(const SchoolPlan& plan, const PopulationSpec& spec);

/** Timetable and population problems: double-booked teachers or rooms, bad room references, empty classes... */
std::vector<std::string> ValidateTimetable(const SchoolPlan& plan, const Population& pop);

}  // namespace School
