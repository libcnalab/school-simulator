#pragma once

// What is inside a room: furniture (fixtures) and the places people can occupy (spots). Both come from
// one generator so a chair and the seat the student sits on can never disagree. Rendering draws the
// fixtures, collision uses the solid ones, the simulation hands out the spots.
//
// Facing: yaw 0 looks north (-Z), positive yaw turns left (the same as the first-person camera);
// the forward vector is (-sin yaw, -cos yaw).

#include <cstdint>
#include <memory>
#include <vector>

#include "World/SchoolPlan.hpp"

namespace School {

enum class FixtureType : std::uint8_t {
    Desk,            // double student desk
    Chair,
    TeacherDesk,
    Whiteboard,      // on a wall
    Screen,          // projection screen / big display on a wall
    Shelf,           // bookcase, books are drawn on it
    Table,           // generic table (library, cafeteria, meeting)
    Bench,           // seat without a back (gym, changing room, cafeteria)
    ComputerDesk,    // desk with a monitor, keyboard and case
    LabBench,        // laboratory bench with taps and sink
    Stool,
    Locker,          // a run of lockers (size gives the run)
    Easel,
    Piano,
    Counter,         // serving counter, reception desk, kitchen counter
    Cabinet,
    Rack,            // server rack
    Plant,
    Hoop,            // basketball backboard and hoop
    Stage,           // raised platform
    TheatreSeats,    // a row of auditorium seats
    Sofa,
    Machine,         // workshop machinery
    Bed,
    Scoreboard,
    Vending,
    TreeTrunk,       // outdoor: tree (rendered with a crown)
    BikeRack,
    Goal,            // football goal
    Lamp,            // a floor or ceiling light that is drawn as a lit object
    Car,             // a parked car (nose towards local +z)
    Box              // anything else: a crate, a bin
};

struct Fixture {
    FixtureType type = FixtureType::Box;
    Vec2 pos;              // centre in plan
    float y0 = 0.0f;       // height of the underside above the floor
    float sx = 1.0f;       // extent along the fixture's own x axis
    float sy = 1.0f;       // height
    float sz = 1.0f;       // extent along its own z axis
    float yaw = 0.0f;      // rotation about the vertical axis (same convention as facing)
    std::uint8_t color = 0;   // palette index chosen by the generator, interpreted by rendering
    bool solid = true;     // blocks the player
};

enum class SpotKind : std::uint8_t { Seat, Teacher, Work, Stand, Queue, Browse, Equipment };
enum class Pose : std::uint8_t { Stand, Sit, SitWrite, SitType, Eat, Work, Browse, Exercise, Lecture, Sweep };

struct Spot {
    Vec2 pos;
    float yaw = 0.0f;
    SpotKind kind = SpotKind::Stand;
    Pose pose = Pose::Stand;
};

struct RoomLayout {
    RoomId room = kNone;
    std::vector<Fixture> fixtures;
    std::vector<Spot> seats;      // lesson places for students (sorted front to back)
    std::vector<Spot> teacher;    // where a teacher or supervisor stands or sits
    std::vector<Spot> work;       // desks for staff who work here
    std::vector<Spot> idle;       // places to loiter in a break (corridors, courtyard, cafeteria queue back...)
    std::vector<Spot> queue;      // serving queue, front of the queue first (cafeteria)
    std::vector<Spot> eat;        // seats where students eat (cafeteria)
    std::vector<Spot> browse;     // shelf browsing places (library)
    Side doorSide = Side::South;  // the wall of the main door
    Vec2 hub;                     // an aisle crossing near the door; paths to spots go through it

    /** Intermediate points from just inside the door (`entry`) to a spot, along the aisles. */
    [[nodiscard]] std::vector<Vec2> PathTo(Vec2 entry, const Spot& spot) const;
    /** And back out again towards an exit point. */
    [[nodiscard]] std::vector<Vec2> PathFrom(const Spot& spot, Vec2 exitPoint) const;

    /** Local aisle lines (u or v coordinates) used by PathTo, filled by the generator. */
    struct Aisles {
        bool valid = false;
        Vec2 origin;              // local origin in world
        Vec2 uAxis, vAxis;        // unit vectors in world
        float W = 0.0f, D = 0.0f;
        float sideLeft = 0.4f, sideRight = 0.0f;   // u coordinates of the two side aisles
        float frontV = 1.0f;      // v of the cross aisle nearest the door
    } aisles;
};

/** Generates the layout of a room. Deterministic: depends only on the plan and the room. */
RoomLayout BuildRoomLayout(const SchoolPlan& plan, RoomId room);

/** Lazy cache: a layout is only built the first time somebody needs it. */
class LayoutCache {
public:
    explicit LayoutCache(const SchoolPlan& plan) : plan_(plan), layouts_(plan.rooms.size()) {}
    const RoomLayout& Get(RoomId room) const
    {
        std::unique_ptr<RoomLayout>& slot = layouts_[static_cast<std::size_t>(room)];
        if (!slot) {
            slot = std::make_unique<RoomLayout>(BuildRoomLayout(plan_, room));
            ++built_;
        }
        return *slot;
    }
    [[nodiscard]] int Built() const { return built_; }

private:
    const SchoolPlan& plan_;
    mutable std::vector<std::unique_ptr<RoomLayout>> layouts_;
    mutable int built_ = 0;
};

}  // namespace School
