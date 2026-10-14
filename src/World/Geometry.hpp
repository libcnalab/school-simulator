#pragma once

// Plain geometry for the simulation core. This half of the project never includes CNA, so it has its
// own tiny vector types; the rendering side converts to CNA's Vector3 at the boundary.
//
// Conventions (same as XNA): X east, Y up, Z south (so "north" is -Z). Units are metres.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace School {

struct Vec2 {
    float x = 0.0f;
    float z = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float z_) : x(x_), z(z_) {}
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, z + o.z}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, z - o.z}; }
    constexpr Vec2 operator*(float s) const { return {x * s, z * s}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; z += o.z; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; z -= o.z; return *this; }
    constexpr bool operator==(Vec2 o) const { return x == o.x && z == o.z; }
    [[nodiscard]] float Length() const { return std::sqrt(x * x + z * z); }
    [[nodiscard]] constexpr float LengthSq() const { return x * x + z * z; }
    [[nodiscard]] Vec2 Normalized() const
    {
        const float l = Length();
        return l > 1e-6f ? Vec2{x / l, z / l} : Vec2{0.0f, 0.0f};
    }
    /** The vector turned 90 degrees to the right when seen from above with north (-Z) up. */
    [[nodiscard]] constexpr Vec2 RightHand() const { return {-z, x}; }
};

constexpr float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
inline float Distance(Vec2 a, Vec2 b) { return (a - b).Length(); }
constexpr float DistanceSq(Vec2 a, Vec2 b) { return (a - b).LengthSq(); }
constexpr Vec2 Lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3(Vec2 p, float y_) : x(p.x), y(y_), z(p.z) {}
    [[nodiscard]] constexpr Vec2 XZ() const { return {x, z}; }
    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    [[nodiscard]] float Length() const { return std::sqrt(x * x + y * y + z * z); }
};

/** Axis-aligned rectangle on the floor plane. x0 < x1 (west to east), z0 < z1 (north to south). */
struct Rect {
    float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
    constexpr Rect() = default;
    constexpr Rect(float x0_, float z0_, float x1_, float z1_) : x0(x0_), z0(z0_), x1(x1_), z1(z1_) {}
    [[nodiscard]] constexpr float Width() const { return x1 - x0; }
    [[nodiscard]] constexpr float Depth() const { return z1 - z0; }
    [[nodiscard]] constexpr float Area() const { return Width() * Depth(); }
    [[nodiscard]] constexpr Vec2 Center() const { return {(x0 + x1) * 0.5f, (z0 + z1) * 0.5f}; }
    [[nodiscard]] constexpr bool Valid() const { return x1 > x0 && z1 > z0; }
    [[nodiscard]] constexpr bool Contains(Vec2 p, float margin = 0.0f) const
    {
        return p.x >= x0 - margin && p.x <= x1 + margin && p.z >= z0 - margin && p.z <= z1 + margin;
    }
    /** True when the interiors overlap (touching edges do not count). */
    [[nodiscard]] constexpr bool Overlaps(const Rect& o, float eps = 1e-4f) const
    {
        return x0 < o.x1 - eps && o.x0 < x1 - eps && z0 < o.z1 - eps && o.z0 < z1 - eps;
    }
    [[nodiscard]] constexpr Rect Inflated(float m) const { return {x0 - m, z0 - m, x1 + m, z1 + m}; }
    [[nodiscard]] constexpr Vec2 Clamp(Vec2 p, float margin = 0.0f) const
    {
        return {std::clamp(p.x, x0 + margin, std::max(x0 + margin, x1 - margin)),
                std::clamp(p.z, z0 + margin, std::max(z0 + margin, z1 - margin))};
    }
    /** Smallest rectangle containing both. */
    [[nodiscard]] constexpr Rect United(const Rect& o) const
    {
        return {std::min(x0, o.x0), std::min(z0, o.z0), std::max(x1, o.x1), std::max(z1, o.z1)};
    }
};

/** The four compass sides of a rectangle. */
enum class Side : std::uint8_t { North, East, South, West };

constexpr Vec2 SideNormal(Side s)   // outward unit normal
{
    switch (s) {
    case Side::North: return {0.0f, -1.0f};
    case Side::East: return {1.0f, 0.0f};
    case Side::South: return {0.0f, 1.0f};
    case Side::West: return {-1.0f, 0.0f};
    }
    return {0.0f, 0.0f};
}

constexpr Side Opposite(Side s)
{
    switch (s) {
    case Side::North: return Side::South;
    case Side::East: return Side::West;
    case Side::South: return Side::North;
    case Side::West: return Side::East;
    }
    return s;
}

inline const char* SideName(Side s)
{
    switch (s) {
    case Side::North: return "north";
    case Side::East: return "east";
    case Side::South: return "south";
    case Side::West: return "west";
    }
    return "?";
}

}  // namespace School
