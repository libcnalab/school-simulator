#include "Rendering/RoomGeometry.hpp"

#include <algorithm>
#include <cmath>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "World/Rng.hpp"
#include "World/StairGeometry.hpp"

using namespace Microsoft::Xna::Framework;
using School::Door;
using School::DoorId;
using School::DoorKind;
using School::Fixture;
using School::FixtureType;
using School::Rect;
using School::Room;
using School::RoomId;
using School::RoomLayout;
using School::RoomType;
using School::SchoolPlan;
using School::Side;
using School::Vec2;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kWindowSill = 0.9f;
constexpr float kWindowHeight = 1.5f;
constexpr float kWindowWidth = 1.5f;
constexpr float kWindowPitch = 2.4f;
constexpr float kFacadeOffset = 0.3f;

const Vector3 kWhite(1, 1, 1);

Vector3 Tint(const float c[3]) { return Vector3(c[0], c[1], c[2]); }

/** One hole in a wall: an interval along the wall and an absolute height range. */
struct Opening {
    float lo, hi;
    float y0, y1;
};

/**
 * A wall as a set of patches: full-height strips between the openings, the parts above and below each
 * opening. `origin` is the wall's start at floor level, `u` the unit direction along it, `length` its length.
 */
void AddWall(MeshBuilder& b, const Vector3& origin, const Vector3& u, float length, float y0, float y1, std::vector<Opening> openings, float texMeters,
             const Vector3& tint, float maxCell = 2.5f)
{
    std::sort(openings.begin(), openings.end(), [](const Opening& a, const Opening& c) { return a.lo < c.lo; });
    const auto strip = [&](float a, float c, float h0, float h1) {
        if (c - a < 0.005f || h1 - h0 < 0.005f) return;
        b.AddPatch(origin + u * a + Vector3(0, h0 - y0, 0), u * (c - a), Vector3(0, h1 - h0, 0), texMeters, tint, maxCell);
    };
    float cursor = 0.0f;
    for (const Opening& o : openings) {
        const float lo = std::clamp(o.lo, 0.0f, length);
        const float hi = std::clamp(o.hi, 0.0f, length);
        if (hi <= cursor) continue;
        strip(cursor, std::max(cursor, lo), y0, y1);
        const float a = std::max(cursor, lo);
        strip(a, hi, y0, o.y0);
        strip(a, hi, o.y1, y1);
        cursor = hi;
    }
    strip(cursor, length, y0, y1);
}

/** Start point and direction of the inside face of a wall (normal pointing into the room). */
void InnerWallFrame(const Rect& r, Side side, Vector3& origin, Vector3& u, float& length)
{
    switch (side) {
    case Side::North: origin = Vector3(r.x0, 0, r.z0); u = Vector3(1, 0, 0); length = r.Width(); break;
    case Side::South: origin = Vector3(r.x1, 0, r.z1); u = Vector3(-1, 0, 0); length = r.Width(); break;
    case Side::West: origin = Vector3(r.x0, 0, r.z1); u = Vector3(0, 0, -1); length = r.Depth(); break;
    default: origin = Vector3(r.x1, 0, r.z0); u = Vector3(0, 0, 1); length = r.Depth(); break;
    }
}

/** Coordinate of a point along a wall, measured from the inner frame's origin. */
float AlongWall(const Rect& r, Side side, Vec2 p)
{
    switch (side) {
    case Side::North: return p.x - r.x0;
    case Side::South: return r.x1 - p.x;
    case Side::West: return r.z1 - p.z;
    default: return p.z - r.z0;
    }
}

Side SideOfDoor(const Door& d, RoomId room) { return d.a == room ? d.sideOfA : School::Opposite(d.sideOfA); }

/** Window positions along a wall of the given length, avoiding the door intervals. */
std::vector<float> WindowCentres(float length, const std::vector<Opening>& doors)
{
    std::vector<float> out;
    if (length < kWindowWidth + 1.0f) return out;
    const int n = std::max(1, static_cast<int>((length - 0.8f) / kWindowPitch));
    for (int i = 0; i < n; ++i) {
        const float c = (length - static_cast<float>(n - 1) * kWindowPitch) * 0.5f + static_cast<float>(i) * kWindowPitch;
        bool clash = false;
        for (const Opening& d : doors)
            if (c + kWindowWidth * 0.5f + 0.25f > d.lo && c - kWindowWidth * 0.5f - 0.25f < d.hi) clash = true;
        if (!clash) out.push_back(c);
    }
    return out;
}

std::string WallTextureOf(const Room& room)
{
    switch (room.type) {
    case RoomType::Toilets:
    case RoomType::ChangingRoom:
    case RoomType::Kitchen: return "wall_tile";
    case RoomType::Gym:
    case RoomType::Auditorium:
    case RoomType::MusicRoom:
    case RoomType::DramaStudio:
    case RoomType::FitnessRoom: return "wall_panel";
    case RoomType::Workshop:
    case RoomType::Boiler:
    case RoomType::Electrical:
    case RoomType::Storage:
    case RoomType::EquipmentStore:
    case RoomType::Janitor:
    case RoomType::Archive:
    case RoomType::StairWell:
    case RoomType::ElevatorCab:
    case RoomType::LockerHall: return "wall_concrete";
    default: return "wall_plaster";
    }
}

Vector3 WallTintOf(const SchoolPlan& plan, const Room& room)
{
    const School::Wing& w = plan.wings[static_cast<std::size_t>(room.wing)];
    if (room.type == RoomType::Corridor || room.type == RoomType::Lobby || room.type == RoomType::StairWell) return Tint(w.wall);
    switch (room.type) {
    case RoomType::ArtStudio: return Vector3(0.97f, 0.95f, 0.90f);
    case RoomType::ComputerLab: return Vector3(0.86f, 0.90f, 0.96f);
    case RoomType::PhysicsLab:
    case RoomType::ChemistryLab:
    case RoomType::BiologyLab: return Vector3(0.90f, 0.95f, 0.92f);
    case RoomType::Nurse: return Vector3(0.95f, 0.98f, 0.98f);
    case RoomType::Cafeteria: return Vector3(0.98f, 0.94f, 0.82f);
    case RoomType::Library: return Vector3(0.94f, 0.90f, 0.82f);
    case RoomType::Auditorium: return Vector3(0.45f, 0.30f, 0.28f);
    case RoomType::Gym: return Vector3(0.90f, 0.90f, 0.86f);
    default: return Vector3(0.95f, 0.95f, 0.93f);
    }
}

struct FixtureSink {
    MeshSet& set;
    Matrix world;
    const Fixture& f;
    School::Rng rng;

    MeshBuilder& B(const char* tex, bool transparent = false) { return transparent ? set.Transparent(tex) : set.Opaque(tex); }
    void Box(const char* tex, const Vector3& min, const Vector3& max, const Vector3& tint, float emissive = 0.0f, BoxFaces faces = BoxFaces::All,
             float texMeters = 1.0f)
    {
        B(tex).AddBoxTransformed(world, min, max, texMeters, tint, faces, 4.0f, emissive);
    }
    /** A front-facing panel (local -z side) with its own texture mapped once. */
    void Front(const char* tex, float x0, float y0, float x1, float y1, float z, const Vector3& tint, float emissive = 0.0f, bool transparent = false, float alpha = 1.0f)
    {
        MeshBuilder& b = B(tex, transparent);
        b.SetAlpha(alpha);
        b.AddPatch(Vector3::Transform(Vector3(x1, y0, z), world), Vector3::TransformNormal(Vector3(x0 - x1, 0, 0), world),
                   Vector3::TransformNormal(Vector3(0, y1 - y0, 0), world), 0.0f, tint, 10.0f, emissive);
        b.SetAlpha(1.0f);
    }
    /** A quad p0-p1-p2-p3 (local coordinates, going round) facing away from `inside`. */
    void Quad(const char* tex, const Vector3& p0, const Vector3& p1, const Vector3& p3, const Vector3& inside, const Vector3& tint, float emissive = 0.0f)
    {
        Vector3 u = p1 - p0, v = p3 - p0;
        const Vector3 centre = p0 + (u + v) * 0.5f;
        Vector3 origin = p0;
        if (Vector3::Dot(Vector3::Cross(u, v), centre - inside) < 0.0f) std::swap(u, v);
        B(tex).AddPatch(Vector3::Transform(origin, world), Vector3::TransformNormal(u, world), Vector3::TransformNormal(v, world), 0.0f, tint, 10.0f, emissive);
    }
    /** A box that narrows from the bottom rectangle (x0,z0,x1,z1 at y0) to the top one (at y1): a car's greenhouse. */
    void Frustum(const char* sideTex, float y0, float y1, const float bottom[4], const float top[4], const Vector3& sides, const Vector3& roof)
    {
        const Vector3 b00(bottom[0], y0, bottom[1]), b10(bottom[2], y0, bottom[1]), b11(bottom[2], y0, bottom[3]), b01(bottom[0], y0, bottom[3]);
        const Vector3 t00(top[0], y1, top[1]), t10(top[2], y1, top[1]), t11(top[2], y1, top[3]), t01(top[0], y1, top[3]);
        const Vector3 centre = (b00 + b11 + t00 + t11) * 0.25f;
        Quad(sideTex, b00, b10, t00, centre, sides);   // back
        Quad(sideTex, b10, b11, t10, centre, sides);   // right
        Quad(sideTex, b11, b01, t11, centre, sides);   // front
        Quad(sideTex, b01, b00, t01, centre, sides);   // left
        Quad("white", t00, t10, t01, centre, roof);    // roof
    }
    /** A cylinder lying along local x: the outer face at x = `outerX`, running `width` inwards. */
    void Wheel(float outerX, float y, float z, float radius, float width, const Vector3& tyre, const Vector3& hub)
    {
        const bool plusX = outerX > 0.0f;
        MeshBuilder tmp;
        tmp.AddCylinder(Vector3(0, 0, 0), radius, width, 14, 0.0f, tyre, true);
        tmp.AddCylinder(Vector3(0, width, 0), radius * 0.55f, 0.012f, 10, 0.0f, hub, true);
        const Matrix m = Matrix::CreateRotationZ(plusX ? -1.5707963f : 1.5707963f) * Matrix::CreateTranslation(plusX ? outerX - width : outerX + width, y, z) * world;
        B("white").AppendTransformed(tmp, m);
    }
    void Legs(float sx, float sz, float height, float thickness, const Vector3& tint, float inset = 0.06f)
    {
        const float hx = sx * 0.5f - inset, hz = sz * 0.5f - inset, t = thickness * 0.5f;
        for (int i = 0; i < 4; ++i) {
            const float x = (i & 1) ? hx : -hx, z = (i & 2) ? hz : -hz;
            Box("white", Vector3(x - t, 0, z - t), Vector3(x + t, height, z + t), tint, 0.0f, BoxFaces::Sides);
        }
    }
};

void AddBar(MeshBuilder& b, const Vector3& from, const Vector3& to, float thickness, const Vector3& tint);

Vector3 Palette(int i) { return PaletteColor(i); }

void AddFixture(MeshSet& set, const Fixture& f, float baseY, int roomId)
{
    FixtureSink s{set, Matrix::CreateRotationY(f.yaw) * Matrix::CreateTranslation(f.pos.x, baseY + f.y0, f.pos.z), f,
                  School::Rng(School::Hash(static_cast<std::uint64_t>(roomId), static_cast<std::uint64_t>(f.pos.x * 7.0f), static_cast<std::uint64_t>(f.pos.z * 13.0f)))};
    const float hx = f.sx * 0.5f, hz = f.sz * 0.5f;
    const Vector3 c = Palette(f.color);
    const Vector3 wood(0.62f, 0.45f, 0.28f);
    const Vector3 dark(0.22f, 0.2f, 0.19f);
    const Vector3 metal(0.68f, 0.7f, 0.73f);
    switch (f.type) {
    case FixtureType::Desk:
    case FixtureType::Table:
    case FixtureType::TeacherDesk: {
        const Vector3 top = f.color == 9 ? metal : c;
        s.B("floor_wood").AddBoxTransformed(s.world, Vector3(-hx, f.sy - 0.05f, -hz), Vector3(hx, f.sy, hz), 1.2f, top);
        s.Legs(f.sx, f.sz, f.sy - 0.05f, 0.05f, f.color == 9 ? metal : dark);
        if (f.type == FixtureType::TeacherDesk) s.Box("white", Vector3(-hx + 0.05f, 0.1f, -hz), Vector3(hx - 0.05f, f.sy - 0.05f, -hz + 0.03f), c * 0.9f);
        break;
    }
    case FixtureType::Chair: {
        s.Box("white", Vector3(-hx, 0.42f, -hz), Vector3(hx, 0.46f, hz), c);
        s.Box("white", Vector3(-hx, 0.46f, hz - 0.05f), Vector3(hx, f.sy, hz), c);
        s.Legs(f.sx, f.sz, 0.42f, 0.03f, metal, 0.03f);
        break;
    }
    case FixtureType::Stool: {
        s.Box("white", Vector3(-hx, f.sy - 0.04f, -hz), Vector3(hx, f.sy, hz), c);
        s.Legs(f.sx, f.sz, f.sy - 0.04f, 0.03f, metal, 0.04f);
        break;
    }
    case FixtureType::Bench: {
        if (f.color <= 2) s.B("floor_wood").AddBoxTransformed(s.world, Vector3(-hx, f.sy - 0.06f, -hz), Vector3(hx, f.sy, hz), 1.0f, c);
        else s.Box("white", Vector3(-hx, f.sy - 0.06f, -hz), Vector3(hx, f.sy, hz), c);
        for (const float x : {-hx + 0.15f, hx - 0.15f}) s.Box("white", Vector3(x - 0.04f, 0, -hz + 0.03f), Vector3(x + 0.04f, f.sy - 0.06f, hz - 0.03f), dark);
        break;
    }
    case FixtureType::Whiteboard: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy, hz), Vector3(0.75f, 0.75f, 0.78f));
        s.Front("whiteboard", -hx + 0.03f, 0.03f, hx - 0.03f, f.sy - 0.03f, -hz - 0.004f, kWhite, 0.15f);
        // a pen tray
        s.Box("white", Vector3(-hx * 0.6f, -0.03f, -hz - 0.06f), Vector3(hx * 0.6f, 0.0f, -hz), metal);
        break;
    }
    case FixtureType::Screen:
    case FixtureType::Scoreboard: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy, hz), dark);
        s.Front("screen", -hx + 0.04f, 0.04f, hx - 0.04f, f.sy - 0.04f, -hz - 0.004f, kWhite, 0.9f);
        break;
    }
    case FixtureType::Shelf: {
        const int shelves = std::max(1, static_cast<int>(f.sy / 0.38f));
        s.Box("floor_wood", Vector3(-hx, 0, -hz), Vector3(-hx + 0.03f, f.sy, hz), c);
        s.Box("floor_wood", Vector3(hx - 0.03f, 0, -hz), Vector3(hx, f.sy, hz), c);
        s.Box("floor_wood", Vector3(-hx, 0, hz - 0.02f), Vector3(hx, f.sy, hz), c * 0.85f);
        s.Box("floor_wood", Vector3(-hx, f.sy - 0.03f, -hz), Vector3(hx, f.sy, hz), c);
        for (int i = 0; i < shelves; ++i) {
            const float y = 0.06f + (f.sy - 0.12f) * static_cast<float>(i) / static_cast<float>(shelves);
            s.Box("floor_wood", Vector3(-hx, y, -hz), Vector3(hx, y + 0.03f, hz), c);
            const float fill = 0.55f + 0.45f * s.rng.Uniform();
            s.B("books").AddBoxTransformed(s.world, Vector3(-hx + 0.05f, y + 0.03f, -hz + 0.08f), Vector3(-hx + 0.05f + (f.sx - 0.1f) * fill, y + 0.30f, hz - 0.06f), 0.0f, kWhite,
                                           BoxFaces::NoBottom, 4.0f);
        }
        break;
    }
    case FixtureType::ComputerDesk: {
        s.B("floor_wood").AddBoxTransformed(s.world, Vector3(-hx, f.sy - 0.04f, -hz), Vector3(hx, f.sy, hz), 1.2f, c);
        s.Legs(f.sx, f.sz, f.sy - 0.04f, 0.04f, metal);
        // monitor at the back, facing the chair in front; keyboard; the case under the desk
        const float mz = hz - 0.2f;
        s.Box("white", Vector3(-0.06f, f.sy, mz - 0.03f), Vector3(0.06f, f.sy + 0.12f, mz + 0.03f), dark);
        s.Box("white", Vector3(-0.24f, f.sy + 0.1f, mz - 0.015f), Vector3(0.24f, f.sy + 0.42f, mz + 0.015f), dark);
        s.Front("screen", -0.22f, f.sy + 0.12f, 0.22f, f.sy + 0.40f, mz - 0.019f, kWhite, 0.9f);
        s.Box("white", Vector3(-0.2f, f.sy, -hz + 0.12f), Vector3(0.2f, f.sy + 0.02f, -hz + 0.27f), Vector3(0.3f, 0.3f, 0.32f));
        s.Box("white", Vector3(hx - 0.25f, 0.0f, -0.15f), Vector3(hx - 0.05f, 0.42f, 0.3f), Vector3(0.35f, 0.35f, 0.38f));
        break;
    }
    case FixtureType::LabBench: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy - 0.05f, hz), c * 0.9f);
        s.Box("white", Vector3(-hx - 0.02f, f.sy - 0.05f, -hz - 0.02f), Vector3(hx + 0.02f, f.sy, hz + 0.02f), dark);
        s.B("white").AddCylinder(Vector3::Transform(Vector3(0, f.sy, 0), s.world), 0.025f, 0.35f, 8, 0.0f, metal);
        s.Box("white", Vector3(-0.2f, f.sy - 0.001f, -0.15f), Vector3(0.2f, f.sy + 0.002f, 0.15f), Vector3(0.45f, 0.5f, 0.55f));
        break;
    }
    case FixtureType::Locker: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy, hz), c);
        for (float x = -hx + 0.4f; x < hx - 0.05f; x += 0.4f) s.Box("white", Vector3(x - 0.006f, 0.05f, -hz - 0.006f), Vector3(x + 0.006f, f.sy - 0.05f, -hz), dark);
        for (float x = -hx + 0.2f; x < hx; x += 0.4f) s.Box("white", Vector3(x + 0.1f, f.sy * 0.55f, -hz - 0.02f), Vector3(x + 0.14f, f.sy * 0.55f + 0.08f, -hz), metal);
        break;
    }
    case FixtureType::Easel: {
        for (const float x : {-0.25f, 0.25f}) s.Box("white", Vector3(x - 0.02f, 0, -0.02f), Vector3(x + 0.02f, f.sy, 0.02f), wood);
        s.Box("white", Vector3(-0.02f, 0, 0.25f), Vector3(0.02f, f.sy * 0.9f, 0.29f), wood);
        s.Box("white", Vector3(-0.3f, 0.75f, -0.03f), Vector3(0.3f, 0.79f, 0.0f), wood);
        const int poster = 1 + s.rng.Below(4);
        const std::string tex = "poster_" + std::to_string(poster);
        s.Box("white", Vector3(-0.28f, 0.79f, -0.02f), Vector3(0.28f, 1.45f, 0.0f), kWhite);
        s.Front(tex.c_str(), -0.26f, 0.81f, 0.26f, 1.43f, -0.024f, kWhite);
        break;
    }
    case FixtureType::Piano: {
        s.Box("white", Vector3(-hx, 0, -hz + 0.3f), Vector3(hx, f.sy, hz), c);
        s.Box("white", Vector3(-hx, 0.68f, -hz), Vector3(hx, 0.74f, -hz + 0.3f), c);
        s.Box("white", Vector3(-hx + 0.05f, 0.74f, -hz + 0.02f), Vector3(hx - 0.05f, 0.76f, -hz + 0.26f), Vector3(0.95f, 0.95f, 0.9f));
        for (float x = -hx + 0.1f; x < hx - 0.1f; x += 0.14f) s.Box("white", Vector3(x, 0.76f, -hz + 0.02f), Vector3(x + 0.07f, 0.775f, -hz + 0.17f), dark);
        s.Legs(f.sx, 0.3f, 0.68f, 0.05f, c);
        break;
    }
    case FixtureType::Counter: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy - 0.04f, hz), c);
        s.Box("white", Vector3(-hx - 0.03f, f.sy - 0.04f, -hz - 0.03f), Vector3(hx + 0.03f, f.sy, hz + 0.03f), f.color == 9 ? metal : Vector3(0.85f, 0.82f, 0.78f));
        break;
    }
    case FixtureType::Cabinet: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy, hz), c);
        s.Box("white", Vector3(-0.004f, 0.05f, -hz - 0.004f), Vector3(0.004f, f.sy - 0.05f, -hz), dark);
        for (const float x : {-0.06f, 0.06f}) s.Box("white", Vector3(x - 0.012f, f.sy * 0.5f, -hz - 0.03f), Vector3(x + 0.012f, f.sy * 0.5f + 0.12f, -hz), metal);
        break;
    }
    case FixtureType::Rack: {
        // A cabinet with a vented door, a patch panel and servers in the slots, some of them blanked off; the
        // blinking lights are drawn by RoomEffects, the steady ones (power, drive bays) are part of the mesh.
        const Vector3 frame(0.2f, 0.21f, 0.24f);
        s.Box("white", Vector3(-hx, 0.0f, -hz), Vector3(hx, 0.08f, hz), frame);                       // plinth
        s.Box("white", Vector3(-hx, 0.08f, -hz + 0.02f), Vector3(-hx + 0.03f, f.sy, hz), frame);       // posts and sides
        s.Box("white", Vector3(hx - 0.03f, 0.08f, -hz + 0.02f), Vector3(hx, f.sy, hz), frame);
        s.Box("white", Vector3(-hx, 0.08f, hz - 0.02f), Vector3(hx, f.sy, hz), frame);                // back
        s.Box("white", Vector3(-hx, f.sy - 0.04f, -hz), Vector3(hx, f.sy, hz), frame);                // roof
        const float front = -hz;
        float y = 0.16f;
        int unit = 0;
        while (y < f.sy - 0.34f) {
            const float u1 = 0.045f;
            const float roll = s.rng.Uniform();
            const float h = roll < 0.35f ? u1 * 2.0f : u1;
            if (roll > 0.88f) {                                    // blanking panel
                s.Box("white", Vector3(-hx + 0.03f, y, front - 0.012f), Vector3(hx - 0.03f, y + h, front), Vector3(0.07f, 0.07f, 0.08f));
            } else {
                const Vector3 face = unit % 5 == 0 ? Vector3(0.18f, 0.20f, 0.30f) : unit % 3 == 0 ? Vector3(0.28f, 0.29f, 0.31f) : Vector3(0.20f, 0.21f, 0.23f);
                s.Box("white", Vector3(-hx + 0.03f, y, front - 0.03f), Vector3(hx - 0.03f, y + h - 0.004f, front), face);
                // drive bays: a row of small lighter slots
                const int bays = h > u1 * 1.5f ? 8 : 4;
                for (int b = 0; b < bays; ++b) {
                    const float x0 = -hx + 0.07f + static_cast<float>(b) * (f.sx - 0.2f) / static_cast<float>(bays);
                    s.Box("white", Vector3(x0, y + 0.006f, front - 0.036f), Vector3(x0 + (f.sx - 0.2f) / static_cast<float>(bays) - 0.008f, y + h - 0.012f, front - 0.03f), Vector3(0.34f, 0.35f, 0.38f));
                }
                s.Box("white", Vector3(hx - 0.075f, y + 0.012f, front - 0.036f), Vector3(hx - 0.06f, y + 0.026f, front - 0.03f), Vector3(0.2f, 1.0f, 0.3f), 1.0f);   // power
            }
            y += h + 0.004f;
            ++unit;
        }
        // patch panel with a loom of cables on top of the unit stack
        s.Box("white", Vector3(-hx + 0.03f, y, front - 0.02f), Vector3(hx - 0.03f, y + 0.07f, front), Vector3(0.32f, 0.32f, 0.35f));
        const Vector3 cables[3] = {Vector3(0.15f, 0.35f, 0.9f), Vector3(0.95f, 0.8f, 0.15f), Vector3(0.9f, 0.4f, 0.1f)};
        for (int k = 0; k < 9; ++k) {
            const float x = -hx + 0.07f + static_cast<float>(k) * (f.sx - 0.14f) / 9.0f;
            s.Box("white", Vector3(x, y - 0.02f, front - 0.05f), Vector3(x + 0.012f, y + 0.5f - 0.04f * static_cast<float>(k % 3), front - 0.035f), cables[k % 3]);
        }
        // the cable tray and the orange trunk on the roof
        s.Box("white", Vector3(-hx * 0.8f, f.sy, -hz * 0.4f), Vector3(hx * 0.8f, f.sy + 0.04f, hz * 0.4f), Vector3(0.4f, 0.4f, 0.42f));
        s.Box("white", Vector3(-hx * 0.5f, f.sy + 0.04f, -0.04f), Vector3(hx * 0.5f, f.sy + 0.1f, 0.04f), Vector3(0.95f, 0.45f, 0.1f));
        break;
    }
    case FixtureType::Car: {
        // Local +z is the nose. Body, greenhouse, bumpers, lights, wheels, mirrors and a number plate.
        const float length = f.sz, h = f.sy;
        const Vector3 glass(0.12f, 0.17f, 0.24f);
        const float bodyTop = h * 0.62f;
        s.Box("white", Vector3(-hx, 0.28f, -hz), Vector3(hx, bodyTop, hz), c);
        s.Box("white", Vector3(-hx - 0.02f, 0.22f, -hz - 0.06f), Vector3(hx + 0.02f, 0.5f, -hz + 0.12f), dark);   // bumpers
        s.Box("white", Vector3(-hx - 0.02f, 0.22f, hz - 0.12f), Vector3(hx + 0.02f, 0.5f, hz + 0.06f), dark);
        const float bottom[4] = {-hx + 0.06f, -hz + 0.55f, hx - 0.06f, hz - 1.15f};
        const float top[4] = {-hx + 0.26f, -hz + 1.05f, hx - 0.26f, hz - 1.85f};
        s.Frustum("white", bodyTop, h, bottom, top, glass, c * 0.92f);
        for (const float x : {-1.0f, 1.0f}) {
            s.Box("white", Vector3(x * hx - 0.04f, bodyTop - 0.1f, hz - 1.2f), Vector3(x * hx + 0.04f, bodyTop + 0.04f, hz - 1.05f), dark);               // mirror stalks
            s.Box("white", Vector3(x * (hx - 0.1f) - 0.01f, 0.58f, hz - 0.01f), Vector3(x * (hx - 0.1f) + 0.01f, 0.64f, hz + 0.03f), Vector3(1.0f, 0.97f, 0.8f), 1.0f);
        }
        for (const float x : {-0.62f, 0.62f}) {
            s.Box("white", Vector3(x - 0.2f, 0.56f, hz - 0.01f), Vector3(x + 0.2f, 0.7f, hz + 0.02f), Vector3(1.0f, 0.97f, 0.82f), 1.0f);               // headlights
            s.Box("white", Vector3(x - 0.2f, 0.56f, -hz - 0.02f), Vector3(x + 0.2f, 0.68f, -hz + 0.01f), Vector3(0.9f, 0.1f, 0.08f), 0.6f);              // tail lights
        }
        s.Box("white", Vector3(-0.26f, 0.38f, -hz - 0.05f), Vector3(0.26f, 0.5f, -hz - 0.02f), Vector3(0.92f, 0.92f, 0.88f));                           // plate
        for (const float z : {-hz + 0.8f, hz - 0.85f})
            for (const float x : {-hx, hx}) s.Wheel(x, 0.32f, z, 0.32f, 0.22f, Vector3(0.07f, 0.07f, 0.08f), Vector3(0.62f, 0.64f, 0.68f));
        (void)length;
        break;
    }
    case FixtureType::Plant: {
        const Vector3 base = Vector3::Transform(Vector3(0, 0, 0), s.world);
        s.B("white").AddCylinder(base, f.sx * 0.4f, f.sy * 0.3f, 10, 0.0f, Vector3(0.6f, 0.35f, 0.25f));
        s.B("leaves").AddSphere(base + Vector3(0, f.sy * 0.65f, 0), f.sx * 0.9f, 10, 6, Vector3(0.4f, 0.65f, 0.3f));
        break;
    }
    case FixtureType::TreeTrunk: {
        // Three species, chosen from the tree's position: a broadleaf with a lumpy crown, a conifer of stacked
        // cones and a narrow poplar. The trunk and every branch are separate shapes so nothing is a lollipop.
        const Vector3 base = Vector3::Transform(Vector3(0, 0, 0), s.world);
        const float species = s.rng.Uniform();
        const float tone = 0.85f + 0.3f * s.rng.Uniform();
        const Vector3 bark(0.8f, 0.75f, 0.7f);
        if (species < 0.5f) {
            const float trunk = f.sy * 0.5f;
            s.B("bark").AddCone(base, f.sx * 0.62f, f.sx * 0.38f, trunk, 8, bark);
            const float r = f.sy * 0.3f;
            for (int k = 0; k < 3; ++k) {   // limbs spreading into the crown
                const float a = static_cast<float>(k) * 2.1f + s.rng.Uniform();
                AddBar(s.B("bark"), base + Vector3(0, trunk * 0.8f, 0), base + Vector3(std::cos(a) * r * 0.55f, trunk + r * 0.5f, std::sin(a) * r * 0.55f), f.sx * 0.28f, bark);
            }
            const Vector3 centre = base + Vector3(0, trunk + r * 0.55f, 0);
            s.B("leaves").AddSphere(centre, r, 10, 7, Vector3(0.5f, 0.78f, 0.42f) * tone);
            for (int k = 0; k < 5; ++k) {
                const float a = static_cast<float>(k) * 1.26f + s.rng.Uniform() * 0.6f;
                const float rr = r * (0.55f + 0.25f * s.rng.Uniform());
                s.B("leaves").AddSphere(centre + Vector3(std::cos(a) * r * 0.75f, (s.rng.Uniform() - 0.35f) * r * 0.7f, std::sin(a) * r * 0.75f), rr, 8, 6,
                                        Vector3(0.45f + 0.1f * s.rng.Uniform(), 0.72f + 0.1f * s.rng.Uniform(), 0.38f) * tone);
            }
        } else if (species < 0.78f) {
            s.B("bark").AddCone(base, f.sx * 0.6f, f.sx * 0.45f, f.sy * 0.22f, 8, bark);
            float y = f.sy * 0.14f, r = f.sy * 0.27f;
            for (int k = 0; k < 4; ++k) {
                const float h = f.sy * 0.3f;
                s.B("leaves").AddCone(base + Vector3(0, y, 0), r, r * 0.12f, h, 10, Vector3(0.2f, 0.45f, 0.27f) * tone);
                y += h * 0.55f;
                r *= 0.76f;
            }
        } else {
            s.B("bark").AddCone(base, f.sx * 0.55f, f.sx * 0.4f, f.sy * 0.2f, 8, bark);
            float y = f.sy * 0.12f;
            for (int k = 0; k < 3; ++k) {
                const float h = f.sy * 0.36f;
                s.B("leaves").AddCone(base + Vector3(0, y, 0), f.sy * (0.12f - 0.015f * static_cast<float>(k)), f.sy * (0.13f - 0.03f * static_cast<float>(k)), h, 9,
                                      Vector3(0.42f, 0.66f, 0.3f) * tone);
                y += h * 0.8f;
            }
            s.B("leaves").AddCone(base + Vector3(0, y, 0), f.sy * 0.1f, 0.02f, f.sy * 0.26f, 9, Vector3(0.45f, 0.7f, 0.32f) * tone);
        }
        break;
    }
    case FixtureType::Hoop: {
        s.Box("white", Vector3(-0.07f, 0, 0.35f), Vector3(0.07f, f.sy, 0.49f), metal);
        s.Box("white", Vector3(-0.05f, f.sy - 0.4f, 0.0f), Vector3(0.05f, f.sy - 0.3f, 0.42f), metal);
        s.Box("white", Vector3(-hx * 0.5f, f.sy - 1.05f, 0.0f), Vector3(hx * 0.5f, f.sy, 0.05f), kWhite);
        s.Box("white", Vector3(-0.23f, f.sy - 0.9f, -0.45f), Vector3(0.23f, f.sy - 0.87f, -0.42f), Vector3(1.0f, 0.4f, 0.1f));
        s.Box("white", Vector3(-0.23f, f.sy - 0.9f, -0.03f), Vector3(0.23f, f.sy - 0.87f, 0.0f), Vector3(1.0f, 0.4f, 0.1f));
        for (const float x : {-0.23f, 0.2f}) s.Box("white", Vector3(x, f.sy - 0.9f, -0.45f), Vector3(x + 0.03f, f.sy - 0.87f, 0.0f), Vector3(1.0f, 0.4f, 0.1f));
        break;
    }
    case FixtureType::Stage: {
        s.B("floor_wood").AddBoxTransformed(s.world, Vector3(-hx, 0, -hz), Vector3(hx, f.sy, hz), 1.5f, Vector3(0.4f, 0.3f, 0.25f), BoxFaces::NoBottom, 3.0f);
        for (int i = 0; i < 3; ++i) {
            const float h = f.sy * static_cast<float>(i + 1) / 4.0f;
            s.Box("white", Vector3(-1.0f, 0, -hz - 0.3f * static_cast<float>(3 - i)), Vector3(1.0f, h, -hz - 0.3f * static_cast<float>(2 - i)), Vector3(0.35f, 0.28f, 0.24f));
        }
        break;
    }
    case FixtureType::TheatreSeats: {
        s.Box("white", Vector3(-hx, 0.1f, -hz), Vector3(hx, 0.45f, hz), c * 0.9f);
        s.Box("white", Vector3(-hx, 0.45f, hz - 0.1f), Vector3(hx, 0.95f, hz), c);
        for (float x = -hx; x <= hx + 0.01f; x += 0.55f) s.Box("white", Vector3(x - 0.025f, 0.45f, -hz), Vector3(x + 0.025f, 0.65f, hz), dark);
        break;
    }
    case FixtureType::Sofa: {
        s.Box("white", Vector3(-hx, 0.05f, -hz), Vector3(hx, 0.45f, hz), c);
        s.Box("white", Vector3(-hx, 0.45f, hz - 0.22f), Vector3(hx, 0.85f, hz), c);
        for (const float x : {-hx, hx - 0.18f}) s.Box("white", Vector3(x, 0.45f, -hz), Vector3(x + 0.18f, 0.62f, hz), c * 0.92f);
        break;
    }
    case FixtureType::Machine: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy * 0.65f, hz), c);
        s.Box("white", Vector3(-hx * 0.6f, f.sy * 0.65f, -hz * 0.5f), Vector3(hx * 0.6f, f.sy, hz * 0.5f), metal);
        s.B("white").AddCylinder(Vector3::Transform(Vector3(hx * 0.2f, f.sy, 0), s.world), 0.12f, 0.08f, 12, 0.0f, dark);
        s.Box("white", Vector3(-hx + 0.05f, f.sy * 0.3f, -hz - 0.01f), Vector3(-hx + 0.12f, f.sy * 0.3f + 0.05f, -hz), Vector3(1.0f, 0.2f, 0.1f), 1.0f);
        break;
    }
    case FixtureType::Bed: {
        s.Box("white", Vector3(-hx, 0.25f, -hz), Vector3(hx, 0.4f, hz), metal);
        s.Legs(f.sx, f.sz, 0.25f, 0.04f, metal, 0.04f);
        s.Box("white", Vector3(-hx + 0.03f, 0.4f, -hz + 0.03f), Vector3(hx - 0.03f, f.sy, hz - 0.03f), kWhite);
        s.Box("white", Vector3(-hx + 0.1f, f.sy, hz - 0.5f), Vector3(hx - 0.1f, f.sy + 0.1f, hz - 0.1f), Vector3(0.95f, 0.95f, 0.98f));
        break;
    }
    case FixtureType::Vending: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy, hz), c);
        s.Front("glass", -hx + 0.08f, 0.7f, hx - 0.08f, f.sy - 0.15f, -hz - 0.004f, Vector3(0.6f, 0.75f, 0.9f), 0.6f, true, 0.7f);
        s.Box("white", Vector3(-hx + 0.1f, 0.2f, -hz - 0.01f), Vector3(hx - 0.1f, 0.5f, -hz), dark);
        break;
    }
    case FixtureType::BikeRack: {
        for (float x = -hx; x <= hx + 0.01f; x += 0.6f) s.Box("white", Vector3(x - 0.02f, 0, -0.02f), Vector3(x + 0.02f, f.sy, 0.02f), metal);
        s.Box("white", Vector3(-hx - 0.02f, f.sy - 0.04f, -0.02f), Vector3(hx + 0.02f, f.sy, 0.02f), metal);
        for (float x = -hx + 0.3f; x < hx; x += 0.6f)
            if (s.rng.Uniform() < 0.6f) s.Box("white", Vector3(x - 0.03f, 0.1f, -0.85f), Vector3(x + 0.03f, 0.7f, 0.85f), Palette(3 + s.rng.Below(5)));
        break;
    }
    case FixtureType::Goal: {
        for (const float x : {-hx, hx - 0.1f}) s.Box("white", Vector3(x, 0, -0.05f), Vector3(x + 0.1f, f.sy, 0.05f), kWhite);
        s.Box("white", Vector3(-hx, f.sy - 0.1f, -0.05f), Vector3(hx, f.sy, 0.05f), kWhite);
        s.Front("white", -hx, 0.0f, hx, f.sy, f.sz, Vector3(0.9f, 0.9f, 0.9f), 0.3f, true, 0.35f);
        break;
    }
    case FixtureType::Lamp: {
        // A post with a head; the glow at night is drawn by WorldRenderer::DrawLamps.
        s.B("white").AddCylinder(Vector3::Transform(Vector3(0, 0, 0), s.world), 0.06f, f.sy - 0.3f, 8, 0.0f, Vector3(0.3f, 0.32f, 0.35f), false);
        s.Box("white", Vector3(-0.22f, f.sy - 0.3f, -0.22f), Vector3(0.22f, f.sy, 0.22f), Vector3(0.35f, 0.36f, 0.4f));
        s.Box("white", Vector3(-0.18f, f.sy - 0.29f, -0.18f), Vector3(0.18f, f.sy - 0.25f, 0.18f), Vector3(1.0f, 0.95f, 0.8f), 1.0f);
        break;
    }
    case FixtureType::Box:
    default: {
        s.Box("white", Vector3(-hx, 0, -hz), Vector3(hx, f.sy, hz), c);
        break;
    }
    }
}

// --- stairs ------------------------------------------------------------------------------------------------

/** A bar along a 3D segment (handrails, sloped stringers). */
void AddBar(MeshBuilder& b, const Vector3& from, const Vector3& to, float thickness, const Vector3& tint)
{
    const Vector3 d = to - from;
    const float len = d.Length();
    if (len < 1e-4f) return;
    const Vector3 x = d / len;
    const Vector3 helper = std::abs(x.Y) > 0.95f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
    const Vector3 z = Vector3::Normalize(Vector3::Cross(helper, x));
    const Vector3 y = Vector3::Cross(z, x);
    const Matrix m(x.X, x.Y, x.Z, 0, y.X, y.Y, y.Z, 0, z.X, z.Y, z.Z, 0, from.X, from.Y, from.Z, 1);
    const float t = thickness * 0.5f;
    b.AddBoxTransformed(m, Vector3(0, -t, -t), Vector3(len, t, t), 0.0f, tint, BoxFaces::All, 10.0f);
}

void AddStairs(const SchoolPlan& plan, const Room& room, MeshSet& set)
{
    const School::StairShaft& s = plan.stairs[static_cast<std::size_t>(room.stairId)];
    const School::StairFrame f{s.footprint, s.entrySide};
    const int level = room.level;
    const float base = plan.FloorY(level);
    const float W = f.W(), D = f.D();
    const float Le = s.entryDepth;
    const float run = D - Le - s.midDepth;
    const float half = School::kFloorHeight * 0.5f;
    const float fw = s.flightWidth;
    const Vector3 stepTint(0.62f, 0.62f, 0.64f);
    const Vector3 railTint(0.25f, 0.3f, 0.5f);
    MeshBuilder& concrete = set.Opaque("floor_concrete");
    MeshBuilder& rail = set.Opaque("white");
    const auto worldBox = [&](const Rect& r, float y0, float y1, MeshBuilder& b, const Vector3& tint, float tex = 1.0f) {
        b.AddBox(Vector3(r.x0, y0, r.z0), Vector3(r.x1, y1, r.z1), tex, tint, BoxFaces::All, 4.0f);
    };
    // Entry landing floor (the rest of the room is the stair itself and the open well).
    {
        const Rect e = f.World(0, W, 0, Le);
        set.Opaque("floor_concrete").AddPatch(Vector3(e.x0, base, e.z1), Vector3(e.Width(), 0, 0), Vector3(0, 0, -e.Depth()), 2.0f, Vector3(0.8f, 0.8f, 0.82f), 2.0f);
    }
    if (level >= s.levelHigh) return;
    const int steps = std::max(6, static_cast<int>(std::lround(half / 0.17f)));
    const auto flight = [&](float u0, float u1, float yFrom, float yTo) {
        for (int i = 0; i < steps; ++i) {
            const float v0 = Le + run * static_cast<float>(i) / static_cast<float>(steps);
            const float v1 = Le + run * static_cast<float>(i + 1) / static_cast<float>(steps);
            const float yTop = yFrom + (yTo - yFrom) * static_cast<float>(i + 1) / static_cast<float>(steps);
            const float yBottom = yFrom + (yTo - yFrom) * static_cast<float>(i) / static_cast<float>(steps) - 0.3f;
            worldBox(f.World(u0, u1, v0, v1), std::min(yBottom, yTop - 0.05f), yTop, concrete, stepTint, 0.0f);
        }
    };
    flight(0.0f, fw, base, base + half);                         // flight 1, up to the half landing
    flight(W - fw, W, base + half, base + School::kFloorHeight);  // flight 2, up to the next level
    worldBox(f.World(0, W, Le + run, D), base + half - 0.3f, base + half, concrete, stepTint);   // half landing
    // Handrails along the inner edges of both flights and across the well on the entry landing.
    const auto p = [&](float u, float v, float y) { return Vector3(f.Point(u, v).x, y, f.Point(u, v).z); };
    const float rh = 1.0f;
    AddBar(rail, p(fw, Le, base + rh), p(fw, Le + run, base + half + rh), 0.05f, railTint);
    AddBar(rail, p(W - fw, Le + run, base + half + rh), p(W - fw, Le, base + School::kFloorHeight + rh), 0.05f, railTint);
    AddBar(rail, p(fw, Le + run, base + half + rh), p(W - fw, Le + run, base + half + rh), 0.05f, railTint);
    AddBar(rail, p(fw, Le, base + rh), p(W - fw, Le, base + rh), 0.05f, railTint);
    for (int i = 0; i <= 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        AddBar(rail, p(fw, Le + run * t, base + half * t), p(fw, Le + run * t, base + half * t + rh), 0.03f, railTint);
        AddBar(rail, p(W - fw, Le + run * (1.0f - t), base + half + half * t), p(W - fw, Le + run * (1.0f - t), base + half + half * t + rh), 0.03f, railTint);
    }
    for (int i = 0; i <= 3; ++i) {
        const float u = fw + (W - 2.0f * fw) * static_cast<float>(i) / 3.0f;
        AddBar(rail, p(u, Le, base), p(u, Le, base + rh), 0.03f, railTint);
        AddBar(rail, p(u, Le + run, base + half), p(u, Le + run, base + half + rh), 0.03f, railTint);
    }
}

}  // namespace

// --- public -------------------------------------------------------------------------------------------------

Vector3 PaletteColor(int index)
{
    static const Vector3 kPalette[] = {
        Vector3(0.62f, 0.45f, 0.28f),   // wood
        Vector3(0.85f, 0.78f, 0.62f),   // light wood
        Vector3(0.33f, 0.24f, 0.18f),   // dark wood
        Vector3(0.25f, 0.42f, 0.78f),   // blue
        Vector3(0.78f, 0.22f, 0.2f),    // red
        Vector3(0.25f, 0.6f, 0.32f),    // green
        Vector3(0.92f, 0.76f, 0.2f),    // yellow
        Vector3(0.55f, 0.56f, 0.6f),    // grey
        Vector3(0.95f, 0.95f, 0.95f),   // white
        Vector3(0.68f, 0.7f, 0.73f),    // metal
    };
    return kPalette[static_cast<std::size_t>(index) % 10];
}

std::string FloorTextureOf(const SchoolPlan& plan, const Room& room)
{
    switch (room.type) {
    case RoomType::Corridor: return plan.wings[static_cast<std::size_t>(room.wing)].corridorFloor;
    case RoomType::Lobby:
    case RoomType::Cafeteria:
    case RoomType::Reception: return "floor_terrazzo";
    case RoomType::ArtStudio:
    case RoomType::Workshop:
    case RoomType::DramaStudio: return "floor_wood";
    case RoomType::MusicRoom:
    case RoomType::Library:
    case RoomType::Auditorium:
    case RoomType::AdminOffice:
    case RoomType::PrincipalOffice:
    case RoomType::StaffRoom:
    case RoomType::MeetingRoom:
    case RoomType::Counseling:
    case RoomType::SecurityOffice: return "floor_carpet";
    case RoomType::Gym: return "gym_court";
    case RoomType::Toilets:
    case RoomType::ChangingRoom:
    case RoomType::Kitchen:
    case RoomType::Nurse: return "wall_tile";
    case RoomType::Storage:
    case RoomType::LabPrep:
    case RoomType::Boiler:
    case RoomType::Electrical:
    case RoomType::Janitor:
    case RoomType::Archive:
    case RoomType::EquipmentStore:
    case RoomType::ServerRoom:
    case RoomType::StairWell:
    case RoomType::LockerHall:
    case RoomType::FitnessRoom: return "floor_concrete";
    case RoomType::Courtyard:
    case RoomType::BikeArea: return "floor_paving";
    case RoomType::Plaza: return room.name.find("lawn") != std::string::npos || room.name.find("terrace") != std::string::npos ? "floor_grass" : "floor_paving";
    case RoomType::Playground: return "floor_sand";
    case RoomType::Track: return "floor_track";
    case RoomType::FootballField: return "pitch";
    case RoomType::Parking:
    case RoomType::Street: return "floor_asphalt";
    case RoomType::AssemblyPoint: return "floor_grass";
    default: return "floor_tile";
    }
}

RoomGeometry BuildRoomGeometry(const SchoolPlan& plan, const School::LayoutCache& layouts, RoomId id)
{
    RoomGeometry g;
    const Room& room = plan.room(id);
    const Rect& r = room.rect;
    const float y0 = plan.FloorY(room.level);
    const School::Wing& wing = plan.wings[static_cast<std::size_t>(room.wing)];
    g.outdoor = !room.Indoor();
    MeshSet& set = g.meshes;

    const bool shaft = room.type == RoomType::ElevatorCab;
    const School::ElevatorShaft* lift = shaft ? &plan.elevators[static_cast<std::size_t>(room.elevatorId)] : nullptr;
    // --- floor ---------------------------------------------------------------------------------------------
    const std::string floorTex = FloorTextureOf(plan, room);
    const float floorScale = room.type == RoomType::Gym ? 0.0f : (g.outdoor ? 4.0f : 2.0f);
    Vector3 floorTint = kWhite;
    if (room.type == RoomType::Corridor || room.type == RoomType::Lobby) floorTint = Vector3(0.9f, 0.9f, 0.9f);
    if (room.type == RoomType::AssemblyPoint) floorTint = Vector3(0.85f, 0.95f, 0.8f);
    if (!shaft || room.level == lift->levelLow)   // a shaft has no floor but its pit
        set.Opaque(floorTex).AddPatch(Vector3(r.x0, y0 + (g.outdoor ? 0.01f : 0.0f) - (shaft ? 1.2f : 0.0f), r.z1), Vector3(r.Width(), 0, 0), Vector3(0, 0, -r.Depth()), floorScale, shaft ? Vector3(0.3f, 0.3f, 0.32f) : floorTint,
                                      g.outdoor ? 6.0f : 3.0f);
    if (room.type == RoomType::Gym && floorScale == 0.0f) {}   // the court texture is mapped once over the whole hall

    if (g.outdoor) {
        // Flat sunlight: the daylight tint is applied at draw time; bake only a mild directional term.
        g.ambient = Vector3(0.62f, 0.62f, 0.62f);
        BakedLight sun;
        sun.position = Vector3(r.Center().x - 300.0f, 500.0f, r.Center().z + 200.0f);
        sun.color = Vector3(1, 0.98f, 0.95f);
        sun.intensity = 0.55f;
        sun.radius = 5000.0f;
        g.lights.push_back(sun);
        // Assembly point markings: a bright border.
        if (room.type == RoomType::AssemblyPoint) {
            MeshBuilder& b = set.Opaque("white");
            const float t = 0.12f;
            b.AddBox(Vector3(r.x0, y0 + 0.012f, r.z0), Vector3(r.x1, y0 + 0.02f, r.z0 + t), 0.0f, Vector3(0.95f, 0.9f, 0.2f), BoxFaces::PosY);
            b.AddBox(Vector3(r.x0, y0 + 0.012f, r.z1 - t), Vector3(r.x1, y0 + 0.02f, r.z1), 0.0f, Vector3(0.95f, 0.9f, 0.2f), BoxFaces::PosY);
            b.AddBox(Vector3(r.x0, y0 + 0.012f, r.z0), Vector3(r.x0 + t, y0 + 0.02f, r.z1), 0.0f, Vector3(0.95f, 0.9f, 0.2f), BoxFaces::PosY);
            b.AddBox(Vector3(r.x1 - t, y0 + 0.012f, r.z0), Vector3(r.x1, y0 + 0.02f, r.z1), 0.0f, Vector3(0.95f, 0.9f, 0.2f), BoxFaces::PosY);
        }
        if (room.type == RoomType::Track) {
            MeshBuilder& b = set.Opaque("white");
            const bool alongX = r.Width() > r.Depth();
            for (int lane = 1; lane < 6; ++lane) {
                const float t = static_cast<float>(lane) / 6.0f;
                if (alongX) b.AddBox(Vector3(r.x0, y0 + 0.012f, r.z0 + r.Depth() * t - 0.04f), Vector3(r.x1, y0 + 0.016f, r.z0 + r.Depth() * t + 0.04f), 0.0f, kWhite, BoxFaces::PosY, 20.0f);
                else b.AddBox(Vector3(r.x0 + r.Width() * t - 0.04f, y0 + 0.012f, r.z0), Vector3(r.x0 + r.Width() * t + 0.04f, y0 + 0.016f, r.z1), 0.0f, kWhite, BoxFaces::PosY, 20.0f);
            }
        }
        if (room.type == RoomType::Parking) {
            MeshBuilder& b = set.Opaque("white");
            for (float x = r.x0 + 4.5f; x < r.x1 - 4.5f; x += 3.0f)
                for (int row = 0; row < 2; ++row) {
                    const float z = r.z0 + 6.0f + static_cast<float>(row) * 22.0f;
                    b.AddBox(Vector3(x - 0.05f, y0 + 0.012f, z - 2.6f), Vector3(x + 0.05f, y0 + 0.016f, z + 2.6f), 0.0f, kWhite, BoxFaces::PosY, 20.0f);
                }
        }
        if (room.type == RoomType::FootballField) {
            MeshBuilder& b = set.Opaque("white");
            const float m = 2.0f;
            b.AddBox(Vector3(r.x0 + m, y0 + 0.012f, r.z0 + m), Vector3(r.x1 - m, y0 + 0.016f, r.z0 + m + 0.1f), 0.0f, kWhite, BoxFaces::PosY, 20.0f);
            b.AddBox(Vector3(r.x0 + m, y0 + 0.012f, r.z1 - m - 0.1f), Vector3(r.x1 - m, y0 + 0.016f, r.z1 - m), 0.0f, kWhite, BoxFaces::PosY, 20.0f);
            b.AddBox(Vector3(r.x0 + m, y0 + 0.012f, r.z0 + m), Vector3(r.x0 + m + 0.1f, y0 + 0.016f, r.z1 - m), 0.0f, kWhite, BoxFaces::PosY, 20.0f);
            b.AddBox(Vector3(r.x1 - m - 0.1f, y0 + 0.012f, r.z0 + m), Vector3(r.x1 - m, y0 + 0.016f, r.z1 - m), 0.0f, kWhite, BoxFaces::PosY, 20.0f);
            b.AddBox(Vector3(r.Center().x - 0.05f, y0 + 0.012f, r.z0 + m), Vector3(r.Center().x + 0.05f, y0 + 0.016f, r.z1 - m), 0.0f, kWhite, BoxFaces::PosY, 20.0f);
        }
        // Low fence posts around the campus boundary zones so the edge of the world reads as an edge.
        const RoomLayout& layout = layouts.Get(id);
        for (const Fixture& f : layout.fixtures) AddFixture(set, f, y0, id);
        return g;
    }

    // --- indoor: lights ------------------------------------------------------------------------------------
    const float height = room.type == RoomType::StairWell || shaft ? School::kFloorHeight : room.Height();
    const float ceilY = y0 + height;
    g.ambient = shaft ? Vector3(0.2f, 0.2f, 0.22f) : Vector3(0.42f, 0.42f, 0.45f);
    if (shaft) {
        // Dark shaft: one weak lamp per level, guide rails on the back wall, the cab itself is drawn by WorldRenderer.
        BakedLight l;
        l.position = Vector3(r.Center().x, ceilY - 0.2f, r.Center().z);
        l.color = Vector3(0.9f, 0.9f, 1.0f);
        l.intensity = 0.3f;
        l.radius = 5.0f;
        g.lights.push_back(l);
        const Vec2 n = School::SideNormal(lift->entrySide);
        const Vec2 back = r.Center() - n * (std::max(r.Width(), r.Depth()) * 0.5f - 0.08f);
        const Vec2 along = n.RightHand();
        for (const float side : {-0.9f, 0.9f}) {
            const Vec2 p = back + along * side;
            set.Opaque("white").AddBox(Vector3(p.x - 0.05f, y0 - (room.level == lift->levelLow ? 1.2f : 0.0f), p.z - 0.05f), Vector3(p.x + 0.05f, ceilY, p.z + 0.05f), 0.0f, Vector3(0.5f, 0.52f, 0.55f), BoxFaces::Sides, 4.0f);
        }
        // The hall call button on the corridor side of the door wall, with a lit dot.
        for (const DoorId did : room.doors) {
            const Door& d = plan.door(did);
            const Vec2 out = n;   // the entry side's normal points out of the shaft into the corridor
            const Vec2 at = d.pos + out * 0.02f + out.RightHand() * (d.width * 0.5f + 0.35f);
            const Vector3 lo(std::min(at.x - 0.07f, at.x + out.x * 0.04f - 0.07f), y0 + 1.0f, std::min(at.z - 0.07f, at.z + out.z * 0.04f - 0.07f));
            const Vector3 hi(std::max(at.x + 0.07f, at.x + out.x * 0.04f + 0.07f), y0 + 1.25f, std::max(at.z + 0.07f, at.z + out.z * 0.04f + 0.07f));
            set.Opaque("white").AddBox(lo, hi, 0.0f, Vector3(0.75f, 0.76f, 0.8f), BoxFaces::All, 4.0f, 0.7f);   // bright: the shaft's own light is dim
            const Vector3 dot = (lo + hi) * 0.5f + Vector3(out.x, 0, out.z) * 0.05f;
            set.Opaque("white").AddBox(dot - Vector3(0.025f, 0.025f, 0.025f), dot + Vector3(0.025f, 0.025f, 0.025f), 0.0f, Vector3(1.0f, 0.6f, 0.1f), BoxFaces::All, 4.0f, 1.0f);
        }
    } else {
        const float spacing = room.type == RoomType::Corridor ? 5.0f : 4.2f;
        const int nx = std::max(1, static_cast<int>(std::lround(r.Width() / spacing)));
        const int nz = std::max(1, static_cast<int>(std::lround(r.Depth() / spacing)));
        MeshBuilder& lamps = set.Opaque("white");
        for (int i = 0; i < nx; ++i)
            for (int j = 0; j < nz; ++j) {
                const float x = r.x0 + r.Width() * (static_cast<float>(i) + 0.5f) / static_cast<float>(nx);
                const float z = r.z0 + r.Depth() * (static_cast<float>(j) + 0.5f) / static_cast<float>(nz);
                BakedLight l;
                l.position = Vector3(x, ceilY - 0.3f, z);
                l.color = room.type == RoomType::Auditorium ? Vector3(1.0f, 0.85f, 0.7f) : Vector3(1.0f, 0.97f, 0.9f);
                l.intensity = room.levels > 1 ? 0.9f : 0.62f;
                l.radius = room.levels > 1 ? 14.0f : 8.0f;
                g.lights.push_back(l);
                if (room.type != RoomType::StairWell || room.level == plan.stairs[static_cast<std::size_t>(room.stairId)].levelHigh)
                    lamps.AddBox(Vector3(x - 0.6f, ceilY - 0.06f, z - 0.15f), Vector3(x + 0.6f, ceilY - 0.005f, z + 0.15f), 0.0f, Vector3(1.0f, 0.98f, 0.92f), BoxFaces::NoTop, 4.0f, 1.0f);
            }
    }

    if (room.type == RoomType::Auditorium || room.type == RoomType::DramaStudio) {
        // Coloured stage lights over the stage end (the far wall of the room's frame).
        const RoomLayout& l0 = layouts.Get(id);
        for (const Fixture& f : l0.fixtures) {
            if (f.type != FixtureType::Stage) continue;
            static const Vector3 kGels[] = {Vector3(1.0f, 0.3f, 0.2f), Vector3(0.3f, 0.5f, 1.0f), Vector3(1.0f, 0.85f, 0.4f), Vector3(0.4f, 1.0f, 0.5f)};
            const float c = std::cos(f.yaw), sn = std::sin(f.yaw);
            for (int i = 0; i < 4; ++i) {
                const float u = (static_cast<float>(i) - 1.5f) * f.sx * 0.22f;
                BakedLight l;
                l.position = Vector3(f.pos.x + c * u, ceilY - 0.8f, f.pos.z - sn * u);
                l.color = kGels[i];
                l.intensity = 1.1f;
                l.radius = 9.0f;
                g.lights.push_back(l);
                set.Opaque("white").AddBox(l.position + Vector3(-0.15f, -0.15f, -0.15f), l.position + Vector3(0.15f, 0.15f, 0.15f), 0.0f, kGels[i], BoxFaces::All, 4.0f, 1.0f);
            }
        }
    }


    // --- ceiling -------------------------------------------------------------------------------------------
    const bool topOfShaft = (room.type != RoomType::StairWell || room.level == plan.stairs[static_cast<std::size_t>(room.stairId)].levelHigh) && (!shaft || room.level == lift->levelHigh);
    if (topOfShaft) {
        const char* ceilTex = room.levels > 1 ? "wall_concrete" : "ceiling_tile";
        const Vector3 ceilTint = room.levels > 1 ? Vector3(0.5f, 0.5f, 0.52f) : kWhite;
        set.Opaque(ceilTex).AddPatch(Vector3(r.x0, room.type == RoomType::StairWell ? y0 + room.Height() : ceilY, r.z0), Vector3(r.Width(), 0, 0), Vector3(0, 0, r.Depth()), 1.2f, ceilTint, 3.0f);
    }

    // --- walls ----------------------------------------------------------------------------------------------
    const std::string wallTex = WallTextureOf(room);
    const Vector3 wallTint = WallTintOf(plan, room);
    const bool stripes = room.type == RoomType::Corridor || room.type == RoomType::Lobby;
    for (int sIdx = 0; sIdx < 4; ++sIdx) {
        const Side side = static_cast<Side>(sIdx);
        Vector3 origin, u;
        float length;
        InnerWallFrame(r, side, origin, u, length);
        origin.Y = y0;
        std::vector<Opening> doors;
        for (const DoorId did : room.doors) {
            const Door& d = plan.door(did);
            if (SideOfDoor(d, id) != side) continue;
            const float c = AlongWall(r, side, d.pos);
            const float h = d.kind == DoorKind::Opening ? (room.Indoor() && plan.room(d.Other(id)).Indoor() ? std::min(height, 2.6f) : height) : School::kDoorHeight;
            doors.push_back({c - d.width * 0.5f, c + d.width * 0.5f, y0, y0 + h});
        }
        std::vector<Opening> openings = doors;
        const bool exterior = room.HasExterior(side) && room.level >= 1;
        std::vector<float> windows;
        if (exterior) windows = WindowCentres(length, doors);
        const int storeys = room.levels;
        for (const float c : windows)
            for (int st = 0; st < storeys; ++st) {
                const float sill = y0 + School::kFloorHeight * static_cast<float>(st) + kWindowSill;
                if (sill + kWindowHeight > ceilY - 0.1f) continue;
                openings.push_back({c - kWindowWidth * 0.5f, c + kWindowWidth * 0.5f, sill, sill + kWindowHeight});
            }
        AddWall(set.Opaque(wallTex), origin, u, length, y0, ceilY, openings, 2.0f, wallTint);
        const Vector3 n = Vector3::Cross(u, Vector3::Up);   // into the room
        // Window glass, sills and reveals.
        for (const Opening& o : openings) {
            if (o.y0 <= y0 + 0.01f) continue;   // a door
            MeshBuilder& glass = set.Transparent("glass");
            glass.SetAlpha(0.28f);
            glass.AddPatch(origin + u * o.lo + Vector3(0, o.y0 - y0, 0) - n * 0.1f, u * (o.hi - o.lo), Vector3(0, o.y1 - o.y0, 0), 0.0f, Vector3(0.75f, 0.85f, 1.0f), 10.0f, 0.6f);
            glass.SetAlpha(1.0f);
            MeshBuilder& frame = set.Opaque("white");
            const Vector3 ftint(0.9f, 0.9f, 0.9f);
            // sill and reveal boxes through the wall thickness (towards the facade)
            const Vector3 a = origin + u * o.lo, b = origin + u * o.hi;
            const Vector3 depth = n * -(kFacadeOffset + 0.05f);
            frame.AddPatch(Vector3(a.X, o.y0, a.Z) + n * 0.05f, u * (o.hi - o.lo), depth, 0.0f, ftint, 10.0f);                 // sill
            frame.AddPatch(Vector3(a.X, o.y1, a.Z) + n * 0.05f + depth, u * (o.hi - o.lo), -depth, 0.0f, ftint, 10.0f);        // head
            frame.AddPatch(Vector3(a.X, o.y0, a.Z) + n * 0.05f, depth, Vector3(0, o.y1 - o.y0, 0), 0.0f, ftint, 10.0f);        // jamb
            frame.AddPatch(Vector3(b.X, o.y0, b.Z) + n * 0.05f + depth, -depth, Vector3(0, o.y1 - o.y0, 0), 0.0f, ftint, 10.0f);
        }
        // Wing stripe along corridor walls, and a skirting board everywhere.
        if (stripes) {
            MeshBuilder& b = set.Opaque("white");
            std::vector<Opening> none;
            const Vector3 accent = Tint(wing.accent);
            float cursor = 0.0f;
            std::vector<Opening> sorted = doors;
            std::sort(sorted.begin(), sorted.end(), [](const Opening& x, const Opening& y) { return x.lo < y.lo; });
            const auto seg = [&](float a0, float a1) {
                if (a1 - a0 < 0.05f) return;
                b.AddPatch(origin + u * a0 + n * 0.012f + Vector3(0, 1.05f, 0), u * (a1 - a0), Vector3(0, 0.22f, 0), 0.0f, accent, 10.0f, 0.35f);
            };
            for (const Opening& d : sorted) {
                seg(cursor, std::max(cursor, d.lo));
                cursor = std::max(cursor, d.hi);
            }
            seg(cursor, length);
        }
        // Posters in teaching rooms on the walls without the door.
        if (School::Info(room.type).teaching && doors.empty() && !exterior && length > 4.0f && room.type != RoomType::Gym) {
            const int count = std::min(3, static_cast<int>(length / 3.5f));
            for (int i = 0; i < count; ++i) {
                const float c = length * (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
                const int poster = 1 + static_cast<int>((static_cast<unsigned>(id) * 7u + static_cast<unsigned>(i) * 3u + static_cast<unsigned>(sIdx)) % 4u);
                const std::string tex = "poster_" + std::to_string(poster);
                set.Opaque(tex).AddPatch(origin + u * (c - 0.45f) + n * 0.01f + Vector3(0, 1.3f, 0), u * 0.9f, Vector3(0, 1.2f, 0), 0.0f, kWhite, 10.0f);
            }
        }
        // Light switches beside every door, inside the room, on the hinge side.
        if (!School::Info(room.type).circulation)
            for (const DoorId did : room.doors) {
                const Door& d = plan.door(did);
                if (SideOfDoor(d, id) != side || d.kind == DoorKind::Opening) continue;
                const float c = AlongWall(r, side, d.pos) + d.width * 0.5f + 0.25f;
                if (c > length - 0.1f) continue;
                MeshBuilder& b = set.Opaque("white");
                const Vector3 at = origin + u * c + n * 0.012f + Vector3(0, 1.15f, 0);
                b.AddBoxTransformed(Matrix::CreateTranslation(0, 0, 0), Vector3::Min(at - u * 0.04f - Vector3(0, 0.04f, 0), at + u * 0.04f + n * 0.012f + Vector3(0, 0.04f, 0)),
                                    Vector3::Max(at - u * 0.04f - Vector3(0, 0.04f, 0), at + u * 0.04f + n * 0.012f + Vector3(0, 0.04f, 0)), 0.0f, Vector3(0.95f, 0.95f, 0.93f));
            }
        // Door frames: jambs and a lintel, added by the room that is `a` of the door.
        for (const DoorId did : room.doors) {
            const Door& d = plan.door(did);
            if (d.a != id || SideOfDoor(d, id) != side || d.kind == DoorKind::Opening) continue;
            const float c = AlongWall(r, side, d.pos);
            MeshBuilder& b = set.Opaque("white");
            const Vector3 ft = d.kind == DoorKind::External || d.kind == DoorKind::Gate ? Vector3(0.3f, 0.3f, 0.32f) : Tint(wing.accent) * 0.9f;
            const float h = d.kind == DoorKind::Gate ? 1.6f : School::kDoorHeight;
            const bool throughFacade = d.exit && room.Indoor() && room.HasExterior(side);
            const float t = 0.16f, w = 0.08f;
            const Vector3 a = origin + u * (c - d.width * 0.5f);
            const Vector3 bpos = origin + u * (c + d.width * 0.5f);
            // An exit's frame reaches out to the facade so the gap between the two walls is closed.
            const Vector3 half = n * t;
            (void)throughFacade;
            const Vector3 outer = throughFacade ? n * -(kFacadeOffset + 0.05f) : -half;   // how far the frame goes away from the room
            b.AddBoxTransformed(Matrix::CreateTranslation(0, 0, 0), Vector3::Min(a - u * w + outer, a + half + Vector3(0, h + w, 0)), Vector3::Max(a - u * w + outer, a + half + Vector3(0, h + w, 0)), 0.0f, ft);
            b.AddBoxTransformed(Matrix::CreateTranslation(0, 0, 0), Vector3::Min(bpos + outer, bpos + u * w + half + Vector3(0, h + w, 0)), Vector3::Max(bpos + outer, bpos + u * w + half + Vector3(0, h + w, 0)), 0.0f, ft);
            b.AddBoxTransformed(Matrix::CreateTranslation(0, 0, 0), Vector3::Min(a + outer + Vector3(0, h, 0), bpos + half + Vector3(0, h + w, 0)), Vector3::Max(a + outer + Vector3(0, h, 0), bpos + half + Vector3(0, h + w, 0)), 0.0f, ft);
        }
    }

    // --- furniture and stairs ----------------------------------------------------------------------------------
    if (room.type == RoomType::StairWell) AddStairs(plan, room, set);
    const RoomLayout& layout = layouts.Get(id);
    for (const Fixture& f : layout.fixtures) AddFixture(set, f, y0, id);
    return g;
}

FacadeGeometry BuildFacadeGeometry(const SchoolPlan& plan)
{
    FacadeGeometry out;
    out.chunks.resize(plan.wings.size() + 1);
    const Vector3 brick(0.85f, 0.8f, 0.75f);
    for (const Room& room : plan.rooms) {
        if (!room.Indoor() || room.level < 1) continue;
        MeshSet& set = out.chunks[static_cast<std::size_t>(room.wing)];
        const Rect& r = room.rect;
        const float y0 = plan.FloorY(room.level);
        const float y1 = y0 + static_cast<float>(room.levels) * School::kFloorHeight;
        const School::Wing& wing = plan.wings[static_cast<std::size_t>(room.wing)];
        const Vector3 tint = Vector3(wing.wall[0], wing.wall[1], wing.wall[2]) * 0.5f + brick * 0.5f;
        for (int sIdx = 0; sIdx < 4; ++sIdx) {
            const Side side = static_cast<Side>(sIdx);
            if (!room.HasExterior(side)) continue;
            Vector3 origin, u;
            float length;
            InnerWallFrame(r, side, origin, u, length);
            const Vector3 n = Vector3::Cross(u, Vector3::Up);   // into the room
            // The facade faces out: start at the far end, run the other way, offset outwards.
            const Vector3 fo = origin + u * length - n * kFacadeOffset + Vector3(0, y0, 0);
            const Vector3 fu = -u;
            std::vector<Opening> doors;
            for (const DoorId did : room.doors) {
                const Door& d = plan.door(did);
                if (SideOfDoor(d, room.id) != side) continue;
                const float c = length - AlongWall(r, side, d.pos);
                doors.push_back({c - d.width * 0.5f, c + d.width * 0.5f, y0, y0 + School::kDoorHeight});
            }
            std::vector<Opening> openings = doors;
            std::vector<Opening> innerDoors;
            for (const Opening& d : doors) innerDoors.push_back({length - d.hi, length - d.lo, d.y0, d.y1});
            const std::vector<float> centres = WindowCentres(length, innerDoors);
            for (const float c0 : centres)
                for (int st = 0; st < room.levels; ++st) {
                    const float sill = y0 + School::kFloorHeight * static_cast<float>(st) + kWindowSill;
                    if (sill + kWindowHeight > y0 + room.Height() - 0.1f) continue;
                    const float c = length - c0;
                    openings.push_back({c - kWindowWidth * 0.5f, c + kWindowWidth * 0.5f, sill, sill + kWindowHeight});
                }
            AddWall(set.Opaque("wall_brick"), fo, fu, length, y0, y1, openings, 2.0f, tint, 4.0f);
            for (const Opening& o : openings) {
                if (o.y0 <= y0 + 0.01f) continue;
                MeshBuilder& glass = set.Transparent("glass");
                glass.SetAlpha(0.9f);
                glass.AddPatch(fo + fu * o.lo + Vector3(0, o.y0 - y0, 0) + n * 0.03f, fu * (o.hi - o.lo), Vector3(0, o.y1 - o.y0, 0), 0.0f, Vector3(0.35f, 0.45f, 0.6f), 10.0f, 0.5f);
                glass.SetAlpha(1.0f);
            }
            // A band at each floor slab.
            for (int st = 1; st < room.levels; ++st) {
                const float y = y0 + School::kFloorHeight * static_cast<float>(st);
                set.Opaque("white").AddPatch(fo + Vector3(0, y - 0.6f - y0, 0) - n * 0.02f, fu * length, Vector3(0, 0.6f, 0), 0.0f, tint * 0.8f, 10.0f);
            }
        }
        // Roof where nothing is above.
        const bool roomAbove = room.TopLevel() + 1 < plan.LevelCount() && plan.RoomAt(r.Center(), room.TopLevel() + 1) != School::kNone &&
                               plan.room(plan.RoomAt(r.Center(), room.TopLevel() + 1)).Indoor();
        if (!roomAbove) {
            MeshBuilder& roof = set.Opaque("roof");
            roof.AddPatch(Vector3(r.x0 - kFacadeOffset, y1, r.z1 + kFacadeOffset), Vector3(r.Width() + 2 * kFacadeOffset, 0, 0), Vector3(0, 0, -(r.Depth() + 2 * kFacadeOffset)), 3.0f,
                          Vector3(0.7f, 0.7f, 0.72f), 8.0f);
            for (int sIdx = 0; sIdx < 4; ++sIdx) {
                const Side side = static_cast<Side>(sIdx);
                if (!room.HasExterior(side)) continue;
                Vector3 origin, u;
                float length;
                InnerWallFrame(r, side, origin, u, length);
                const Vector3 n = Vector3::Cross(u, Vector3::Up);
                const Vector3 a = origin + Vector3(0, y1, 0) - n * kFacadeOffset;
                const Vector3 b = a + u * length;
                set.Opaque("white").AddBox(Vector3::Min(a, b + n * 0.3f + Vector3(0, 0.5f, 0)), Vector3::Max(a, b + n * 0.3f + Vector3(0, 0.5f, 0)), 0.0f, tint * 0.9f, BoxFaces::All, 10.0f);
            }
        }
    }
    // The ground: a grass plane well beyond the campus.
    {
        MeshSet& set = out.chunks.back();
        const Rect c = plan.campus.Inflated(400.0f);
        set.Opaque("floor_grass").AddPatch(Vector3(c.x0, -0.03f, c.z1), Vector3(c.Width(), 0, 0), Vector3(0, 0, -c.Depth()), 6.0f, Vector3(0.9f, 0.95f, 0.85f), 40.0f);
    }
    return out;
}
