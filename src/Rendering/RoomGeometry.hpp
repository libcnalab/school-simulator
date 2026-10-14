#pragma once

// CPU geometry of one room: floor, ceiling, walls with door and window openings, lights, furniture and
// stairs, grouped by texture. Pure data in, pure data out; WorldRenderer bakes and uploads it.

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Rendering/MeshBuilder.hpp"
#include "Rooms/RoomLayout.hpp"
#include "World/SchoolPlan.hpp"

struct RoomGeometry {
    MeshSet meshes;                      // opaque and transparent groups by texture name
    std::vector<BakedLight> lights;      // ceiling lights (indoors) or the sun (outdoors)
    Microsoft::Xna::Framework::Vector3 ambient{0.5f, 0.5f, 0.52f};
    bool outdoor = false;                // outdoor geometry is modulated by the daylight at draw time
};

/** The interior of a room (or the ground and furniture of an outdoor zone). */
RoomGeometry BuildRoomGeometry(const School::SchoolPlan& plan, const School::LayoutCache& layouts, School::RoomId room);

/** The outside of the building: brick facades with window glass, roofs, plus the ground plane. One MeshSet per wing. */
struct FacadeGeometry {
    std::vector<MeshSet> chunks;         // index = wing id (+1 for the ground)
};
FacadeGeometry BuildFacadeGeometry(const School::SchoolPlan& plan);

/** Floor texture name of a room (also used by the minimap for colours). */
std::string FloorTextureOf(const School::SchoolPlan& plan, const School::Room& room);
Microsoft::Xna::Framework::Vector3 PaletteColor(int index);
