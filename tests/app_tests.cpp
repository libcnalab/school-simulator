// Tests of the application half that need no window or graphics device: room geometry for every room,
// the facade, collision boxes around doors, the people's looks. Links CNA for its maths only.
#include <cmath>
#include <cstdio>
#include <string>

#include "Rendering/NpcRenderer.hpp"
#include "Rendering/RoomGeometry.hpp"
#include "Scene/Collision.hpp"
#include "Simulation/Population.hpp"
#include "World/SchoolPlan.hpp"
#include "World/StairGeometry.hpp"

using namespace School;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Vector3;

namespace {
int g_checks = 0;
int g_failures = 0;
#define CHECK_MSG(cond, ...)                                                                          \
    do {                                                                                              \
        ++g_checks;                                                                                   \
        if (!(cond)) {                                                                                \
            ++g_failures;                                                                             \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);                             \
            std::printf(__VA_ARGS__);                                                                 \
            std::printf("\n");                                                                        \
        }                                                                                             \
    } while (0)

bool Finite(const Vector3& v) { return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z); }
}  // namespace

int main()
{
    const SchoolPlan plan = BuildSchoolPlan();
    const LayoutCache layouts(plan);

    // --- every room builds, stays inside its rectangle and within the index limit --------------------------------
    long long vertices = 0, triangles = 0;
    int builders = 0;
    for (const Room& r : plan.rooms) {
        const RoomGeometry g = BuildRoomGeometry(plan, layouts, r.id);
        const float y0 = plan.FloorY(r.level);
        const float margin = r.Indoor() ? 1.2f : 6.0f;   // outdoor trees and crowns spill a little
        for (const auto* groups : {&g.meshes.OpaqueMeshes(), &g.meshes.TransparentMeshes()})
            for (const auto& [texture, list] : *groups)
                for (const MeshBuilder& b : list) {
                    ++builders;
                    vertices += static_cast<long long>(b.VertexCount());
                    triangles += static_cast<long long>(b.Indices().size() / 3);
                    CHECK_MSG(b.VertexCount() <= 65535, "%s/%s has %zu vertices", r.name.c_str(), texture.c_str(), b.VertexCount());
                    CHECK_MSG(b.Indices().size() % 3 == 0, "%s/%s index count", r.name.c_str(), texture.c_str());
                    const BoundingBox bb = b.Bounds();
                    CHECK_MSG(Finite(bb.Min) && Finite(bb.Max), "%s/%s has non-finite bounds", r.name.c_str(), texture.c_str());
                    CHECK_MSG(bb.Min.X >= r.rect.x0 - margin && bb.Max.X <= r.rect.x1 + margin && bb.Min.Z >= r.rect.z0 - margin && bb.Max.Z <= r.rect.z1 + margin,
                              "%s/%s spills out of its room: x %.1f..%.1f z %.1f..%.1f vs %.1f..%.1f %.1f..%.1f", r.name.c_str(), texture.c_str(), static_cast<double>(bb.Min.X),
                              static_cast<double>(bb.Max.X), static_cast<double>(bb.Min.Z), static_cast<double>(bb.Max.Z), static_cast<double>(r.rect.x0), static_cast<double>(r.rect.x1),
                              static_cast<double>(r.rect.z0), static_cast<double>(r.rect.z1));
                    const float below = r.type == RoomType::ElevatorCab ? 1.5f : 1.0f;   // a shaft has a pit
                    CHECK_MSG(bb.Min.Y >= y0 - below && bb.Max.Y <= y0 + static_cast<float>(r.levels) * kFloorHeight + 12.0f, "%s/%s has a strange height", r.name.c_str(), texture.c_str());
                    for (const std::uint16_t i : b.Indices()) CHECK_MSG(i < b.VertexCount(), "%s/%s index out of range", r.name.c_str(), texture.c_str());
                    if (g.lights.size() > 8) {
                        // The grid-accelerated light bake must agree with the plain loop over every light (within a rounding step).
                        const auto baked = b.Bake(g.lights, g.ambient);
                        CHECK_MSG(baked.size() == b.VertexCount(), "%s/%s bake size", r.name.c_str(), texture.c_str());
                        for (std::size_t vi = 0; vi < baked.size() && vi < b.Vertices().size(); ++vi) {
                            const MeshBuilder::Vertex& v = b.Vertices()[vi];
                            Vector3 lit = g.ambient;
                            for (const BakedLight& light : g.lights) {
                                Vector3 toLight = light.position - v.position;
                                const float dist = toLight.Length();
                                if (dist >= light.radius || dist < 1e-4f) continue;
                                toLight = toLight * (1.0f / dist);
                                const float ndl = Vector3::Dot(v.normal, toLight) * 0.5f + 0.5f;
                                const float falloff = 1.0f - dist / light.radius;
                                lit += light.color * (light.intensity * ndl * ndl * falloff * falloff);
                            }
                            const float k = 1.0f - v.emissive;
                            const auto byte = [](float x) { return static_cast<int>(std::fmin(std::fmax(x, 0.0f), 1.0f) * 255.0f + 0.5f); };
                            const int er = byte(v.albedo.X * (lit.X * k + v.emissive)), eg = byte(v.albedo.Y * (lit.Y * k + v.emissive)), eb = byte(v.albedo.Z * (lit.Z * k + v.emissive));
                            const auto& c = baked[vi].Color;
                            if (std::abs(c.getRProperty() - er) > 1 || std::abs(c.getGProperty() - eg) > 1 || std::abs(c.getBProperty() - eb) > 1) {
                                CHECK_MSG(false, "%s/%s vertex %zu baked %d %d %d, expected %d %d %d", r.name.c_str(), texture.c_str(), vi, c.getRProperty(), c.getGProperty(), c.getBProperty(), er, eg, eb);
                                break;
                            }
                        }
                    }
                }
        CHECK_MSG(!g.meshes.Empty(), "%s has no geometry", r.name.c_str());
        if (r.Indoor()) CHECK_MSG(!g.lights.empty(), "%s has no lights", r.name.c_str());
    }
    std::printf("geometry: %zu rooms, %d builders, %lld vertices, %lld triangles\n", plan.rooms.size(), builders, vertices, triangles);

    // --- stairs: the steps climb the way the player walks (Blueprint's stairSurfaces) --------------------------------
    {
        int flights = 0;
        for (const StairShaft& sh : plan.stairs) {
            const School::StairFrame f{sh.footprint, sh.entrySide};
            const float run = f.D() - sh.entryDepth - sh.midDepth;
            for (int level = sh.levelLow; level < sh.levelHigh; ++level) {
                const Room& room = plan.room(sh.landing[static_cast<std::size_t>(level - sh.levelLow)]);
                const RoomGeometry g = BuildRoomGeometry(plan, layouts, room.id);
                const float base = plan.FloorY(level);
                // highest upward-facing point of the step geometry on a line across a flight (slice rectangle, a little tolerance)
                const auto topIn = [&](float u0, float u1, float v0, float v1) {
                    const Rect r = f.World(u0, u1, v0, v1);
                    float top = -1e9f;
                    for (const auto& [texture, list] : g.meshes.OpaqueMeshes())
                        for (const MeshBuilder& b : list)
                            for (const MeshBuilder::Vertex& v : b.Vertices())
                                if (v.position.X >= r.x0 - 0.02f && v.position.X <= r.x1 + 0.02f && v.position.Z >= r.z0 - 0.02f && v.position.Z <= r.z1 + 0.02f &&
                                    v.normal.Y > 0.9f && texture == "floor_concrete")
                                    top = std::max(top, v.position.Y);
                    return top;
                };
                                const float u1a = 0.0f, u1b = sh.flightWidth, u2a = f.W() - sh.flightWidth, u2b = f.W();
                const float f1Entry = topIn(u1a, u1b, sh.entryDepth, sh.entryDepth), f1Far = topIn(u1a, u1b, sh.entryDepth + run, sh.entryDepth + run);
                const float f2Entry = topIn(u2a, u2b, sh.entryDepth, sh.entryDepth), f2Far = topIn(u2a, u2b, sh.entryDepth + run, sh.entryDepth + run);
                CHECK_MSG(f1Far > f1Entry + 0.3f, "stair %d level %d: flight 1 does not climb away from the entry (%.2f -> %.2f)", sh.id, level, static_cast<double>(f1Entry - base), static_cast<double>(f1Far - base));
                CHECK_MSG(f2Entry > f2Far + 0.3f, "stair %d level %d: flight 2 does not climb towards the entry (%.2f -> %.2f)", sh.id, level, static_cast<double>(f2Far - base), static_cast<double>(f2Entry - base));
                CHECK_MSG(std::fabs(f2Entry - (base + kFloorHeight)) < 0.25f, "stair %d level %d: flight 2 does not reach the next floor (%.2f)", sh.id, level, static_cast<double>(f2Entry - base));
                CHECK_MSG(std::fabs(f1Far - (base + kFloorHeight * 0.5f)) < 0.25f && std::fabs(f2Far - (base + kFloorHeight * 0.5f)) < 0.25f, "stair %d level %d: flights do not meet at the half landing", sh.id, level);
                ++flights;
            }
        }
        std::printf("stairs: %d flight pairs checked\n", flights);
    }

    // --- facade --------------------------------------------------------------------------------------------------
    {
        const FacadeGeometry f = BuildFacadeGeometry(plan);
        CHECK_MSG(f.chunks.size() == plan.wings.size() + 1, "facade chunk count");
        int nonEmpty = 0;
        for (const MeshSet& set : f.chunks)
            if (!set.Empty()) ++nonEmpty;
        CHECK_MSG(nonEmpty >= 6, "only %d facade chunks have geometry", nonEmpty);
    }

    // --- collision: doors are passable, room hubs are free, walls exist --------------------------------------------
    {
        const CollisionWorld collision(plan, layouts);
        int blockedDoors = 0;
        for (const Door& d : plan.doors) {
            const float y = plan.FloorY(d.level);
            const bool blocked = collision.Overlaps(d.pos.x, d.pos.z, y, 0.2f, d.level);
            if (blocked) ++blockedDoors;
            CHECK_MSG(!blocked, "door %d between %s and %s is blocked", d.id, plan.room(d.a).name.c_str(), plan.room(d.b).name.c_str());
        }
        int blockedHubs = 0;
        for (const Room& r : plan.rooms) {
            if (!r.Indoor() || r.type == RoomType::StairWell) continue;
            const RoomLayout& l = layouts.Get(r.id);
            const Vec2 hub = r.rect.Clamp(l.hub, 0.5f);
            if (collision.Overlaps(hub.x, hub.z, plan.FloorY(r.level), 0.25f, r.level)) {
                ++blockedHubs;
                CHECK_MSG(false, "the hub of %s is inside something solid", r.name.c_str());
            }
            // a point just outside the room, across a wall without a door, must be blocked when walking in
            CHECK_MSG(!collision.RoomBoxes(r.id).empty(), "%s has no collision boxes", r.name.c_str());
        }
        std::printf("collision: %zu doors (%d blocked), %d blocked hubs\n", plan.doors.size(), blockedDoors, blockedHubs);
    }

    // --- looks are deterministic and sane ---------------------------------------------------------------------------
    {
        PopulationSpec spec;
        spec.students = 200;
        const Population pop = BuildPopulation(plan, spec);
        for (const Npc& n : pop.npcs) {
            const NpcRenderer::Look a = NpcRenderer::LookOf(n, pop);
            const NpcRenderer::Look b = NpcRenderer::LookOf(n, pop);
            CHECK_MSG(a.height == b.height && a.shirt == b.shirt, "look of %s is not deterministic", n.name.c_str());
            CHECK_MSG(a.height > 1.3f && a.height < 2.0f, "%s is %.2f m tall", n.name.c_str(), static_cast<double>(a.height));
        }
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
