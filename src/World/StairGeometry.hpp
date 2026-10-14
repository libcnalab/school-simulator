#pragma once

// The shape of a U-shaped stair, shared by the plan builder (walk surfaces and solids), navigation
// (the path an NPC climbs) and rendering (the steps). Local frame: u runs along the entry wall,
// v away from it. The entry landing is v in [0, entryDepth]; flight 1 climbs away from the entry wall
// along u in [0, flightWidth]; the half landing is at the far end; flight 2 returns along the other side.

#include <vector>

#include "World/SchoolPlan.hpp"

namespace School {

struct StairFrame {
    Rect footprint;
    Side entry = Side::South;

    [[nodiscard]] bool NS() const { return entry == Side::North || entry == Side::South; }
    [[nodiscard]] float W() const { return NS() ? footprint.Width() : footprint.Depth(); }
    [[nodiscard]] float D() const { return NS() ? footprint.Depth() : footprint.Width(); }

    [[nodiscard]] Rect World(float u0, float u1, float v0, float v1) const
    {
        const Rect& f = footprint;
        switch (entry) {
        case Side::North: return {f.x0 + u0, f.z0 + v0, f.x0 + u1, f.z0 + v1};
        case Side::South: return {f.x0 + u0, f.z1 - v1, f.x0 + u1, f.z1 - v0};
        case Side::West: return {f.x0 + v0, f.z0 + u0, f.x0 + v1, f.z0 + u1};
        default: return {f.x1 - v1, f.z0 + u0, f.x1 - v0, f.z0 + u1};
        }
    }
    [[nodiscard]] Vec2 Point(float u, float v) const
    {
        const Rect r = World(u, u, v, v);
        return {r.x0, r.z0};
    }
    /** Coordinate along the slope axis at local depth v. */
    [[nodiscard]] float Along(float v) const
    {
        const Rect& f = footprint;
        switch (entry) {
        case Side::North: return f.z0 + v;
        case Side::South: return f.z1 - v;
        case Side::West: return f.x0 + v;
        default: return f.x1 - v;
        }
    }
    [[nodiscard]] int Axis() const { return NS() ? 2 : 1; }
};

/** The walkable line of one climb from level `level` to level + 1, as 3D points; reversed when !upward. */
inline std::vector<Vec3> StairClimbPath(const SchoolPlan& plan, const StairShaft& s, int level, bool upward)
{
    const StairFrame f{s.footprint, s.entrySide};
    const float W = f.W();
    const float Le = s.entryDepth;
    const float run = f.D() - Le - s.midDepth;
    const float base = plan.FloorY(level);
    const float half = kFloorHeight * 0.5f;
    const float fw = s.flightWidth;
    const auto p3 = [&](float u, float v, float y) { return Vec3(f.Point(u, v), y); };
    std::vector<Vec3> path = {
        p3(W * 0.5f, Le * 0.5f, base),
        p3(fw * 0.5f, Le, base),
        p3(fw * 0.5f, Le + run, base + half),
        p3(W * 0.5f, Le + run + s.midDepth * 0.5f, base + half),
        p3(W - fw * 0.5f, Le + run, base + half),
        p3(W - fw * 0.5f, Le, base + kFloorHeight),
        p3(W * 0.5f, Le * 0.5f, base + kFloorHeight),
    };
    if (!upward) std::reverse(path.begin(), path.end());
    return path;
}

inline float PathLength(const std::vector<Vec3>& p)
{
    float len = 0.0f;
    for (std::size_t i = 1; i < p.size(); ++i) len += (p[i] - p[i - 1]).Length();
    return len;
}

}  // namespace School
