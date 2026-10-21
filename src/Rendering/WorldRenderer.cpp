#include "Rendering/WorldRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "App/Log.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Rendering/EffectBank.hpp"
#include "Rendering/LitMesh.hpp"
#include "Rendering/RenderStates.hpp"
#include "Rendering/TextureLibrary.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using School::Door;
using School::DoorKind;
using School::Room;
using School::RoomId;

WorldRenderer::WorldRenderer(Services& services, const School::SchoolPlan& plan, const School::LayoutCache& layouts) : s_(services), plan_(plan), layouts_(layouts)
{
    const auto t0 = std::chrono::steady_clock::now();
    const FacadeGeometry facade = BuildFacadeGeometry(plan);
    for (const MeshSet& set : facade.chunks) {
        Chunk c;
        int tris = 0;
        BakedLight sun;
        sun.position = Vector3(plan.campus.Center().x - 300.0f, 500.0f, plan.campus.Center().z + 200.0f);
        sun.color = Vector3(1, 0.98f, 0.95f);
        sun.intensity = 0.55f;
        sun.radius = 5000.0f;
        Upload(set, {sun}, Vector3(0.62f, 0.62f, 0.62f), c.opaque, c.transparent, c.bounds, tris);
        facade_.push_back(std::move(c));
    }
    // Every outdoor lamp post, for the night glow.
    for (const Room& r : plan.rooms) {
        if (r.Indoor()) continue;
        for (const School::Fixture& f : layouts.Get(r.id).fixtures)
            if (f.type == School::FixtureType::Lamp) lamps_.push_back({Vector3(f.pos.x, plan.FloorY(r.level) + f.y0 + f.sy - 0.27f, f.pos.z), plan.FloorY(r.level)});
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (const char* env = std::getenv("SCHOOL_CORRIDOR_SEGMENTS")) maxGroupSegments = std::max(1, std::atoi(env));   // 1 switches the merging off
    if (const char* env = std::getenv("SCHOOL_PORTAL_CULLING")) portalCulling = std::atoi(env) != 0;
    FindCorridorGroups();
    School::Log("world", "facade built: %zu chunks, %zu lamp posts, %zu corridor groups, in %.0f ms", facade_.size(), lamps_.size(), groups_.size(), ms);
}

void WorldRenderer::FindCorridorGroups()
{
    groupOf_.assign(plan_.rooms.size(), -1);
    groups_.clear();
    if (maxGroupSegments < 2) return;
    // Corridor segments that lie in one line (same wing, level and axis, same cross position) and touch each other.
    struct Seg {
        RoomId id;
        float cross, from, to;
        int wing, level;
        bool alongX;
    };
    std::vector<Seg> segs;
    for (const Room& r : plan_.rooms) {
        if (r.type != School::RoomType::Corridor) continue;
        const bool alongX = r.rect.Width() >= r.rect.Depth();
        segs.push_back({r.id, alongX ? r.rect.Center().z : r.rect.Center().x, alongX ? r.rect.x0 : r.rect.z0, alongX ? r.rect.x1 : r.rect.z1, r.wing, r.level, alongX});
    }
    std::sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) {
        if (a.wing != b.wing) return a.wing < b.wing;
        if (a.level != b.level) return a.level < b.level;
        if (a.alongX != b.alongX) return a.alongX < b.alongX;
        if (std::fabs(a.cross - b.cross) > 0.5f) return a.cross < b.cross;
        return a.from < b.from;
    });
    std::vector<RoomId> run;
    const auto flush = [&] {
        if (run.size() >= 2) {
            for (const RoomId id : run) groupOf_[static_cast<std::size_t>(id)] = static_cast<int>(groups_.size());
            groups_.push_back(run);
        }
        run.clear();
    };
    const Seg* prev = nullptr;
    for (const Seg& sg : segs) {
        const bool continues = prev != nullptr && prev->wing == sg.wing && prev->level == sg.level && prev->alongX == sg.alongX && std::fabs(prev->cross - sg.cross) <= 0.5f &&
                               std::fabs(prev->to - sg.from) <= 0.3f && static_cast<int>(run.size()) < maxGroupSegments;
        if (!continues) flush();
        run.push_back(sg.id);
        prev = &sg;
    }
    flush();
}

void WorldRenderer::DrawLamps(const DrawContext& ctx, const WorldLighting& light)
{
    // Additive glow billboards at the lamp heads and pools of light on the ground, fading in as the daylight goes.
    const float night = std::clamp(1.0f - light.daylight * 1.6f, 0.0f, 1.0f);
    if (night < 0.02f || lamps_.empty()) return;
    const Matrix& v = ctx.view.view;
    const Vector3 right(v.M11, v.M21, v.M31);
    const Vector3 up(v.M12, v.M22, v.M32);
    lampScratch_.clear();
    const auto quad = [&](const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Color& col) {
        lampScratch_.emplace_back(a, col, Vector2(0, 0));
        lampScratch_.emplace_back(b, col, Vector2(1, 0));
        lampScratch_.emplace_back(c, col, Vector2(1, 1));
        lampScratch_.emplace_back(a, col, Vector2(0, 0));
        lampScratch_.emplace_back(c, col, Vector2(1, 1));
        lampScratch_.emplace_back(d, col, Vector2(0, 1));
    };
    const auto tone = [&](float f) { return static_cast<int>(std::clamp(f * night, 0.0f, 1.0f) * 255.0f); };
    const Color glow(tone(1.0f), tone(0.85f), tone(0.55f), 255);
    const Color pool(tone(0.6f), tone(0.52f), tone(0.3f), 255);
    int drawn = 0;
    for (const Lamp& l : lamps_) {
        if (Vector3::DistanceSquared(l.head, ctx.view.eye) > 120.0f * 120.0f) continue;
        if (!ctx.view.frustum.Intersects(BoundingSphere(Vector3(l.head.X, l.groundY + 2.0f, l.head.Z), 6.0f))) continue;
        const Vector3 r = right * 1.7f, u = up * 1.7f;
        quad(l.head - r + u, l.head + r + u, l.head + r - u, l.head - r - u, glow);
        const float R = 6.5f;
        const Vector3 g(l.head.X, l.groundY + 0.03f, l.head.Z);
        quad(g + Vector3(-R, 0, -R), g + Vector3(R, 0, -R), g + Vector3(R, 0, R), g + Vector3(-R, 0, R), pool);
        ++drawn;
    }
    if (lampScratch_.empty()) return;
    GraphicsDevice& device = ctx.device;
    device.setBlendStateProperty(BlendState::Additive);
    device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
    BasicEffect& fx = ctx.fx.World();
    fx.setWorldProperty(Matrix::getIdentityProperty());
    fx.setDiffuseColorProperty(Vector3(1, 1, 1));
    fx.setTextureProperty(s_.textures->Get("particle"));
    EffectBank::Apply(fx);
    device.DrawUserPrimitives(PrimitiveType::TriangleList, lampScratch_.data(), 0, static_cast<int>(lampScratch_.size()) / 3);
    stats_.drawCalls += 1;
    stats_.trianglesDrawn += drawn * 4;
    RenderStates::SetOpaque3D(device);
}

WorldRenderer::~WorldRenderer() = default;

void WorldRenderer::Clear()
{
    rooms_.clear();
    loadedList_.clear();
    buildQueue_.clear();
}

BoundingBox WorldRenderer::RoomBounds(RoomId id) const
{
    const Room& r = plan_.room(id);
    const float y0 = plan_.FloorY(r.level);
    const float h = r.Indoor() ? static_cast<float>(r.levels) * School::kFloorHeight : 8.0f;
    return BoundingBox(Vector3(r.rect.x0, y0 - 0.1f, r.rect.z0), Vector3(r.rect.x1, y0 + h, r.rect.z1));
}

void WorldRenderer::Upload(const MeshSet& set, const std::vector<BakedLight>& lights, const Vector3& ambient, std::vector<Batch>& opaque, std::vector<Batch>& transparent,
                           BoundingBox& bounds, int& triangles)
{
    bool first = true;
    const auto upload = [&](const std::map<std::string, MeshSet::Group>& groups, std::vector<Batch>& out) {
        for (const auto& [texture, builders] : groups) {
            for (const MeshBuilder& b : builders) {
                if (b.Empty()) continue;
                Batch batch;
                batch.texture = s_.textures->Get(texture);
                const BoundingBox bb = b.Bounds();
                bounds = first ? bb : BoundingBox::CreateMerged(bounds, bb);
                first = false;
                batch.mesh = std::make_unique<StaticMesh>(*s_.device, b.Bake(lights, ambient), b.Indices(), bb);
                triangles += batch.mesh->TriangleCount();
                out.push_back(std::move(batch));
            }
        }
    };
    upload(set.OpaqueMeshes(), opaque);
    upload(set.TransparentMeshes(), transparent);
    if (first) bounds = BoundingBox(Vector3::Zero, Vector3::Zero);
}

void WorldRenderer::BuildGroup(int group)
{
    const std::vector<RoomId>& members = groups_[static_cast<std::size_t>(group)];
    RoomGeometry merged;
    bool first = true;
    for (const RoomId m : members) {
        RoomGeometry g = BuildRoomGeometry(plan_, layouts_, m);
        merged.meshes.Merge(g.meshes);
        merged.lights.insert(merged.lights.end(), g.lights.begin(), g.lights.end());
        if (first) merged.ambient = g.ambient;
        first = false;
    }
    auto lead = std::make_unique<LoadedRoom>();
    lead->id = members.front();
    lead->group = group;
    Upload(merged.meshes, merged.lights, merged.ambient, lead->opaque, lead->transparent, lead->bounds, lead->triangles);
    for (const RoomId m : members) lead->bounds = BoundingBox::CreateMerged(lead->bounds, RoomBounds(m));
    for (std::size_t i = 1; i < members.size(); ++i) {
        auto follower = std::make_unique<LoadedRoom>();
        follower->id = members[i];
        follower->group = group;
        follower->follower = true;
        follower->bounds = RoomBounds(members[i]);
        rooms_[members[i]] = std::move(follower);
        loadedList_.push_back(members[i]);
        ++stats_.roomsBuiltTotal;
    }
    rooms_[members.front()] = std::move(lead);
    loadedList_.push_back(members.front());
    ++stats_.roomsBuiltTotal;
}

void WorldRenderer::Build(RoomId id)
{
    if (rooms_.count(id)) return;
    if (const int group = groupOf_.empty() ? -1 : groupOf_[static_cast<std::size_t>(id)]; group >= 0) {
        BuildGroup(group);
        return;
    }
    auto lr = std::make_unique<LoadedRoom>();
    lr->id = id;
    const RoomGeometry g = BuildRoomGeometry(plan_, layouts_, id);
    lr->outdoor = g.outdoor;
    Upload(g.meshes, g.lights, g.ambient, lr->opaque, lr->transparent, lr->bounds, lr->triangles);
    // The visual bounds include the full room volume so the frustum test never drops a room whose geometry is thin.
    lr->bounds = BoundingBox::CreateMerged(lr->bounds, RoomBounds(id));
    rooms_[id] = std::move(lr);
    loadedList_.push_back(id);
    ++stats_.roomsBuiltTotal;
}

void WorldRenderer::Ensure(RoomId id)
{
    if (id != School::kNone && !rooms_.count(id)) Build(id);
}

float WorldRenderer::DistanceTo(const Room& room, const Vector3& eye) const
{
    const School::Vec2 p{eye.X, eye.Z};
    const School::Vec2 c = room.rect.Clamp(p);
    return School::Distance(p, c);
}

void WorldRenderer::Update(const Vector3& eye, int level, float dt)
{
    Update(eye, level, dt, {});
}

void WorldRenderer::Update(const Vector3& eye, int level, float dt, const std::vector<Anchor>& anchors)
{
    lastEye_ = eye;
    lastLevel_ = level;
    sinceScan_ += dt;
    const auto t0 = std::chrono::steady_clock::now();
    // Re-plan the wanted set a few times a second, not every frame.
    if (sinceScan_ >= 0.25f || buildQueue_.empty()) {
        sinceScan_ = 0.0f;
        struct Want {
            float score;
            RoomId id;
        };
        std::vector<Want> wanted;
        for (const Room& r : plan_.rooms) {
            const float d = DistanceTo(r, eye);
            const int dl = std::abs(r.level - level) - (level > r.level && level <= r.TopLevel() ? std::abs(r.level - level) : 0);
            float radius;
            if (!r.Indoor()) radius = r.type == School::RoomType::Street ? 60.0f : loadRadius * 1.9f;
            else if (dl == 0) radius = loadRadius;
            else if (dl == 1) radius = adjacentLevelRadius;
            else radius = 0.0f;
            if (d < radius) {
                wanted.push_back({d + static_cast<float>(dl) * 20.0f, r.id});
                continue;
            }
            // Secondary anchors (CCTV cameras): a small bubble of rooms around each.
            for (const Anchor& a : anchors) {
                if (r.level > a.level || r.TopLevel() < a.level) continue;
                const float da = DistanceTo(r, a.position);
                if (da < a.radius) {
                    wanted.push_back({200.0f + da, r.id});
                    break;
                }
            }
        }
        std::sort(wanted.begin(), wanted.end(), [](const Want& a, const Want& b) { return a.score < b.score; });
        if (static_cast<int>(wanted.size()) > maxLoaded) wanted.resize(static_cast<std::size_t>(maxLoaded));
        // A corridor group is wanted as a whole (its geometry is one mesh), so one wanted member brings the others along.
        std::unordered_set<int> keep;
        buildQueue_.clear();
        const auto want = [&](RoomId id) {
            if (!keep.insert(id).second) return;
            if (!rooms_.count(id)) buildQueue_.push_back(id);
        };
        for (const Want& w : wanted) {
            want(w.id);
            if (const int g = groupOf_[static_cast<std::size_t>(w.id)]; g >= 0)
                for (const RoomId m : groups_[static_cast<std::size_t>(g)]) want(m);
        }
        // Drop what is no longer wanted.
        for (auto it = rooms_.begin(); it != rooms_.end();) {
            if (keep.count(it->first)) ++it;
            else it = rooms_.erase(it);
        }
        loadedList_.clear();
        for (const auto& [id, lr] : rooms_) loadedList_.push_back(id);
    }
    int built = 0;
    while (!buildQueue_.empty() && built < buildsPerFrame) {
        const RoomId id = buildQueue_.front();
        buildQueue_.erase(buildQueue_.begin());
        Build(id);
        ++built;
        if (buildTimeBudget && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > 6.0) break;
    }
    stats_.buildMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    stats_.roomsLoaded = static_cast<int>(rooms_.size());
}

void WorldRenderer::DrawBatches(const DrawContext& ctx, const std::vector<Batch>& batches, bool)
{
    BasicEffect& fx = ctx.fx.World();
    for (const Batch& b : batches) {
        fx.setTextureProperty(b.texture);
        EffectBank::Apply(fx);
        b.mesh->Draw(ctx.device);
        stats_.trianglesDrawn += b.mesh->TriangleCount();
        ++stats_.drawCalls;
    }
}

bool WorldRenderer::ComputePortalVisibility(const DrawContext& ctx, const std::vector<School::DoorRuntime>& doors, std::vector<char>& seen) const
{
    const Vector3 eye = ctx.view.eye;
    const School::Vec2 p{eye.X, eye.Z};
    seen.assign(plan_.rooms.size(), 0);
    std::vector<std::pair<RoomId, int>> queue;
    const auto add = [&](RoomId id, int depth) {
        if (seen[static_cast<std::size_t>(id)]) return;
        seen[static_cast<std::size_t>(id)] = 1;
        queue.emplace_back(id, depth);
    };
    bool eyeRoomHasWindows = false, found = false;
    for (const auto& [id, lr] : rooms_) {
        const Room& r = plan_.room(id);
        const float y0 = plan_.FloorY(r.level);
        const float top = y0 + (r.Indoor() ? static_cast<float>(r.levels) * School::kFloorHeight : 8.0f);
        const School::Vec2 c = r.rect.Clamp(p);
        const float flat = School::Distance(p, c);
        const bool inside = flat < 0.001f && eye.Y >= y0 - 0.3f && eye.Y <= top;
        if (inside) {
            found = true;
            eyeRoomHasWindows = eyeRoomHasWindows || r.exteriorSides != 0 || !r.Indoor();
            add(id, 0);
        } else if (r.Indoor() && flat < 3.0f && eye.Y >= y0 - 0.3f && eye.Y <= top) {
            add(id, 1);   // a metre or two from the eye: always drawn, whatever the doors say
        }
    }
    if (!found) return false;   // in no room (free flight, a wall): draw everything in the view
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const auto [id, depth] = queue[head];
        if (depth >= 12) continue;
        for (const School::DoorId did : plan_.room(id).doors) {
            const Door& d = plan_.door(did);
            const RoomId other = d.Other(id);
            if (other == School::kNone || seen[static_cast<std::size_t>(other)]) continue;
            const float open = did < static_cast<int>(doors.size()) ? doors[static_cast<std::size_t>(did)].open : 0.0f;
            const bool see = !d.Leaf() || d.kind == DoorKind::Automatic || d.kind == DoorKind::Gate || open > 0.02f;
            if (!see) continue;
            const float y0 = plan_.FloorY(d.level);
            const Vector3 centre(d.pos.x, y0 + School::kDoorHeight * 0.5f, d.pos.z);
            const BoundingBox bb(centre - Vector3(d.width * 0.5f + 0.3f, School::kDoorHeight * 0.5f, d.width * 0.5f + 0.3f), centre + Vector3(d.width * 0.5f + 0.3f, School::kDoorHeight * 0.5f, d.width * 0.5f + 0.3f));
            if (!ctx.view.frustum.Intersects(bb)) continue;
            add(other, depth + 1);
        }
    }
    // Windows: from outdoors (or from a room with a window) the rooms along the facades can be seen across the campus.
    for (const auto& [id, lr] : rooms_) {
        const Room& r = plan_.room(id);
        if (r.Indoor() && r.exteriorSides != 0 && (eyeRoomHasWindows)) seen[static_cast<std::size_t>(id)] = 1;
    }
    return true;
}

void WorldRenderer::Draw(const DrawContext& ctx, const WorldLighting& light, const std::vector<School::DoorRuntime>& doors, const std::vector<School::ElevatorStatus>& elevators)
{
    stats_.roomsDrawn = 0;
    stats_.trianglesDrawn = 0;
    stats_.drawCalls = 0;
    stats_.doorsDrawn = 0;
    GraphicsDevice& device = ctx.device;
    RenderStates::SetOpaque3D(device);
    BasicEffect& fx = ctx.fx.World();
    fx.setWorldProperty(Matrix::getIdentityProperty());
    fx.setViewProperty(ctx.view.view);
    fx.setProjectionProperty(ctx.view.projection);
    fx.setAlphaProperty(1.0f);

    // Outdoors first (facade, ground, zones), tinted by the daylight.
    fx.setDiffuseColorProperty(light.outdoorTint);
    for (const Chunk& c : facade_)
        if (ctx.view.frustum.Intersects(c.bounds)) DrawBatches(ctx, c.opaque, false);
    std::vector<const LoadedRoom*> visible;
    std::vector<char> portal;
    const bool portalValid = portalCulling && ComputePortalVisibility(ctx, doors, portal);
    stats_.roomsCulled = 0;
    for (const auto& [id, lr] : rooms_) {
        if (!ctx.view.frustum.Intersects(lr->bounds)) continue;
        if (portalValid && !lr->outdoor && !lr->follower) {
            bool seen = portal[static_cast<std::size_t>(id)] != 0;
            if (!seen && lr->group >= 0)
                for (const RoomId m : groups_[static_cast<std::size_t>(lr->group)]) seen = seen || portal[static_cast<std::size_t>(m)] != 0;
            if (!seen) {
                ++stats_.roomsCulled;
                continue;
            }
        }
        visible.push_back(lr.get());
    }
    for (const LoadedRoom* lr : visible)
        if (lr->outdoor) {
            DrawBatches(ctx, lr->opaque, false);
            ++stats_.roomsDrawn;
        }
    // Indoors: the baked lights are the room's own; a room whose lights are off keeps only the daylight from its windows.
    stats_.corridorGroupsDrawn = 0;
    for (const LoadedRoom* lr : visible)
        if (!lr->outdoor && !lr->follower) {
            float on = 1.0f;
            if (light.roomLight != nullptr && lr->id < static_cast<int>(light.roomLight->size())) {
                on = (*light.roomLight)[static_cast<std::size_t>(lr->id)];
                // A corridor group shines as brightly as its brightest segment.
                if (lr->group >= 0) {
                    ++stats_.corridorGroupsDrawn;
                    for (const RoomId m : groups_[static_cast<std::size_t>(lr->group)]) on = std::max(on, (*light.roomLight)[static_cast<std::size_t>(m)]);
                }
            }
            const float dark = 0.12f + 0.5f * light.daylight;   // what a dark room looks like: daylight through the windows
            const float k = dark + (1.0f - dark) * on;
            fx.setDiffuseColorProperty(Vector3(k, k, k * 1.03f));
            DrawBatches(ctx, lr->opaque, false);
            ++stats_.roomsDrawn;
        }
    fx.setDiffuseColorProperty(Vector3(1, 1, 1));

    DrawDoors(ctx, light, doors);
    DrawElevators(ctx, elevators);

    DrawLamps(ctx, light);

    // Glass last, without depth writes, so panes blend over what is behind them.
    RenderStates::SetTransparent3D(device, BlendState::NonPremultiplied);
    fx.setDiffuseColorProperty(light.outdoorTint);
    for (const Chunk& c : facade_)
        if (!c.transparent.empty() && ctx.view.frustum.Intersects(c.bounds)) DrawBatches(ctx, c.transparent, true);
    fx.setDiffuseColorProperty(Vector3(1, 1, 1));
    for (const LoadedRoom* lr : visible)
        if (!lr->transparent.empty()) DrawBatches(ctx, lr->transparent, true);
    RenderStates::SetOpaque3D(device);
}

void WorldRenderer::DrawDoors(const DrawContext& ctx, const WorldLighting& light, const std::vector<School::DoorRuntime>& doors)
{
    // Every leaf is the unit cube, scaled and placed: one draw per leaf, for doors of loaded rooms within reach.
    BasicEffect& lit = ctx.fx.BeginLit(nullptr);
    lit.setViewProperty(ctx.view.view);
    lit.setProjectionProperty(ctx.view.projection);
    lit.setAmbientLightColorProperty(Vector3(0.45f, 0.45f, 0.5f));
    lit.setSpecularColorProperty(Vector3(0.05f, 0.05f, 0.05f));
    lit.setEmissiveColorProperty(Vector3(0, 0, 0));
    lit.setAlphaProperty(1.0f);
    DirectionalLight& l0 = lit.getDirectionalLight0Property();
    l0.setEnabledProperty(true);
    l0.setDirectionProperty(Vector3::Normalize(Vector3(0.3f, -0.8f, 0.4f)));
    l0.setDiffuseColorProperty(Vector3(0.7f, 0.68f, 0.65f));
    lit.getDirectionalLight1Property().setEnabledProperty(false);
    lit.getDirectionalLight2Property().setEnabledProperty(false);
    const LitMesh& cube = s_.shapes->Cube();
    const float maxDist2 = 60.0f * 60.0f;
    std::unordered_set<int> drawn;
    for (const RoomId id : loadedList_) {
        const Room& room = plan_.room(id);
        for (const School::DoorId did : room.doors) {
            if (!drawn.insert(did).second) continue;
            const Door& d = plan_.door(did);
            if (!d.Leaf()) continue;
            const float y0 = plan_.FloorY(d.level);
            const Vector3 centre(d.pos.x, y0 + School::kDoorHeight * 0.5f, d.pos.z);
            if (Vector3::DistanceSquared(centre, ctx.view.eye) > maxDist2) continue;
            const BoundingBox bb(centre - Vector3(d.width, School::kDoorHeight * 0.5f, d.width), centre + Vector3(d.width, School::kDoorHeight * 0.5f, d.width));
            if (!ctx.view.frustum.Intersects(bb)) continue;
            const float open = did < static_cast<int>(doors.size()) ? doors[static_cast<std::size_t>(did)].open : 0.0f;
            // Wall direction (along the leaf when closed) and the swing side (the door's normal, into room b).
            const School::Vec2 n = d.Normal();
            const School::Vec2 along = n.RightHand();
            const char* tex = d.kind == DoorKind::External || d.kind == DoorKind::ElevatorDoor ? "door_metal" : d.kind == DoorKind::Automatic || d.kind == DoorKind::Gate ? "glass" : "door_wood";
            lit.setTextureProperty(s_.textures->Get(tex));
            lit.setDiffuseColorProperty(d.kind == DoorKind::Gate ? Vector3(0.4f, 0.45f, 0.5f) : Vector3(0.95f, 0.95f, 0.95f));
            const bool twoLeaves = d.kind == DoorKind::Double || d.kind == DoorKind::External || d.kind == DoorKind::Automatic || d.kind == DoorKind::Gate || d.kind == DoorKind::ElevatorDoor;
            const int leaves = twoLeaves ? 2 : 1;
            const float leafW = twoLeaves ? d.width * 0.5f - 0.015f : d.width - 0.03f;
            const float h = (d.kind == DoorKind::Gate ? 1.6f : School::kDoorHeight) - 0.03f;
            const float t = d.kind == DoorKind::Automatic ? 0.03f : d.kind == DoorKind::ElevatorDoor ? 0.04f : 0.05f;
            for (int leaf = 0; leaf < leaves; ++leaf) {
                // hinge at an end of the opening; a single door hinges on the side the plan says
                const float side = leaves == 2 ? (leaf == 0 ? -1.0f : 1.0f) : (d.hingeLeft ? -1.0f : 1.0f);
                const School::Vec2 hinge = d.pos + along * (side * d.width * 0.5f);
                const School::Vec2 dir = along * (-side);   // from the hinge towards the other jamb
                Matrix world;
                if (d.kind == DoorKind::Automatic || d.kind == DoorKind::ElevatorDoor) {
                    // sliding: the leaf retreats along the wall
                    const School::Vec2 c = hinge + dir * (leafW * 0.5f - open * leafW);
                    const float yaw = std::atan2(-dir.z, dir.x);
                    world = Matrix::CreateScale(leafW, h, t) * Matrix::CreateRotationY(yaw) * Matrix::CreateTranslation(c.x, y0 + h * 0.5f + 0.015f, c.z);
                } else {
                    float yaw = std::atan2(-dir.z, dir.x);
                    // The leaf swings away from the corridor (into the room); an exit swings outwards; otherwise towards b.
                    School::Vec2 swing = n;
                    const bool aCirc = School::Info(plan_.room(d.a).type).circulation, bCirc = School::Info(plan_.room(d.b).type).circulation;
                    if (bCirc && !aCirc) swing = n * -1.0f;
                    if (d.exit && plan_.room(d.b).Indoor()) swing = n * -1.0f;
                    const School::Vec2 perp{dir.z, -dir.x};
                    const float sign = School::Dot(perp, swing) > 0.0f ? 1.0f : -1.0f;
                    yaw += sign * open * MathHelper::PiOver2 * 0.95f;
                    world = Matrix::CreateTranslation(0.5f, 0.5f, 0.0f) * Matrix::CreateScale(leafW, h, t) * Matrix::CreateRotationY(yaw) * Matrix::CreateTranslation(hinge.x, y0 + 0.015f, hinge.z);
                }
                lit.setWorldProperty(world);
                EffectBank::Apply(lit);
                cube.Draw(ctx.device);
                ++stats_.doorsDrawn;
                ++stats_.drawCalls;
                stats_.trianglesDrawn += cube.TriangleCount();
            }
        }
    }
    (void)light;
}

void WorldRenderer::DrawElevators(const DrawContext& ctx, const std::vector<School::ElevatorStatus>& elevators)
{
    // The cab: a metal box open towards the entry side, carried up and down the shaft at the simulation's height.
    if (elevators.empty()) return;
    BasicEffect& lit = ctx.fx.BeginLit(nullptr);
    lit.setViewProperty(ctx.view.view);
    lit.setProjectionProperty(ctx.view.projection);
    lit.setAmbientLightColorProperty(Vector3(0.62f, 0.62f, 0.68f));
    lit.setEmissiveColorProperty(Vector3(0.05f, 0.05f, 0.06f));
    lit.setAlphaProperty(1.0f);
    const LitMesh& cube = s_.shapes->Cube();
    for (const School::ElevatorStatus& st : elevators) {
        if (st.shaft < 0 || st.shaft >= static_cast<int>(plan_.elevators.size())) continue;
        const School::ElevatorShaft& e = plan_.elevators[static_cast<std::size_t>(st.shaft)];
        const School::Vec2 c = e.footprint.Center();
        const Vector3 centre(c.x, st.y + 1.2f, c.z);
        if (Vector3::DistanceSquared(centre, ctx.view.eye) > 70.0f * 70.0f) continue;
        if (!ctx.view.frustum.Intersects(BoundingBox(centre - Vector3(1.5f, 1.3f, 1.5f), centre + Vector3(1.5f, 1.3f, 1.5f)))) continue;
        const School::Vec2 n = School::SideNormal(e.entrySide);   // towards the doors
        const School::Vec2 along = n.RightHand();
        const float w = e.footprint.Width(), d = e.footprint.Depth();
        const float inner = 0.08f;
        const float yaw = std::atan2(-n.x, -n.z);                    // local -z faces the doors... local +z is the back
        const Matrix frame = Matrix::CreateRotationY(yaw) * Matrix::CreateTranslation(c.x, st.y, c.z);
        const float W = (e.entrySide == School::Side::North || e.entrySide == School::Side::South) ? w : d;
        const float D = (e.entrySide == School::Side::North || e.entrySide == School::Side::South) ? d : w;
        const auto box = [&](const Vector3& min, const Vector3& max, const Vector3& colour, const char* tex) {
            lit.setTextureProperty(s_.textures->Get(tex));
            lit.setDiffuseColorProperty(colour);
            lit.setWorldProperty(Matrix::CreateTranslation(0.5f, 0.5f, 0.5f) * Matrix::CreateScale(max - min) * Matrix::CreateTranslation(min) * frame);
            EffectBank::Apply(lit);
            cube.Draw(ctx.device);
            ++stats_.drawCalls;
            stats_.trianglesDrawn += cube.TriangleCount();
        };
        const Vector3 metal(0.92f, 0.93f, 0.96f);
        const float hw = W * 0.5f - inner, hd = D * 0.5f - inner;
        box(Vector3(-hw, 0.0f, -hd), Vector3(hw, 0.1f, hd), Vector3(0.45f, 0.45f, 0.5f), "floor_concrete");      // floor
        box(Vector3(-hw, 2.35f, -hd), Vector3(hw, 2.45f, hd), metal, "white");                                      // ceiling
        box(Vector3(-0.4f, 2.33f, -0.4f), Vector3(0.4f, 2.35f, 0.4f), Vector3(1.0f, 1.0f, 0.95f), "white");          // lamp
        box(Vector3(-hw, 0.1f, hd - 0.04f), Vector3(hw, 2.35f, hd), metal, "wall_panel");                           // back wall
        box(Vector3(-hw, 0.1f, -hd), Vector3(-hw + 0.04f, 2.35f, hd), metal, "wall_panel");                         // side walls
        box(Vector3(hw - 0.04f, 0.1f, -hd), Vector3(hw, 2.35f, hd), metal, "wall_panel");
        box(Vector3(-hw + 0.06f, 0.95f, hd - 0.1f), Vector3(hw - 0.06f, 1.0f, hd - 0.05f), Vector3(0.3f, 0.3f, 0.32f), "white");   // handrail
        box(Vector3(hw - 0.35f, 1.1f, hd - 0.09f), Vector3(hw - 0.15f, 1.5f, hd - 0.04f), Vector3(0.2f, 0.22f, 0.25f), "white");  // button panel
        (void)along;
    }
}
