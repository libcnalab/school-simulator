#pragma once

// Collision for the player: axis-aligned boxes built per room from the plan (walls with their door
// openings cut out), the room layouts (solid furniture) and the stair solids. Boxes are built lazily
// for the rooms around the player and cached; the ground height comes from the plan itself.

#include <optional>
#include <unordered_map>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Rooms/RoomLayout.hpp"
#include "World/SchoolPlan.hpp"

class CollisionWorld {
public:
    CollisionWorld(const School::SchoolPlan& plan, const School::LayoutCache& layouts) : plan_(plan), layouts_(layouts) {}

    /** The boxes of one room (walls, solid fixtures). Built on first use. */
    const std::vector<Microsoft::Xna::Framework::BoundingBox>& RoomBoxes(School::RoomId room) const;

    /** Rooms whose rectangle (inflated by `margin`) contains p on `level` or on the levels a tall room spans. */
    std::vector<School::RoomId> RoomsAround(School::Vec2 p, int level, float margin) const;

    /** Moves a vertical cylinder (feet at from.Y) by `delta` on the XZ plane, sliding along what it hits. */
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 Move(const Microsoft::Xna::Framework::Vector3& from, const Microsoft::Xna::Framework::Vector3& delta, float radius, int level) const;

    [[nodiscard]] bool Overlaps(float x, float z, float feetY, float radius, int level) const;

    /** Nearest hit along the ray among the boxes of the rooms around its origin. */
    [[nodiscard]] std::optional<float> Raycast(const Microsoft::Xna::Framework::Ray& ray, float maxDistance, int level) const;

    /** Axis-aligned bounds of a fixture in world space (used for collision and interaction). */
    static Microsoft::Xna::Framework::BoundingBox FixtureBounds(const School::Fixture& f, float baseY);

    [[nodiscard]] std::size_t CachedRooms() const { return cache_.size(); }

private:
    void Build(School::RoomId room, std::vector<Microsoft::Xna::Framework::BoundingBox>& out) const;
    void GatherBoxes(float x, float z, int level, std::vector<const std::vector<Microsoft::Xna::Framework::BoundingBox>*>& out) const;

    const School::SchoolPlan& plan_;
    const School::LayoutCache& layouts_;
    mutable std::unordered_map<int, std::vector<Microsoft::Xna::Framework::BoundingBox>> cache_;
    mutable std::vector<Microsoft::Xna::Framework::BoundingBox> stairSolids_;
    mutable bool stairsBuilt_ = false;
};
