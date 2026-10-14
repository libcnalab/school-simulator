#include "Rendering/WorldRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "App/Log.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    School::Log("world", "facade built: %zu chunks in %.0f ms", facade_.size(), ms);
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

void WorldRenderer::Build(RoomId id)
{
    if (rooms_.count(id)) return;
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
            if (d < radius) wanted.push_back({d + static_cast<float>(dl) * 20.0f, r.id});
        }
        std::sort(wanted.begin(), wanted.end(), [](const Want& a, const Want& b) { return a.score < b.score; });
        if (static_cast<int>(wanted.size()) > maxLoaded) wanted.resize(static_cast<std::size_t>(maxLoaded));
        std::unordered_set<int> keep;
        buildQueue_.clear();
        for (const Want& w : wanted) {
            keep.insert(w.id);
            if (!rooms_.count(w.id)) buildQueue_.push_back(w.id);
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
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > 12.0) break;
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

void WorldRenderer::Draw(const DrawContext& ctx, const WorldLighting& light, const std::vector<School::DoorRuntime>& doors)
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
    for (const auto& [id, lr] : rooms_) {
        if (!ctx.view.frustum.Intersects(lr->bounds)) continue;
        visible.push_back(lr.get());
    }
    for (const LoadedRoom* lr : visible)
        if (lr->outdoor) {
            DrawBatches(ctx, lr->opaque, false);
            ++stats_.roomsDrawn;
        }
    // Indoors: the baked lights are the room's own, independent of the time of day.
    fx.setDiffuseColorProperty(Vector3(1, 1, 1));
    for (const LoadedRoom* lr : visible)
        if (!lr->outdoor) {
            DrawBatches(ctx, lr->opaque, false);
            ++stats_.roomsDrawn;
        }

    DrawDoors(ctx, light, doors);

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
            const bool twoLeaves = d.kind == DoorKind::Double || d.kind == DoorKind::External || d.kind == DoorKind::Automatic || d.kind == DoorKind::Gate;
            const int leaves = twoLeaves ? 2 : 1;
            const float leafW = twoLeaves ? d.width * 0.5f - 0.015f : d.width - 0.03f;
            const float h = (d.kind == DoorKind::Gate ? 1.6f : School::kDoorHeight) - 0.03f;
            const float t = d.kind == DoorKind::Automatic ? 0.03f : 0.05f;
            for (int leaf = 0; leaf < leaves; ++leaf) {
                // hinge at an end of the opening; a single door hinges on the side the plan says
                const float side = leaves == 2 ? (leaf == 0 ? -1.0f : 1.0f) : (d.hingeLeft ? -1.0f : 1.0f);
                const School::Vec2 hinge = d.pos + along * (side * d.width * 0.5f);
                const School::Vec2 dir = along * (-side);   // from the hinge towards the other jamb
                Matrix world;
                if (d.kind == DoorKind::Automatic) {
                    // sliding: the leaf retreats along the wall
                    const School::Vec2 c = hinge + dir * (leafW * 0.5f - open * leafW);
                    const float yaw = std::atan2(-dir.z, dir.x);
                    world = Matrix::CreateScale(leafW, h, t) * Matrix::CreateRotationY(yaw) * Matrix::CreateTranslation(c.x, y0 + h * 0.5f + 0.015f, c.z);
                } else {
                    float yaw = std::atan2(-dir.z, dir.x);
                    // +delta turns local +x towards (sin yaw... ) : pick the sign that swings towards the normal
                    const School::Vec2 perp{dir.z, -dir.x};
                    const float sign = School::Dot(perp, n) > 0.0f ? 1.0f : -1.0f;
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
