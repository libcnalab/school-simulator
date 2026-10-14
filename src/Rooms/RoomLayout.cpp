#include "Rooms/RoomLayout.hpp"

#include <algorithm>
#include <cmath>

#include "World/Rng.hpp"

namespace School {

namespace {

constexpr float kPi = 3.14159265358979f;

/** Yaw that makes a body face along a world direction. */
float YawOf(Vec2 dir)
{
    return std::atan2(-dir.x, -dir.z);
}
/** Yaw that makes a fixture's local x axis run along a world direction. */
float YawOfX(Vec2 dir)
{
    return std::atan2(-dir.z, dir.x);
}

// Colour indices for fixtures. Rendering maps them to tints.
enum Color : std::uint8_t { kWood = 0, kLight, kDark, kBlue, kRed, kGreen, kYellow, kGrey, kWhite, kMetal };

/** Local frame of a room: u along the main door's wall, v from that wall into the room. */
struct Frame {
    Vec2 origin, u, v;
    float W = 0.0f, D = 0.0f;
    Side doorSide = Side::South;

    [[nodiscard]] Vec2 At(float uu, float vv) const { return origin + u * uu + v * vv; }
    [[nodiscard]] Vec2 Dir(float du, float dv) const { return u * du + v * dv; }
    [[nodiscard]] Vec2 ToLocal(Vec2 p) const
    {
        const Vec2 d = p - origin;
        return {Dot(d, u), Dot(d, v)};
    }
};

Frame MakeFrame(const SchoolPlan& plan, const Room& room)
{
    Side side = Side::South;
    for (const DoorId id : room.doors) {
        const Door& d = plan.door(id);
        if (d.kind == DoorKind::Opening) continue;
        side = d.a == room.id ? d.sideOfA : Opposite(d.sideOfA);
        break;
    }
    const Rect& r = room.rect;
    Frame f;
    f.doorSide = side;
    switch (side) {
    case Side::South: f.origin = {r.x0, r.z1}; f.v = {0, -1}; f.W = r.Width(); f.D = r.Depth(); break;
    case Side::North: f.origin = {r.x1, r.z0}; f.v = {0, 1}; f.W = r.Width(); f.D = r.Depth(); break;
    case Side::East: f.origin = {r.x1, r.z1}; f.v = {-1, 0}; f.W = r.Depth(); f.D = r.Width(); break;
    case Side::West: f.origin = {r.x0, r.z0}; f.v = {1, 0}; f.W = r.Depth(); f.D = r.Width(); break;
    }
    f.u = f.v.RightHand();
    return f;
}

class Gen {
public:
    Gen(const SchoolPlan& plan, const Room& room) : plan_(plan), room_(room), f_(MakeFrame(plan, room)), rng_(Hash(static_cast<std::uint64_t>(room.id), 0xC0FFEE))
    {
        out.room = room.id;
        out.doorSide = f_.doorSide;
        out.hub = f_.At(f_.W * 0.5f, 1.0f);
    }

    RoomLayout out;

    void Run();

private:
    // --- primitives ---------------------------------------------------------------------------------------
    /** Adds a fixture at local (u, v); `faceDv` = +1 faces the far wall, -1 the door wall, 0 uses `faceDu`. */
    void Fix(FixtureType t, float u, float v, float sx, float sy, float sz, float yaw, Color c = kWood, bool solid = true, float y0 = 0.0f)
    {
        Fixture fx;
        fx.type = t;
        fx.pos = f_.At(u, v);
        fx.sx = sx;
        fx.sy = sy;
        fx.sz = sz;
        fx.yaw = yaw;
        fx.color = c;
        fx.solid = solid;
        fx.y0 = y0;
        out.fixtures.push_back(fx);
    }
    float FarYaw() const { return YawOf(f_.v); }       // facing the far wall
    float DoorYaw() const { return YawOf(f_.v * -1.0f); }   // facing the door wall
    float YawLocal(float du, float dv) const { return YawOf(f_.Dir(du, dv)); }
    // Fixture whose long side runs along u (a desk row, a counter): local x -> u.
    float AlongU() const { return YawOfX(f_.u); }
    float AlongV() const { return YawOfX(f_.v); }

    Spot MakeSpot(float u, float v, float yaw, SpotKind kind, Pose pose) const { return {f_.At(u, v), yaw, kind, pose}; }

    void SetAisles(float sideL, float sideR, float frontV)
    {
        out.aisles.valid = true;
        out.aisles.origin = f_.origin;
        out.aisles.uAxis = f_.u;
        out.aisles.vAxis = f_.v;
        out.aisles.W = f_.W;
        out.aisles.D = f_.D;
        out.aisles.sideLeft = sideL;
        out.aisles.sideRight = sideR;
        out.aisles.frontV = frontV;
    }

    void WallBoard(float width, Color c = kWhite)
    {
        Fix(FixtureType::Whiteboard, f_.W * 0.5f, f_.D - 0.04f, width, 1.25f, 0.05f, YawLocal(0, -1), c, false, 0.85f);
    }

    // --- room families -------------------------------------------------------------------------------------
    void GridClass(bool computers);
    void Lab();
    void Art();
    void Music();
    void Workshop();
    void Gym();
    void Library();
    void Auditorium();
    void Cafeteria();
    void Kitchen();
    void OfficeLike(int deskCount);
    void StaffRoom();
    void MeetingRoom();
    void Nurse();
    void Counseling();
    void ServerRoom();
    void Security();
    void StorageLike();
    void Toilets();
    void ChangingRoom();
    void Lockers();
    void Drama();
    void Idle(float spacing, float margin);
    void CorridorIdle();
    void Outdoor();

    const SchoolPlan& plan_;
    const Room& room_;
    Frame f_;
    Rng rng_;
};

void Gen::GridClass(bool)
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.45f, W - 0.45f, 1.0f);
    WallBoard(std::min(W - 1.0f, 4.4f));
    // The projector hangs in the middle of the room, aimed at the board.
    Fix(FixtureType::Box, W * 0.5f, D * 0.55f, 0.35f, 0.12f, 0.3f, FarYaw(), kGrey, false, 2.9f);
    // Teacher's desk at the front, to one side of the board.
    const float deskU = W * 0.78f;
    Fix(FixtureType::TeacherDesk, deskU, D - 1.5f, 1.5f, 0.76f, 0.75f, DoorYaw(), kDark);
    Fix(FixtureType::Chair, deskU, D - 0.8f, 0.45f, 0.9f, 0.45f, DoorYaw(), kDark, false);
    out.work.push_back(MakeSpot(deskU, D - 0.95f, DoorYaw(), SpotKind::Work, Pose::Work));
    out.teacher.push_back(MakeSpot(W * 0.30f, D - 1.35f, DoorYaw(), SpotKind::Teacher, Pose::Lecture));
    out.teacher.push_back(MakeSpot(deskU - 1.2f, D - 1.35f, DoorYaw(), SpotKind::Teacher, Pose::Stand));
    // Cupboard and a plant beside the door end.
    Fix(FixtureType::Cabinet, 0.3f, D - 2.5f, 0.5f, 1.8f, 1.6f, AlongV() + kPi * 0.5f, kLight);
    Fix(FixtureType::Plant, W - 0.45f, 0.6f, 0.4f, 0.9f, 0.4f, 0.0f, kGreen, false);
    // Desks: pairs in columns, rows from the board back towards the door.
    const int cols = std::clamp(static_cast<int>((W - 1.4f) / 1.8f), 1, 5);
    const int rows = std::clamp(static_cast<int>((D - 2.7f - 1.8f) / 1.15f) + 1, 1, 8);
    const float margin = 1.0f;
    const int wanted = room_.capacity;
    int placed = 0;
    for (int r = 0; r < rows && placed < wanted; ++r) {
        const float v = D - 2.7f - 1.15f * static_cast<float>(r);
        for (int c = 0; c < cols && placed < wanted; ++c) {
            const float uc = margin + (W - 2.0f * margin) * (static_cast<float>(c) + 0.5f) / static_cast<float>(cols);
            Fix(FixtureType::Desk, uc, v, 1.3f, 0.74f, 0.55f, AlongU() , kLight);
            for (int s = 0; s < 2 && placed < wanted; ++s) {
                const float us = uc + (s == 0 ? -0.33f : 0.33f);
                Fix(FixtureType::Chair, us, v - 0.55f, 0.42f, 0.88f, 0.42f, FarYaw(), kBlue, false);
                out.seats.push_back(MakeSpot(us, v - 0.55f, FarYaw(), SpotKind::Seat, Pose::SitWrite));
                ++placed;
            }
        }
    }
}

void Gen::Lab()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.5f, W - 0.5f, 1.0f);
    WallBoard(std::min(W - 1.5f, 5.0f));
    // Demonstration bench at the front.
    Fix(FixtureType::LabBench, W * 0.5f, D - 1.4f, std::min(3.2f, W - 2.0f), 0.92f, 0.9f, AlongU(), kDark);
    out.teacher.push_back(MakeSpot(W * 0.5f, D - 0.75f, DoorYaw(), SpotKind::Teacher, Pose::Lecture));
    out.work.push_back(MakeSpot(W * 0.5f - 1.8f, D - 1.0f, DoorYaw(), SpotKind::Work, Pose::Work));
    // Two columns of benches; stools on both long sides.
    const int benchRows = std::clamp(static_cast<int>((D - 4.2f) / 1.9f) + 1, 1, 4);
    const int cols = W >= 10.0f ? 3 : 2;
    int placed = 0;
    for (int r = 0; r < benchRows; ++r) {
        const float v = D - 3.4f - 1.9f * static_cast<float>(r);
        for (int c = 0; c < cols; ++c) {
            const float u = 1.4f + (W - 2.8f) * (static_cast<float>(c) + 0.5f) / static_cast<float>(cols) - 0.0f;
            Fix(FixtureType::LabBench, u, v, std::min(2.6f, (W - 2.0f) / static_cast<float>(cols)), 0.9f, 0.9f, AlongU(), kMetal);
            for (int s = 0; s < 4 && placed < room_.capacity; ++s) {
                const float us = u + (s % 2 == 0 ? -0.5f : 0.5f);
                const float vs = v + (s < 2 ? -0.75f : 0.75f);
                Fix(FixtureType::Stool, us, vs, 0.35f, 0.62f, 0.35f, 0.0f, kRed, false);
                out.seats.push_back(MakeSpot(us, vs, YawLocal(0, s < 2 ? 1.0f : -1.0f), SpotKind::Seat, Pose::SitWrite));
                ++placed;
            }
        }
    }
    // Wall cupboards and a fume cupboard.
    Fix(FixtureType::Cabinet, 0.3f, D * 0.5f, 0.5f, 2.0f, 2.4f, AlongV() + kPi * 0.5f, kLight);
}

void Gen::Art()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.5f, W - 0.5f, 1.0f);
    WallBoard(std::min(W - 2.0f, 4.0f));
    out.teacher.push_back(MakeSpot(W * 0.5f, D - 1.3f, DoorYaw(), SpotKind::Teacher, Pose::Lecture));
    out.work.push_back(MakeSpot(W - 1.2f, D - 1.0f, DoorYaw(), SpotKind::Work, Pose::Work));
    Fix(FixtureType::TeacherDesk, W - 1.2f, D - 1.6f, 1.4f, 0.76f, 0.7f, DoorYaw(), kDark);
    // Easels in staggered rows, all facing a still-life table in the middle of the room.
    Fix(FixtureType::Table, W * 0.5f, D * 0.5f, 1.2f, 0.9f, 1.2f, 0.0f, kLight);
    const int cols = std::clamp(static_cast<int>((W - 1.5f) / 1.4f), 2, 7);
    int placed = 0;
    for (int ring = 0; ring < 3 && placed < room_.capacity; ++ring) {
        for (int c = 0; c < cols && placed < room_.capacity; ++c) {
            const float u = 0.9f + (W - 1.8f) * (static_cast<float>(c) + 0.5f) / static_cast<float>(cols);
            const float v = 1.9f + 1.7f * static_cast<float>(ring) + ((c % 2) != 0 ? 0.0f : 0.6f);
            if (v > D - 2.0f) continue;
            // Skip the still-life table.
            if (std::fabs(u - W * 0.5f) < 0.9f && std::fabs(v - D * 0.5f) < 0.9f) continue;
            const Vec2 toCentre = Vec2{W * 0.5f - u, D * 0.5f - v}.Normalized();
            Fix(FixtureType::Easel, u, v, 0.6f, 1.6f, 0.6f, YawLocal(toCentre.x, toCentre.z), kWood);
            Fix(FixtureType::Stool, u - toCentre.x * 0.7f, v - toCentre.z * 0.7f, 0.33f, 0.6f, 0.33f, 0.0f, kYellow, false);
            out.seats.push_back(MakeSpot(u - toCentre.x * 0.7f, v - toCentre.z * 0.7f, YawLocal(toCentre.x, toCentre.z), SpotKind::Seat, Pose::SitWrite));
            ++placed;
        }
    }
    Fix(FixtureType::Counter, W * 0.5f, 0.35f, std::min(W - 3.0f, 4.0f), 0.9f, 0.5f, AlongU(), kLight);
}

void Gen::Music()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.5f, W - 0.5f, 1.0f);
    WallBoard(std::min(W - 1.5f, 4.0f), kWhite);
    Fix(FixtureType::Piano, W * 0.25f, D - 1.2f, 1.5f, 1.1f, 0.65f, DoorYaw(), kDark);
    out.teacher.push_back(MakeSpot(W * 0.5f, D - 1.4f, DoorYaw(), SpotKind::Teacher, Pose::Lecture));
    out.work.push_back(MakeSpot(W * 0.25f, D - 1.8f, DoorYaw(), SpotKind::Work, Pose::Work));
    // Chairs in arcs facing the front.
    int placed = 0;
    for (int arc = 0; arc < 5 && placed < room_.capacity; ++arc) {
        const float v = D - 3.0f - 1.0f * static_cast<float>(arc);
        if (v < 1.6f) break;
        const int n = std::max(2, static_cast<int>((W - 1.0f) / 0.85f));
        for (int i = 0; i < n && placed < room_.capacity; ++i) {
            const float u = 0.5f + (W - 1.0f) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
            const float curve = -0.25f * std::cos((u / W - 0.5f) * kPi);   // slight arc
            Fix(FixtureType::Chair, u, v + curve, 0.42f, 0.88f, 0.42f, FarYaw(), kRed, false);
            out.seats.push_back(MakeSpot(u, v + curve, FarYaw(), SpotKind::Seat, Pose::Sit));
            ++placed;
        }
    }
}

void Gen::Workshop()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.7f, W - 0.7f, 1.4f);
    out.teacher.push_back(MakeSpot(W * 0.5f, D - 1.4f, DoorYaw(), SpotKind::Teacher, Pose::Lecture));
    out.work.push_back(MakeSpot(1.2f, D - 1.2f, DoorYaw(), SpotKind::Work, Pose::Work));
    Fix(FixtureType::TeacherDesk, 1.6f, D - 0.9f, 1.4f, 0.8f, 0.7f, DoorYaw(), kDark);
    WallBoard(std::min(W - 3.0f, 3.5f));
    // Benches in rows, machines along the far and side walls.
    const int benchCols = std::max(2, static_cast<int>((W - 3.0f) / 3.2f));
    int placed = 0;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < benchCols; ++c) {
            const float u = 1.8f + (W - 3.6f) * (static_cast<float>(c) + 0.5f) / static_cast<float>(benchCols);
            const float v = 3.2f + 2.6f * static_cast<float>(r);
            if (v > D - 3.2f) continue;
            Fix(FixtureType::Table, u, v, 2.2f, 0.92f, 0.9f, AlongU(), kMetal);
            for (int s = 0; s < 3 && placed < room_.capacity; ++s) {
                const float us = u + (static_cast<float>(s) - 1.0f) * 0.7f;
                out.seats.push_back(MakeSpot(us, v - 0.75f, FarYaw(), SpotKind::Seat, Pose::Work));
                ++placed;
            }
        }
    }
    for (int i = 0; i < 4; ++i)
        if (3.0f + 2.4f * static_cast<float>(i) < D - 1.0f) Fix(FixtureType::Machine, 0.6f, 3.0f + 2.4f * static_cast<float>(i), 0.9f, 1.3f, 1.2f, AlongV(), kRed);
    for (int i = 0; i < 3; ++i)
        if (3.5f + 3.0f * static_cast<float>(i) < D - 1.0f) Fix(FixtureType::Machine, W - 0.7f, 3.5f + 3.0f * static_cast<float>(i), 1.0f, 1.2f, 1.4f, AlongV(), kGreen);
}

void Gen::Gym()
{
    const float W = f_.W, D = f_.D;
    SetAisles(1.0f, W - 1.0f, 2.0f);
    // The court runs along the long axis of the hall; two hoops per court end.
    const bool courtAlongU = W >= D;
    const float cl = std::min(28.0f, std::max(W, D) - 6.0f);
    const float cw = std::min(15.0f, std::min(W, D) - 6.0f);
    const float cu = W * 0.5f, cv = D * 0.5f;
    const float halfU = courtAlongU ? cl * 0.5f : cw * 0.5f;
    const float halfV = courtAlongU ? cw * 0.5f : cl * 0.5f;
    for (int end = -1; end <= 1; end += 2) {
        const float hu = courtAlongU ? cu + static_cast<float>(end) * halfU : cu;
        const float hv = courtAlongU ? cv : cv + static_cast<float>(end) * halfV;
        Fix(FixtureType::Hoop, hu, hv, 1.8f, 3.6f, 0.2f, courtAlongU ? YawLocal(-static_cast<float>(end), 0) : YawLocal(0, -static_cast<float>(end)), kWhite, false);
    }
    Fix(FixtureType::Scoreboard, W * 0.5f, D - 0.2f, 4.0f, 1.2f, 0.2f, DoorYaw(), kDark, false, 4.0f);
    // Benches along the long walls.
    for (int i = 0; i < 4; ++i) Fix(FixtureType::Bench, 3.0f + (W - 6.0f) * (static_cast<float>(i) + 0.5f) / 4.0f, 0.45f, 2.4f, 0.45f, 0.4f, AlongU(), kBlue);
    for (int i = 0; i < 3; ++i) Fix(FixtureType::Bench, W - 0.4f, 6.0f + (D - 12.0f) * (static_cast<float>(i) + 0.5f) / 3.0f, 2.4f, 0.45f, 0.4f, AlongV(), kBlue);
    out.teacher.push_back(MakeSpot(cu, cv, DoorYaw(), SpotKind::Teacher, Pose::Stand));
    out.teacher.push_back(MakeSpot(cu - 3.0f, cv - 2.0f, DoorYaw(), SpotKind::Teacher, Pose::Stand));
    // Exercise places: a grid across the hall.
    const int nu = std::max(2, static_cast<int>((W - 6.0f) / 3.0f)), nv = std::max(2, static_cast<int>((D - 6.0f) / 3.0f));
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const float u = 3.0f + (W - 6.0f) * (static_cast<float>(i) + 0.5f) / static_cast<float>(nu);
            const float v = 3.0f + (D - 6.0f) * (static_cast<float>(j) + 0.5f) / static_cast<float>(nv);
            out.seats.push_back(MakeSpot(u, v, YawLocal(0, 1), SpotKind::Seat, Pose::Exercise));
        }
    if (static_cast<int>(out.seats.size()) > room_.capacity) out.seats.resize(static_cast<std::size_t>(room_.capacity));
    // Idle places on the benches for those not playing.
    for (int i = 0; i < 4; ++i) out.idle.push_back(MakeSpot(3.0f + (W - 6.0f) * (static_cast<float>(i) + 0.5f) / 4.0f, 0.9f, DoorYaw(), SpotKind::Stand, Pose::Sit));
}

void Gen::Library()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.8f, W - 0.8f, 2.0f);
    // Librarian's desk by the door.
    Fix(FixtureType::Counter, 6.0f, 2.2f, 4.0f, 1.0f, 0.8f, AlongU(), kDark);
    out.work.push_back(MakeSpot(6.0f, 3.0f, DoorYaw(), SpotKind::Work, Pose::Work));
    out.work.push_back(MakeSpot(8.0f, 3.0f, DoorYaw(), SpotKind::Work, Pose::Work));
    out.teacher.push_back(MakeSpot(10.0f, 3.5f, DoorYaw(), SpotKind::Teacher, Pose::Stand));
    // Shelf rows in the far half, running along v; browse places in the aisles between.
    const float shelfStart = D * 0.50f;
    const int shelfCols = std::max(2, static_cast<int>((W - 4.0f) / 3.6f));
    for (int c = 0; c < shelfCols; ++c) {
        const float u = 2.5f + (W - 5.0f) * (static_cast<float>(c) + 0.5f) / static_cast<float>(shelfCols);
        for (float v = shelfStart + 1.0f; v < D - 1.2f; v += 2.2f) {
            Fix(FixtureType::Shelf, u, v + 1.1f, 1.0f, 2.1f, 2.0f, AlongV(), kWood);
        }
        // browse places beside the shelf, in the aisle
        for (float v = shelfStart + 2.0f; v < D - 1.5f; v += 2.6f) {
            out.browse.push_back(MakeSpot(u + 1.0f, v, YawLocal(-1, 0), SpotKind::Browse, Pose::Browse));
        }
    }
    // Wall shelves along the long walls.
    for (float u = 2.0f; u < W - 2.0f; u += 2.0f) Fix(FixtureType::Shelf, u, D - 0.3f, 2.0f, 2.2f, 0.4f, AlongU(), kWood);
    // Reading tables in the near half: tables of six.
    int placed = 0;
    const int tableCols = std::max(2, static_cast<int>((W - 2.0f) / 4.4f));
    for (float v = 6.0f; v < shelfStart - 0.5f; v += 3.2f) {
        for (int c = 0; c < tableCols; ++c) {
            const float u = 2.4f + (W - 4.8f) * (static_cast<float>(c) + 0.5f) / static_cast<float>(tableCols);
            Fix(FixtureType::Table, u, v, 1.8f, 0.75f, 1.0f, AlongU(), kLight);
            for (int s = 0; s < 6; ++s) {
                const bool top = s < 3;
                const float us = u + (static_cast<float>(s % 3) - 1.0f) * 0.6f;
                const float vs = v + (top ? 0.75f : -0.75f);
                Fix(FixtureType::Chair, us, vs, 0.42f, 0.88f, 0.42f, YawLocal(0, top ? -1.0f : 1.0f), kGreen, false);
                if (placed < room_.capacity) out.seats.push_back(MakeSpot(us, vs, YawLocal(0, top ? -1.0f : 1.0f), SpotKind::Seat, Pose::SitWrite));
                ++placed;
            }
        }
    }
    // Reading corner with armchairs and a plant.
    Fix(FixtureType::Sofa, W - 2.0f, 4.0f, 2.2f, 0.8f, 0.9f, YawLocal(-1, 0) , kRed);
    Fix(FixtureType::Plant, W - 0.6f, 1.2f, 0.5f, 1.2f, 0.5f, 0.0f, kGreen, false);
}

void Gen::Auditorium()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.8f, W - 0.8f, 1.5f);
    // Stage along the far wall; a projection screen above it.
    const float stageDepth = std::min(7.0f, D * 0.2f);
    Fix(FixtureType::Stage, W * 0.5f, D - stageDepth * 0.5f, W - 1.0f, 1.0f, stageDepth, AlongU(), kDark);
    Fix(FixtureType::Screen, W * 0.5f, D - 0.1f, std::min(12.0f, W - 6.0f), 5.0f, 0.1f, YawLocal(0, -1), kWhite, false, 3.5f);
    out.teacher.push_back(MakeSpot(W * 0.5f, D - stageDepth * 0.5f, DoorYaw(), SpotKind::Teacher, Pose::Lecture));
    out.teacher.back().pos = f_.At(W * 0.5f, D - stageDepth * 0.5f);
    // Rows of seats with a central aisle, facing the stage; seat spots are on the floor in front of each seat.
    const float rowStart = 3.0f;
    const float rowEnd = D - stageDepth - 2.0f;
    const int perSide = std::max(3, static_cast<int>((W - 2.4f) * 0.5f / 0.55f));
    int rowIndex = 0;
    int placed = 0;
    for (float v = rowStart; v < rowEnd; v += 0.95f, ++rowIndex) {
        for (int side = 0; side < 2; ++side) {
            const float u0 = side == 0 ? 1.0f : W * 0.5f + 0.9f;
            const float u1 = side == 0 ? W * 0.5f - 0.9f : W - 1.0f;
            Fix(FixtureType::TheatreSeats, (u0 + u1) * 0.5f, v, u1 - u0, 1.0f, 0.55f, AlongU(), kRed, true);
            const int n = std::max(1, static_cast<int>((u1 - u0) / 0.55f));
            for (int i = 0; i < n; ++i) {
                if (placed >= room_.capacity) break;
                const float u = u0 + (u1 - u0) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
                out.seats.push_back(MakeSpot(u, v, FarYaw(), SpotKind::Seat, Pose::Sit));
                ++placed;
            }
        }
    }
    (void)perSide;
}

void Gen::Cafeteria()
{
    const float W = f_.W, D = f_.D;
    SetAisles(1.0f, W - 1.0f, 2.0f);
    // The serving counter lies along the far wall (the kitchen is behind it); the queue forms in front of it.
    // The local frame's far wall is the one opposite the main door. Find the wall that touches the kitchen.
    float counterV = D - 0.8f;
    out.hub = f_.At(W * 0.5f, 2.0f);
    // Serving counter spans most of the far wall.
    Fix(FixtureType::Counter, W * 0.5f, counterV, W - 6.0f, 1.0f, 0.9f, AlongU(), kMetal);
    for (int i = 0; i < 8; ++i)
        out.work.push_back(MakeSpot(3.0f + (W - 6.0f) * (static_cast<float>(i) + 0.5f) / 8.0f, D - 0.1f, DoorYaw(), SpotKind::Work, Pose::Work));
    // Queue lanes: four lanes of slots leading away from the counter.
    const int lanes = 4;
    for (int slot = 0; slot < 8; ++slot)
        for (int l = 0; l < lanes; ++l) {
            const float u = 3.0f + (W - 6.0f) * (static_cast<float>(l) + 0.5f) / static_cast<float>(lanes);
            const float v = counterV - 1.0f - 0.7f * static_cast<float>(slot);
            out.queue.push_back(MakeSpot(u, v, DoorYaw() + kPi, SpotKind::Queue, Pose::Stand));
        }
    // Tables with benches in rows; the area in front of the counter is kept clear for the queue.
    const float tableFrom = 2.5f;
    const float tableTo = counterV - 1.0f - 0.7f * 8.0f - 1.0f;
    const int cols = std::max(2, static_cast<int>((W - 2.0f) / 3.4f));
    for (float v = tableFrom; v < tableTo; v += 2.4f) {
        for (int c = 0; c < cols; ++c) {
            const float u = 1.8f + (W - 3.6f) * (static_cast<float>(c) + 0.5f) / static_cast<float>(cols);
            Fix(FixtureType::Table, u, v, 2.6f, 0.75f, 0.8f, AlongU(), kLight);
            for (int side = 0; side < 2; ++side) {
                const float vs = v + (side == 0 ? -0.65f : 0.65f);
                Fix(FixtureType::Bench, u, vs, 2.6f, 0.45f, 0.32f, AlongU(), kBlue, false);
                for (int s = 0; s < 4; ++s) {
                    const float us = u + (static_cast<float>(s) - 1.5f) * 0.62f;
                    out.eat.push_back(MakeSpot(us, vs, YawLocal(0, side == 0 ? 1.0f : -1.0f), SpotKind::Seat, Pose::Eat));
                }
            }
        }
    }
    out.seats = out.eat;
    // The room is full of voices: idle places at the room's edge (people waiting for friends).
    for (float u = 1.5f; u < W - 1.0f; u += 2.5f) out.idle.push_back(MakeSpot(u, 0.8f, DoorYaw(), SpotKind::Stand, Pose::Stand));
    Fix(FixtureType::Vending, W - 0.5f, 1.0f, 0.9f, 1.9f, 0.8f, YawLocal(-1, 0), kRed);
}

void Gen::Kitchen()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.7f, W - 0.7f, 1.2f);
    for (int i = 0; i < 6; ++i) Fix(FixtureType::Counter, 2.0f + (W - 4.0f) * (static_cast<float>(i) + 0.5f) / 6.0f, 0.5f, 3.0f, 0.95f, 0.7f, AlongU(), kMetal);
    Fix(FixtureType::Table, W * 0.5f, D * 0.5f + 0.5f, std::min(W - 8.0f, 8.0f), 0.92f, 1.2f, AlongU(), kMetal);
    for (int i = 0; i < 8; ++i) {
        const float u = 3.0f + (W - 6.0f) * (static_cast<float>(i) + 0.5f) / 8.0f;
        out.work.push_back(MakeSpot(u, D * 0.5f - 0.9f, YawLocal(0, 1), SpotKind::Work, Pose::Work));
    }
    for (int i = 0; i < 4; ++i) out.work.push_back(MakeSpot(3.0f + (W - 6.0f) * (static_cast<float>(i) + 0.5f) / 4.0f, 1.4f, YawLocal(0, -1), SpotKind::Work, Pose::Work));
}

void Gen::OfficeLike(int deskCount)
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.45f, W - 0.45f, 1.0f);
    deskCount = std::clamp(deskCount, 1, 6);
    for (int i = 0; i < deskCount; ++i) {
        const float u = W * (static_cast<float>(i) + 0.5f) / static_cast<float>(deskCount);
        const float v = D - 1.4f;
        Fix(FixtureType::ComputerDesk, u, v, std::min(1.6f, W / static_cast<float>(deskCount) - 0.3f), 0.76f, 0.75f, DoorYaw(), kLight);
        Fix(FixtureType::Chair, u, v - 0.8f, 0.45f, 0.9f, 0.45f, FarYaw(), kDark, false);
        out.work.push_back(MakeSpot(u, v - 0.8f, FarYaw(), SpotKind::Work, Pose::SitType));
    }
    Fix(FixtureType::Cabinet, 0.3f, D * 0.5f, 0.5f, 1.8f, 1.4f, AlongV() + kPi * 0.5f, kGrey);
    out.teacher.push_back(MakeSpot(W * 0.5f, D * 0.5f, DoorYaw(), SpotKind::Teacher, Pose::Stand));
}

void Gen::StaffRoom()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.6f, W - 0.6f, 1.2f);
    const int tableCols = std::max(1, static_cast<int>(W / 4.5f));
    for (float v = 2.5f; v < D - 3.0f; v += 2.6f)
        for (int c = 0; c < tableCols; ++c) {
            const float u = W * (static_cast<float>(c) + 0.5f) / static_cast<float>(tableCols);
            Fix(FixtureType::Table, u, v, 1.8f, 0.75f, 0.9f, AlongU(), kLight);
            for (int s = 0; s < 4; ++s) {
                const float us = u + ((s % 2) == 0 ? -0.5f : 0.5f);
                const float vs = v + (s < 2 ? -0.7f : 0.7f);
                Fix(FixtureType::Chair, us, vs, 0.45f, 0.9f, 0.45f, YawLocal(0, s < 2 ? 1.0f : -1.0f), kGreen, false);
                out.work.push_back(MakeSpot(us, vs, YawLocal(0, s < 2 ? 1.0f : -1.0f), SpotKind::Work, Pose::SitWrite));
            }
        }
    Fix(FixtureType::Counter, W * 0.5f, D - 0.4f, std::min(W - 2.0f, 4.0f), 0.95f, 0.6f, AlongU(), kMetal);   // coffee corner
    Fix(FixtureType::Sofa, 1.2f, D - 1.8f, 2.0f, 0.8f, 0.9f, AlongV() + kPi, kBlue);
    Fix(FixtureType::Plant, W - 0.5f, D - 0.5f, 0.5f, 1.3f, 0.5f, 0.0f, kGreen, false);
    for (int i = 0; i < 4; ++i) out.idle.push_back(MakeSpot(W * 0.5f - 1.5f + static_cast<float>(i), D - 1.1f, YawLocal(0, 1), SpotKind::Stand, Pose::Stand));
    out.teacher = out.work;
}

void Gen::MeetingRoom()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.5f, W - 0.5f, 1.0f);
    const float tw = std::max(2.0f, W - 2.4f), td = std::max(1.5f, D - 3.0f);
    Fix(FixtureType::Table, W * 0.5f, D * 0.5f + 0.4f, tw, 0.76f, td, AlongU(), kDark);
    const int n = std::max(2, static_cast<int>(tw / 0.8f));
    for (int i = 0; i < n; ++i) {
        const float u = W * 0.5f - tw * 0.5f + tw * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        for (int side = 0; side < 2; ++side) {
            const float v = D * 0.5f + 0.4f + (side == 0 ? -(td * 0.5f + 0.4f) : (td * 0.5f + 0.4f));
            Fix(FixtureType::Chair, u, v, 0.45f, 0.9f, 0.45f, YawLocal(0, side == 0 ? 1.0f : -1.0f), kDark, false);
            out.work.push_back(MakeSpot(u, v, YawLocal(0, side == 0 ? 1.0f : -1.0f), SpotKind::Work, Pose::SitWrite));
        }
    }
    WallBoard(std::min(W - 1.5f, 3.2f));
    out.teacher = out.work;
}

void Gen::Nurse()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.45f, W - 0.45f, 1.0f);
    Fix(FixtureType::Bed, 0.8f, D - 1.2f, 0.9f, 0.55f, 2.0f, AlongV(), kWhite);
    Fix(FixtureType::ComputerDesk, W - 1.0f, D - 0.9f, 1.4f, 0.76f, 0.7f, DoorYaw(), kLight);
    Fix(FixtureType::Cabinet, W * 0.5f, 0.3f, 1.2f, 1.9f, 0.5f, AlongU(), kWhite);
    out.work.push_back(MakeSpot(W - 1.0f, D - 1.7f, FarYaw(), SpotKind::Work, Pose::SitType));
    out.teacher.push_back(MakeSpot(W * 0.5f, D * 0.5f, DoorYaw(), SpotKind::Teacher, Pose::Stand));
}

void Gen::Counseling()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.45f, W - 0.45f, 1.0f);
    Fix(FixtureType::ComputerDesk, W * 0.5f, D - 1.0f, 1.5f, 0.76f, 0.75f, DoorYaw(), kDark);
    Fix(FixtureType::Sofa, 1.2f, D * 0.45f, 1.8f, 0.8f, 0.9f, AlongV(), kGreen);
    Fix(FixtureType::Plant, W - 0.5f, 0.6f, 0.5f, 1.3f, 0.5f, 0.0f, kGreen, false);
    out.work.push_back(MakeSpot(W * 0.5f, D - 1.8f, FarYaw(), SpotKind::Work, Pose::SitType));
    out.teacher.push_back(MakeSpot(W * 0.5f, D * 0.5f, DoorYaw(), SpotKind::Teacher, Pose::Stand));
}

void Gen::ServerRoom()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.5f, W - 0.5f, 1.2f);
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < std::max(2, static_cast<int>((W - 2.0f) / 0.7f)); ++i) {
            const float u = 1.0f + 0.7f * static_cast<float>(i);
            if (u > W - 0.8f) break;
            // Two rows facing each other across a cold aisle.
            Fix(FixtureType::Rack, u, 3.0f + 2.4f * static_cast<float>(row), 0.6f, 2.1f, 1.0f, AlongU() + (row == 0 ? 0.0f : kPi), kDark);
        }
    Fix(FixtureType::ComputerDesk, W - 1.0f, D - 0.7f, 1.4f, 0.76f, 0.7f, DoorYaw(), kLight);
    out.work.push_back(MakeSpot(W - 1.0f, D - 1.4f, FarYaw(), SpotKind::Work, Pose::SitType));
    out.teacher.push_back(MakeSpot(W * 0.5f, 1.8f, FarYaw(), SpotKind::Teacher, Pose::Stand));
}

void Gen::Security()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.45f, W - 0.45f, 1.0f);
    Fix(FixtureType::ComputerDesk, W * 0.5f, D - 0.9f, std::min(W - 0.6f, 3.4f), 0.76f, 0.9f, DoorYaw(), kDark);
    Fix(FixtureType::Screen, W * 0.5f, D - 0.1f, std::min(W - 0.6f, 3.6f), 1.6f, 0.1f, YawLocal(0, -1), kDark, false, 1.3f);   // the CCTV wall
    out.work.push_back(MakeSpot(W * 0.5f, D - 1.8f, FarYaw(), SpotKind::Work, Pose::SitType));
    out.teacher.push_back(MakeSpot(W * 0.5f, D * 0.5f, DoorYaw(), SpotKind::Teacher, Pose::Stand));
}

void Gen::StorageLike()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.45f, W - 0.45f, 1.0f);
    for (int i = 0; i < std::max(1, static_cast<int>((D - 1.0f) / 1.6f)); ++i) {
        Fix(FixtureType::Shelf, 0.35f, 1.6f + 1.6f * static_cast<float>(i), 0.5f, 2.0f, 1.2f, AlongV() + kPi * 0.5f, kGrey);
        Fix(FixtureType::Shelf, W - 0.35f, 1.6f + 1.6f * static_cast<float>(i), 0.5f, 2.0f, 1.2f, AlongV() + kPi * 0.5f, kGrey);
    }
    if (W > 3.5f) Fix(FixtureType::Box, W * 0.5f, D - 1.0f, 0.8f, 0.6f, 0.8f, 0.3f, kYellow);
    out.work.push_back(MakeSpot(W * 0.5f, D * 0.5f, DoorYaw(), SpotKind::Work, Pose::Work));
    out.teacher.push_back(out.work.back());
}

void Gen::Toilets()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.5f, W - 0.5f, 1.0f);
    const int stalls = std::max(2, static_cast<int>((D - 1.5f) / 1.0f));
    for (int i = 0; i < stalls; ++i) Fix(FixtureType::Box, W - 0.6f, 1.2f + 1.0f * static_cast<float>(i), 1.1f, 2.0f, 0.9f, AlongV(), kGrey);
    for (int i = 0; i < 3; ++i) Fix(FixtureType::Counter, 0.35f, 1.6f + 1.1f * static_cast<float>(i), 0.5f, 0.85f, 0.6f, AlongV(), kWhite);
    for (int i = 0; i < 3; ++i) out.idle.push_back(MakeSpot(1.1f, 1.6f + 1.1f * static_cast<float>(i), YawLocal(-1, 0), SpotKind::Stand, Pose::Stand));
    out.work.push_back(MakeSpot(W * 0.5f, 1.2f, DoorYaw(), SpotKind::Work, Pose::Sweep));
}

void Gen::ChangingRoom()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.5f, W - 0.5f, 1.2f);
    for (int i = 0; i < 4; ++i) Fix(FixtureType::Bench, 2.0f + (W - 4.0f) * (static_cast<float>(i) + 0.5f) / 4.0f, D * 0.5f, 1.8f, 0.45f, 0.35f, AlongU(), kBlue);
    for (int i = 0; i < 4; ++i) Fix(FixtureType::Locker, 1.5f + (W - 3.0f) * (static_cast<float>(i) + 0.5f) / 4.0f, D - 0.3f, 2.2f, 1.9f, 0.5f, AlongU(), kGrey);
    for (int i = 0; i < 6; ++i) out.idle.push_back(MakeSpot(1.2f + (W - 2.4f) * (static_cast<float>(i) + 0.5f) / 6.0f, D * 0.5f + 0.6f, YawLocal(0, 1), SpotKind::Stand, Pose::Stand));
}

void Gen::Lockers()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.5f, W - 0.5f, 1.2f);
    for (int row = 0; row < 3; ++row)
        for (int i = 0; i < std::max(1, static_cast<int>((D - 2.0f) / 2.0f)); ++i)
            Fix(FixtureType::Locker, 1.5f + 3.2f * static_cast<float>(row), 1.8f + 2.0f * static_cast<float>(i), 0.5f, 1.9f, 2.0f, AlongV() + kPi * 0.5f, kBlue);
    for (int i = 0; i < 8; ++i) out.idle.push_back(MakeSpot(1.0f + (W - 2.0f) * (static_cast<float>(i) + 0.5f) / 8.0f, 0.9f, DoorYaw(), SpotKind::Stand, Pose::Stand));
}

void Gen::Drama()
{
    const float W = f_.W, D = f_.D;
    SetAisles(0.7f, W - 0.7f, 1.2f);
    Fix(FixtureType::Stage, W * 0.5f, D - 2.0f, W - 2.0f, 0.5f, 3.6f, AlongU(), kDark);
    WallBoard(std::min(W - 3.0f, 4.0f));
    out.teacher.push_back(MakeSpot(W * 0.5f, D - 1.8f, DoorYaw(), SpotKind::Teacher, Pose::Lecture));
    out.work.push_back(MakeSpot(1.2f, D - 1.0f, DoorYaw(), SpotKind::Work, Pose::Work));
    // Everyone stands in lines on the open floor (or on the stage).
    int placed = 0;
    for (int r = 0; r < 6 && placed < room_.capacity; ++r)
        for (int c = 0; c < std::max(2, static_cast<int>((W - 2.0f) / 1.2f)) && placed < room_.capacity; ++c) {
            const float u = 1.0f + (W - 2.0f) * (static_cast<float>(c) + 0.5f) / std::max(2.0f, std::floor((W - 2.0f) / 1.2f));
            const float v = 2.0f + 1.4f * static_cast<float>(r);
            if (v > D - 4.5f) continue;
            out.seats.push_back(MakeSpot(u, v, FarYaw(), SpotKind::Seat, Pose::Stand));
            ++placed;
        }
    Fix(FixtureType::Bench, W - 0.4f, D * 0.4f, 2.4f, 0.45f, 0.4f, AlongV(), kBlue);
}

void Gen::Idle(float spacing, float margin)
{
    // Loiter places on a jittered grid, shuffled so that people arriving one after another spread over the
    // whole area instead of filling it row by row.
    const Rect& r = room_.rect;
    const std::size_t first = out.idle.size();
    for (float x = r.x0 + margin; x < r.x1 - margin + 1e-3f; x += spacing)
        for (float z = r.z0 + margin; z < r.z1 - margin + 1e-3f; z += spacing) {
            const Vec2 jitter{rng_.Range(-0.3f, 0.3f), rng_.Range(-0.3f, 0.3f)};
            out.idle.push_back({r.Clamp(Vec2{x, z} + jitter, margin), rng_.Range(-kPi, kPi), SpotKind::Stand, Pose::Stand});
        }
    for (std::size_t i = out.idle.size(); i > first + 1; --i) {
        const std::size_t j = first + static_cast<std::size_t>(rng_.Below(static_cast<int>(i - first)));
        std::swap(out.idle[i - 1], out.idle[j]);
    }
}

void Gen::CorridorIdle()
{
    // Along both walls, 0.55 m from them, so the middle stays free for walkers.
    const Rect& r = room_.rect;
    const bool alongX = r.Width() >= r.Depth();
    if (alongX) {
        for (float x = r.x0 + 1.0f; x < r.x1 - 0.8f; x += 1.1f) {
            out.idle.push_back({{x, r.z0 + 0.55f}, YawOf({0, 1}), SpotKind::Stand, Pose::Stand});
            out.idle.push_back({{x + 0.5f, r.z1 - 0.55f}, YawOf({0, -1}), SpotKind::Stand, Pose::Stand});
        }
    } else {
        for (float z = r.z0 + 1.0f; z < r.z1 - 0.8f; z += 1.1f) {
            out.idle.push_back({{r.x0 + 0.55f, z}, YawOf({1, 0}), SpotKind::Stand, Pose::Stand});
            out.idle.push_back({{r.x1 - 0.55f, z + 0.5f}, YawOf({-1, 0}), SpotKind::Stand, Pose::Stand});
        }
    }
}

void Gen::Outdoor()
{
    const Rect& r = room_.rect;
    // Lamp posts along the edges of the paved zones, every 18 m or so, a little inside the boundary.
    if (room_.type == RoomType::Plaza || room_.type == RoomType::Parking || room_.type == RoomType::Courtyard || room_.type == RoomType::BikeArea) {
        const float inset = 1.2f;
        for (float x = r.x0 + 9.0f; x < r.x1 - 6.0f; x += 18.0f) {
            out.fixtures.push_back({FixtureType::Lamp, {x, r.z0 + inset}, 0, 0.2f, 4.5f, 0.2f, 0, kMetal, true});
            if (r.Depth() > 20.0f) out.fixtures.push_back({FixtureType::Lamp, {x + 9.0f, r.z1 - inset}, 0, 0.2f, 4.5f, 0.2f, 0, kMetal, true});
        }
    }
    switch (room_.type) {
    case RoomType::Courtyard: {
        // Benches around a central bed of trees; open lawn between.
        for (int i = 0; i < 6; ++i) {
            const float x = r.x0 + 12.0f + (r.Width() - 24.0f) * (static_cast<float>(i) + 0.5f) / 6.0f;
            out.fixtures.push_back({FixtureType::TreeTrunk, {x, r.Center().z - 6.0f}, 0, 0.5f, 6.0f, 0.5f, 0, kGreen, true});
            out.fixtures.push_back({FixtureType::TreeTrunk, {x + 6.0f, r.Center().z + 8.0f}, 0, 0.5f, 5.0f, 0.5f, 0, kGreen, true});
            out.fixtures.push_back({FixtureType::Bench, {x, r.Center().z - 3.0f}, 0, 1.8f, 0.45f, 0.5f, YawOfX({1, 0}), kWood, true});
            out.fixtures.push_back({FixtureType::Bench, {x + 6.0f, r.Center().z + 11.0f}, 0, 1.8f, 0.45f, 0.5f, YawOfX({1, 0}), kWood, true});
            // Two sitters per bench, facing away from the trees behind them.
            for (const float du : {-0.45f, 0.45f}) {
                out.idle.push_back({{x + du, r.Center().z - 3.0f}, YawOf({0, 1}), SpotKind::Stand, Pose::Sit});
                out.idle.push_back({{x + 6.0f + du, r.Center().z + 11.0f}, YawOf({0, -1}), SpotKind::Stand, Pose::Sit});
            }
        }
        Idle(5.0f, 3.0f);
        break;
    }
    case RoomType::Playground: {
        out.fixtures.push_back({FixtureType::Box, r.Center(), 0, 6.0f, 2.2f, 3.0f, 0.0f, kRed, true});      // climbing frame
        out.fixtures.push_back({FixtureType::Box, {r.Center().x + 12.0f, r.Center().z + 8.0f}, 0, 4.0f, 1.6f, 2.0f, 0.0f, kBlue, true});
        Idle(4.0f, 3.0f);
        break;
    }
    case RoomType::FootballField: {
        out.fixtures.push_back({FixtureType::Goal, {r.x0 + 0.5f, r.Center().z}, 0, 7.3f, 2.4f, 2.0f, YawOfX({0, 1}), kWhite, false});
        out.fixtures.push_back({FixtureType::Goal, {r.x1 - 0.5f, r.Center().z}, 0, 7.3f, 2.4f, 2.0f, YawOfX({0, 1}), kWhite, false});
        // Exercise places for PE lessons.
        for (float x = r.x0 + 6.0f; x < r.x1 - 5.0f; x += 5.0f)
            for (float z = r.z0 + 5.0f; z < r.z1 - 4.0f; z += 5.0f) out.seats.push_back({{x, z}, 0.0f, SpotKind::Seat, Pose::Exercise});
        out.teacher.push_back({r.Center(), 0.0f, SpotKind::Teacher, Pose::Stand});
        Idle(6.0f, 4.0f);
        break;
    }
    case RoomType::Track: {
        for (float x = r.x0 + 2.0f; x < r.x1 - 1.0f; x += 4.0f)
            for (float z = r.z0 + 2.0f; z < r.z1 - 1.0f; z += 4.0f) out.seats.push_back({{x, z}, 0.0f, SpotKind::Seat, Pose::Exercise});
        out.teacher.push_back({r.Center(), 0.0f, SpotKind::Teacher, Pose::Stand});
        Idle(6.0f, 2.0f);
        break;
    }
    case RoomType::Parking: {
        // Cars in rows: they are fixtures, not occupants.
        for (float x = r.x0 + 6.0f; x < r.x1 - 6.0f; x += 3.0f)
            for (int row = 0; row < 2; ++row) {
                if (Unit(Hash(static_cast<std::uint64_t>(x * 10.0f), static_cast<std::uint64_t>(row), static_cast<std::uint64_t>(room_.id))) < 0.45f) continue;
                const float z = r.z0 + 6.0f + static_cast<float>(row) * 22.0f;
                const std::uint64_t h = Hash(static_cast<std::uint64_t>(x), static_cast<std::uint64_t>(row), 9);
                // Nose in or nose out, and a colour from the palette's saturated end.
                out.fixtures.push_back({FixtureType::Car, {x, z}, 0.0f, 1.85f, 1.45f, 4.4f, Unit(h * 3 + 1) < 0.5f ? 0.0f : kPi,
                                        static_cast<std::uint8_t>(3 + static_cast<int>(Unit(h) * 5.0f)), true});
            }
        Idle(8.0f, 2.0f);
        break;
    }
    case RoomType::BikeArea: {
        for (float x = r.x0 + 3.0f; x < r.x1 - 3.0f; x += 3.0f) out.fixtures.push_back({FixtureType::BikeRack, {x, r.z0 + 6.0f}, 0, 2.4f, 0.9f, 0.6f, 0.0f, kMetal, true});
        for (float x = r.x0 + 3.0f; x < r.x1 - 3.0f; x += 3.0f) out.fixtures.push_back({FixtureType::BikeRack, {x, r.z1 - 5.0f}, 0, 2.4f, 0.9f, 0.6f, 0.0f, kMetal, true});
        Idle(4.0f, 2.0f);
        break;
    }
    case RoomType::AssemblyPoint:
        // Spots on a tight grid: where classes stand in rows during a fire drill.
        for (float x = r.x0 + 1.0f; x < r.x1 - 0.5f; x += 1.1f)
            for (float z = r.z0 + 1.0f; z < r.z1 - 0.5f; z += 1.1f) out.idle.push_back({{x, z}, 0.0f, SpotKind::Stand, Pose::Stand});
        break;
    default:
        // Plaza, east yard, lawns: some trees and benches, loiter places.
        for (float x = r.x0 + 10.0f; x < r.x1 - 6.0f; x += 28.0f)
            for (float z = r.z0 + 5.0f; z < r.z1 - 4.0f; z += 24.0f)
                if (r.Width() > 12.0f && r.Depth() > 12.0f) out.fixtures.push_back({FixtureType::TreeTrunk, {x, z}, 0, 0.5f, 5.5f, 0.5f, 0, kGreen, true});
        Idle(5.0f, 2.0f);
        break;
    }
}

void Gen::Run()
{
    switch (room_.type) {
    case RoomType::Classroom:
    case RoomType::LanguageRoom: GridClass(false); break;
    case RoomType::ComputerLab: {
        // Computer desks in rows facing the front instead of double desks.
        const float W = f_.W, D = f_.D;
        SetAisles(0.5f, W - 0.5f, 1.0f);
        WallBoard(std::min(W - 1.5f, 4.0f));
        out.teacher.push_back(MakeSpot(W * 0.25f, D - 1.3f, DoorYaw(), SpotKind::Teacher, Pose::Lecture));
        Fix(FixtureType::TeacherDesk, W * 0.75f, D - 1.4f, 1.5f, 0.76f, 0.75f, DoorYaw(), kDark);
        out.work.push_back(MakeSpot(W * 0.75f, D - 0.8f, DoorYaw(), SpotKind::Work, Pose::SitType));
        const int cols = std::clamp(static_cast<int>((W - 1.2f) / 1.5f), 2, 7);
        const int rows = std::clamp(static_cast<int>((D - 3.4f) / 1.5f), 1, 6);
        int placed = 0;
        for (int r = 0; r < rows && placed < room_.capacity; ++r)
            for (int c = 0; c < cols && placed < room_.capacity; ++c) {
                const float u = 0.6f + (W - 1.2f) * (static_cast<float>(c) + 0.5f) / static_cast<float>(cols);
                const float v = D - 3.0f - 1.5f * static_cast<float>(r);
                Fix(FixtureType::ComputerDesk, u, v, 1.1f, 0.74f, 0.7f, DoorYaw(), kLight);
                Fix(FixtureType::Chair, u, v - 0.7f, 0.42f, 0.88f, 0.42f, FarYaw(), kGrey, false);
                out.seats.push_back(MakeSpot(u, v - 0.7f, FarYaw(), SpotKind::Seat, Pose::SitType));
                ++placed;
            }
        break;
    }
    case RoomType::PhysicsLab:
    case RoomType::ChemistryLab:
    case RoomType::BiologyLab: Lab(); break;
    case RoomType::ArtStudio: Art(); break;
    case RoomType::MusicRoom: Music(); break;
    case RoomType::Workshop: Workshop(); break;
    case RoomType::Gym: Gym(); break;
    case RoomType::Library: Library(); break;
    case RoomType::Auditorium: Auditorium(); break;
    case RoomType::Cafeteria: Cafeteria(); break;
    case RoomType::DramaStudio: Drama(); break;
    case RoomType::Kitchen: Kitchen(); break;
    case RoomType::Reception: OfficeLike(2); break;
    case RoomType::AdminOffice: OfficeLike(room_.rect.Width() > 9.0f || room_.rect.Depth() > 9.0f ? 2 : 1); break;
    case RoomType::PrincipalOffice: OfficeLike(1); break;
    case RoomType::StaffRoom: StaffRoom(); break;
    case RoomType::MeetingRoom: MeetingRoom(); break;
    case RoomType::Nurse: Nurse(); break;
    case RoomType::Counseling: Counseling(); break;
    case RoomType::ServerRoom: ServerRoom(); break;
    case RoomType::SecurityOffice: Security(); break;
    case RoomType::Storage:
    case RoomType::LabPrep:
    case RoomType::Boiler:
    case RoomType::Electrical:
    case RoomType::Janitor:
    case RoomType::Archive:
    case RoomType::EquipmentStore:
    case RoomType::FitnessRoom: StorageLike(); break;
    case RoomType::Toilets: Toilets(); break;
    case RoomType::ChangingRoom: ChangingRoom(); break;
    case RoomType::LockerHall: Lockers(); break;
    case RoomType::Corridor: CorridorIdle(); break;
    case RoomType::Lobby: {
        const float W = f_.W, D = f_.D;
        SetAisles(1.0f, W - 1.0f, 2.0f);
        // Reception desk in the middle, benches along the sides, trophy cabinets.
        Fix(FixtureType::Counter, W * 0.5f, D * 0.5f, 5.0f, 1.05f, 1.0f, AlongU(), kDark);
        out.work.push_back(MakeSpot(W * 0.5f - 1.0f, D * 0.5f + 0.9f, YawLocal(0, -1), SpotKind::Work, Pose::Work));
        out.work.push_back(MakeSpot(W * 0.5f + 1.0f, D * 0.5f + 0.9f, YawLocal(0, -1), SpotKind::Work, Pose::Work));
        for (int i = 0; i < 4; ++i) {
            const float u = 4.0f + (W - 8.0f) * (static_cast<float>(i) + 0.5f) / 4.0f;
            Fix(FixtureType::Bench, u, D - 0.5f, 2.2f, 0.45f, 0.5f, AlongU(), kBlue);
            for (const float du : {-0.6f, 0.0f, 0.6f}) out.idle.push_back(MakeSpot(u + du, D - 0.5f, DoorYaw(), SpotKind::Stand, Pose::Sit));
        }
        Fix(FixtureType::Plant, 2.0f, D * 0.5f, 0.7f, 1.6f, 0.7f, 0.0f, kGreen, false);
        Fix(FixtureType::Plant, W - 2.0f, D * 0.5f, 0.7f, 1.6f, 0.7f, 0.0f, kGreen, false);
        // Trophy cabinets along both side walls (a Vending fixture draws as a cabinet with a glass front).
        for (const float v : {D * 0.3f, D * 0.7f}) {
            Fix(FixtureType::Vending, 0.4f, v, 1.8f, 2.0f, 0.6f, YawLocal(1, 0), kLight);
            Fix(FixtureType::Vending, W - 0.4f, v, 1.8f, 2.0f, 0.6f, YawLocal(-1, 0), kLight);
        }
        Idle(3.0f, 1.5f);
        break;
    }
    case RoomType::StairWell:
    case RoomType::ElevatorCab: break;
    default:
        if (!room_.Indoor()) Outdoor();
        break;
    }
}

}  // namespace

RoomLayout BuildRoomLayout(const SchoolPlan& plan, RoomId room)
{
    Gen g(plan, plan.room(room));
    g.Run();
    RoomLayout& out = g.out;
    // Nothing stands in front of a door: a clearance zone behind every door of the room is cleared of
    // furniture and of the places people occupy, whatever the generator put there.
    const Room& r = plan.room(room);
    std::vector<Rect> zones;
    for (const DoorId did : r.doors) {
        const Door& d = plan.door(did);
        const Side side = d.a == room ? d.sideOfA : Opposite(d.sideOfA);
        const Vec2 inward = SideNormal(side) * -1.0f;
        const Vec2 along = inward.RightHand();
        const float half = d.width * 0.5f + 0.45f;
        const float depth = 1.2f;
        const Vec2 a = d.pos - along * half, b = d.pos + along * half + inward * depth;
        zones.push_back({std::min(a.x, b.x), std::min(a.z, b.z), std::max(a.x, b.x), std::max(a.z, b.z)});
    }
    const auto inZone = [&](Vec2 p, float radius) {
        for (const Rect& z : zones)
            if (z.Contains(p, radius)) return true;
        return false;
    };
    std::erase_if(out.fixtures, [&](const Fixture& f) {
        if (f.type == FixtureType::Whiteboard || f.type == FixtureType::Screen) return inZone(f.pos, f.sx * 0.5f);   // wall-mounted: only its width matters
        return inZone(f.pos, std::max(f.sx, f.sz) * 0.5f);
    });
    for (std::vector<Spot>* list : {&out.seats, &out.teacher, &out.work, &out.idle, &out.queue, &out.eat, &out.browse})
        std::erase_if(*list, [&](const Spot& sp) { return inZone(sp.pos, 0.1f); });
    return std::move(out);
}

// --- aisle paths -------------------------------------------------------------------------------------------------------

namespace {
Vec2 LocalToWorld(const RoomLayout::Aisles& a, float u, float v)
{
    return a.origin + a.uAxis * u + a.vAxis * v;
}
}  // namespace

std::vector<Vec2> RoomLayout::PathTo(Vec2 entry, const Spot& spot) const
{
    std::vector<Vec2> path;
    if (!aisles.valid) return path;
    const Vec2 le = {Dot(entry - aisles.origin, aisles.uAxis), Dot(entry - aisles.origin, aisles.vAxis)};
    const Vec2 ls = {Dot(spot.pos - aisles.origin, aisles.uAxis), Dot(spot.pos - aisles.origin, aisles.vAxis)};
    const float side = ls.x < aisles.W * 0.5f ? aisles.sideLeft : aisles.sideRight;
    const float frontV = std::max(aisles.frontV, std::min(le.z, 1.6f));
    path.push_back(LocalToWorld(aisles, side, frontV));
    if (ls.z > frontV + 0.3f) path.push_back(LocalToWorld(aisles, side, ls.z));
    return path;
}

std::vector<Vec2> RoomLayout::PathFrom(const Spot& spot, Vec2 exitPoint) const
{
    std::vector<Vec2> path;
    if (!aisles.valid) return path;
    const Vec2 ls = {Dot(spot.pos - aisles.origin, aisles.uAxis), Dot(spot.pos - aisles.origin, aisles.vAxis)};
    const Vec2 lx = {Dot(exitPoint - aisles.origin, aisles.uAxis), Dot(exitPoint - aisles.origin, aisles.vAxis)};
    const float side = ls.x < aisles.W * 0.5f ? aisles.sideLeft : aisles.sideRight;
    const float frontV = std::max(aisles.frontV, std::min(lx.z, 1.6f));
    if (ls.z > frontV + 0.3f) path.push_back(LocalToWorld(aisles, side, ls.z));
    path.push_back(LocalToWorld(aisles, side, frontV));
    return path;
}

}  // namespace School
