#include "Scene/Collision.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using School::Door;
using School::DoorId;
using School::Fixture;
using School::Room;
using School::RoomId;
using School::Side;

namespace {

constexpr float kWallThickness = 0.12f;
constexpr float kBodyBottom = 0.12f;   // a kerb or a step is walked over
constexpr float kBodyTop = 1.85f;

bool Blocks(const BoundingBox& b, float feetY)
{
    return b.Max.Y > feetY + kBodyBottom && b.Min.Y < feetY + kBodyTop;
}

bool CircleOverlapsBox(const BoundingBox& b, float x, float z, float r)
{
    const float cx = std::clamp(x, b.Min.X, b.Max.X);
    const float cz = std::clamp(z, b.Min.Z, b.Max.Z);
    const float dx = x - cx;
    const float dz = z - cz;
    return dx * dx + dz * dz < r * r;
}

}  // namespace

BoundingBox CollisionWorld::FixtureBounds(const Fixture& f, float baseY)
{
    const float c = std::cos(f.yaw), s = std::sin(f.yaw);
    // local x axis (c, -s), local z axis (s, c): extents along world axes of the rotated rectangle
    const float ex = std::fabs(c) * f.sx * 0.5f + std::fabs(s) * f.sz * 0.5f;
    const float ez = std::fabs(s) * f.sx * 0.5f + std::fabs(c) * f.sz * 0.5f;
    return BoundingBox(Vector3(f.pos.x - ex, baseY + f.y0, f.pos.z - ez), Vector3(f.pos.x + ex, baseY + f.y0 + f.sy, f.pos.z + ez));
}

void CollisionWorld::Build(RoomId id, std::vector<BoundingBox>& out) const
{
    const Room& room = plan_.room(id);
    const float y0 = plan_.FloorY(room.level);
    const float y1 = y0 + static_cast<float>(room.levels) * School::kFloorHeight;
    const School::Rect& r = room.rect;
    // Walls: each side minus its door openings. Outdoor zones have no walls (their boundaries are open ground),
    // except where they meet the campus boundary, which nobody needs to cross.
    if (room.Indoor()) {
        for (int s = 0; s < 4; ++s) {
            const Side side = static_cast<Side>(s);
            const bool alongX = side == Side::North || side == Side::South;
            const float lo = alongX ? r.x0 : r.z0;
            const float hi = alongX ? r.x1 : r.z1;
            const float fixed = side == Side::North ? r.z0 : side == Side::South ? r.z1 : side == Side::West ? r.x0 : r.x1;
            std::vector<std::pair<float, float>> gaps;
            for (const DoorId did : room.doors) {
                const Door& d = plan_.door(did);
                const Side mine = d.a == id ? d.sideOfA : School::Opposite(d.sideOfA);
                if (mine != side) continue;
                const float c = alongX ? d.pos.x : d.pos.z;
                gaps.emplace_back(c - d.width * 0.5f, c + d.width * 0.5f);
            }
            std::sort(gaps.begin(), gaps.end());
            float cursor = lo;
            const auto segment = [&](float a, float b) {
                if (b - a < 0.01f) return;
                const float t = kWallThickness * 0.5f;
                if (alongX) out.emplace_back(Vector3(a, y0 - 0.5f, fixed - t), Vector3(b, y1, fixed + t));
                else out.emplace_back(Vector3(fixed - t, y0 - 0.5f, a), Vector3(fixed + t, y1, b));
            };
            for (const auto& [g0, g1] : gaps) {
                segment(cursor, std::max(cursor, g0));
                cursor = std::max(cursor, g1);
            }
            segment(cursor, hi);
        }
    }
    const School::RoomLayout& layout = layouts_.Get(id);
    for (const Fixture& f : layout.fixtures) {
        if (!f.solid) continue;
        BoundingBox b = FixtureBounds(f, y0);
        // Low furniture is stepped on, not around; the rest blocks.
        if (b.Max.Y - y0 < 0.25f) continue;
        out.push_back(b);
    }
}

const std::vector<BoundingBox>& CollisionWorld::RoomBoxes(RoomId room) const
{
    auto it = cache_.find(room);
    if (it != cache_.end()) return it->second;
    std::vector<BoundingBox>& boxes = cache_[room];
    Build(room, boxes);
    return boxes;
}

std::vector<RoomId> CollisionWorld::RoomsAround(School::Vec2 p, int level, float margin) const
{
    std::vector<RoomId> out;
    for (const Room& r : plan_.rooms) {
        if (level < r.level - 1 || level > r.TopLevel() + 1) continue;
        if (!r.rect.Contains(p, margin)) continue;
        out.push_back(r.id);
    }
    return out;
}

void CollisionWorld::GatherBoxes(float x, float z, int level, std::vector<const std::vector<BoundingBox>*>& out) const
{
    if (!stairsBuilt_) {
        stairsBuilt_ = true;
        for (const School::SolidBox& s : plan_.stairSolids)
            stairSolids_.emplace_back(Vector3(s.rect.x0, s.y0, s.rect.z0), Vector3(s.rect.x1, s.y1, s.rect.z1));
    }
    out.push_back(&stairSolids_);
    out.push_back(&dynamic_);
    for (const RoomId id : RoomsAround(School::Vec2{x, z}, level, 1.5f)) out.push_back(&RoomBoxes(id));
}

bool CollisionWorld::Overlaps(float x, float z, float feetY, float radius, int level) const
{
    std::vector<const std::vector<BoundingBox>*> lists;
    GatherBoxes(x, z, level, lists);
    for (const auto* list : lists)
        for (const BoundingBox& b : *list)
            if (Blocks(b, feetY) && CircleOverlapsBox(b, x, z, radius)) return true;
    return false;
}

Vector3 CollisionWorld::Move(const Vector3& from, const Vector3& delta, float radius, int level) const
{
    // A frame moves a few centimetres, far less than a wall's thickness, so resolving each axis on its own slides along walls.
    std::vector<const std::vector<BoundingBox>*> lists;
    GatherBoxes(from.X, from.Z, level, lists);
    const auto blocked = [&](float x, float z) {
        for (const auto* list : lists)
            for (const BoundingBox& b : *list)
                if (Blocks(b, from.Y) && CircleOverlapsBox(b, x, z, radius)) return true;
        return false;
    };
    Vector3 p = from;
    if (!blocked(p.X + delta.X, p.Z)) p.X += delta.X;
    if (!blocked(p.X, p.Z + delta.Z)) p.Z += delta.Z;
    return p;
}

std::optional<float> CollisionWorld::Raycast(const Ray& ray, float maxDistance, int level) const
{
    std::vector<const std::vector<BoundingBox>*> lists;
    GatherBoxes(ray.Position.X, ray.Position.Z, level, lists);
    std::optional<float> best;
    for (const auto* list : lists)
        for (const BoundingBox& b : *list) {
            const std::optional<float> d = b.Intersects(ray);
            if (d && *d >= 0.0f && *d <= maxDistance && (!best || *d < *best)) best = d;
        }
    return best;
}
