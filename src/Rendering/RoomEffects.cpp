#include "Rendering/RoomEffects.hpp"

#include <algorithm>
#include <cmath>

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Rendering/EffectBank.hpp"
#include "Rendering/LitMesh.hpp"
#include "Rendering/RenderStates.hpp"
#include "World/Rng.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using School::Fixture;
using School::FixtureType;
using School::Room;
using School::RoomType;

namespace {

bool Animated(RoomType t)
{
    return t == RoomType::PhysicsLab || t == RoomType::ChemistryLab || t == RoomType::BiologyLab || t == RoomType::Workshop || t == RoomType::Gym || t == RoomType::ArtStudio ||
           t == RoomType::MusicRoom || t == RoomType::Kitchen || t == RoomType::ServerRoom;
}

/** World transform of a fixture's local frame (origin at its base centre). */
Matrix FixtureFrame(const Fixture& f, float baseY)
{
    return Matrix::CreateRotationY(f.yaw) * Matrix::CreateTranslation(f.pos.x, baseY + f.y0, f.pos.z);
}

const Fixture* FirstOf(const School::RoomLayout& l, FixtureType t)
{
    for (const Fixture& f : l.fixtures)
        if (f.type == t) return &f;
    return nullptr;
}

}  // namespace

void RoomEffects::Update(float dt, const Vector3& eye, const WorldRenderer& world)
{
    // Rooms in range keep a state; the others are forgotten (and start afresh when they come back).
    std::map<int, RoomState> next;
    for (const School::RoomId id : world.LoadedRooms()) {
        const Room& r = plan_.room(id);
        if (!Animated(r.type)) continue;
        const School::Vec2 c = r.rect.Clamp(School::Vec2{eye.X, eye.Z});
        const float dx = c.x - eye.X, dz = c.z - eye.Z, dy = plan_.FloorY(r.level) - eye.Y;
        if (dx * dx + dz * dz + dy * dy > range_ * range_) continue;
        auto it = active_.find(id);
        RoomState st = it != active_.end() ? std::move(it->second) : RoomState{};
        st.room = id;
        st.time += dt;
        if (r.type == RoomType::Gym && st.balls.empty()) {
            School::Rng rng(School::Hash(static_cast<std::uint64_t>(id), 0xBA11));
            for (int i = 0; i < 4; ++i) {
                Ball b;
                b.pos = Vector3(r.rect.x0 + r.rect.Width() * rng.Range(0.3f, 0.7f), plan_.FloorY(r.level) + rng.Range(1.0f, 3.0f), r.rect.z0 + r.rect.Depth() * rng.Range(0.3f, 0.7f));
                b.vel = Vector3(rng.Range(-2.0f, 2.0f), 0.0f, rng.Range(-2.0f, 2.0f));
                st.balls.push_back(b);
            }
        }
        if (r.type == RoomType::Gym) {
            const float floor = plan_.FloorY(r.level) + 0.12f;
            for (Ball& b : st.balls) {
                b.vel.Y -= 9.81f * dt;
                b.pos += b.vel * dt;
                if (b.pos.Y < floor) {
                    b.pos.Y = floor;
                    b.vel.Y = std::fabs(b.vel.Y) * 0.78f + 0.4f;   // a little energy back in: the balls never stop
                    b.vel.X *= 0.98f;
                    b.vel.Z *= 0.98f;
                }
                if (b.pos.X < r.rect.x0 + 1.0f || b.pos.X > r.rect.x1 - 1.0f) b.vel.X = -b.vel.X;
                if (b.pos.Z < r.rect.z0 + 1.0f || b.pos.Z > r.rect.z1 - 1.0f) b.vel.Z = -b.vel.Z;
                b.pos.X = std::clamp(b.pos.X, r.rect.x0 + 1.0f, r.rect.x1 - 1.0f);
                b.pos.Z = std::clamp(b.pos.Z, r.rect.z0 + 1.0f, r.rect.z1 - 1.0f);
            }
        }
        next[id] = std::move(st);
    }
    active_ = std::move(next);
}

void RoomEffects::DrawShape(const DrawContext& ctx, const LitMesh& mesh, const Matrix& world, const Vector3& colour, float alpha)
{
    BasicEffect& lit = ctx.fx.Lit();
    lit.setWorldProperty(world);
    lit.setDiffuseColorProperty(colour);
    lit.setAlphaProperty(alpha);
    EffectBank::Apply(lit);
    mesh.Draw(ctx.device);
}

void RoomEffects::DrawLab(const DrawContext& ctx, const Room& r, RoomState& st)
{
    const School::RoomLayout& l = layouts_.Get(r.id);
    const Fixture* bench = FirstOf(l, FixtureType::LabBench);
    if (bench == nullptr) return;
    const float y0 = plan_.FloorY(r.level);
    const Matrix frame = FixtureFrame(*bench, y0);
    const float top = bench->sy;
    const float t = st.time;
    const Shapes& sh = *s_.shapes;
    const Vector3 metal(0.75f, 0.76f, 0.8f);
    if (r.type == RoomType::PhysicsLab) {
        // A pendulum on a stand at one end of the bench.
        const Vector3 pivot(-bench->sx * 0.3f, top + 0.9f, 0.0f);
        DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.03f, 0.9f, 0.03f) * Matrix::CreateTranslation(pivot.X + 0.25f, top, pivot.Z) * frame, metal);
        DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.3f, 0.02f, 0.02f) * Matrix::CreateTranslation(pivot.X + 0.1f, pivot.Y, pivot.Z) * frame, metal);
        const float angle = 0.75f * std::sin(t * 2.4f);
        const float len = 0.7f;
        const Matrix swing = Matrix::CreateRotationZ(angle) * Matrix::CreateTranslation(pivot) * frame;
        DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.012f, len, 0.012f) * Matrix::CreateTranslation(0, -len, 0) * swing, Vector3(0.3f, 0.3f, 0.3f));
        DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.12f) * Matrix::CreateTranslation(0, -len, 0) * swing, Vector3(0.85f, 0.2f, 0.15f));
        // A launched ball: a parabola from the other end of the bench, every three seconds.
        const float period = 3.0f;
        const float u = std::fmod(t, period);
        const float v0 = 3.2f;
        const float x = bench->sx * 0.3f - u * 1.4f;
        const float y = top + 0.25f + v0 * u - 0.5f * 9.81f * u * u;
        if (y > top + 0.05f) DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.08f) * Matrix::CreateTranslation(x, y, 0.15f) * frame, Vector3(0.2f, 0.4f, 0.9f));
        DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.25f, 0.2f, 0.25f) * Matrix::CreateTranslation(bench->sx * 0.3f, top + 0.1f, 0.15f) * frame, Vector3(0.35f, 0.35f, 0.4f));
    } else if (r.type == RoomType::ChemistryLab) {
        // Three flasks with coloured liquid and rising bubbles.
        static const Vector3 kLiquids[] = {Vector3(0.2f, 0.8f, 0.4f), Vector3(0.9f, 0.5f, 0.1f), Vector3(0.3f, 0.4f, 1.0f)};
        for (int i = 0; i < 3; ++i) {
            const float x = (static_cast<float>(i) - 1.0f) * 0.45f;
            DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.16f, 0.22f, 0.16f) * Matrix::CreateTranslation(x, top, 0.0f) * frame, Vector3(0.85f, 0.9f, 0.95f), 0.35f);
            DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.14f, 0.14f, 0.14f) * Matrix::CreateTranslation(x, top + 0.005f, 0.0f) * frame, kLiquids[i]);
            DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.06f, 0.12f, 0.06f) * Matrix::CreateTranslation(x, top + 0.22f, 0.0f) * frame, Vector3(0.85f, 0.9f, 0.95f), 0.35f);
            for (int b = 0; b < 4; ++b) {
                const float phase = std::fmod(t * (0.6f + 0.15f * static_cast<float>(b)) + static_cast<float>(b) * 0.37f + static_cast<float>(i) * 0.5f, 1.0f);
                const float bx = x + 0.05f * std::sin(phase * 12.0f + static_cast<float>(b));
                DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.02f + 0.015f * phase) * Matrix::CreateTranslation(bx, top + 0.03f + phase * 0.3f, 0.0f) * frame, Vector3(1, 1, 1), 0.7f);
            }
        }
        // a slow vapour wisp above the middle flask
        const float wisp = std::fmod(t * 0.3f, 1.0f);
        DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.1f + 0.2f * wisp) * Matrix::CreateTranslation(0.0f, top + 0.4f + wisp * 0.6f, 0.0f) * frame, Vector3(0.9f, 0.95f, 1.0f), 0.25f * (1.0f - wisp));
    } else {
        // Biology: a turning double helix on a stand.
        const float turn = t * 0.8f;
        DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.3f, 0.03f, 0.3f) * Matrix::CreateTranslation(0.0f, top, 0.0f) * frame, Vector3(0.25f, 0.25f, 0.28f));
        for (int k = 0; k < 14; ++k) {
            const float h = top + 0.08f + static_cast<float>(k) * 0.065f;
            const float a = turn + static_cast<float>(k) * 0.55f;
            const Vector3 p1(std::cos(a) * 0.14f, h, std::sin(a) * 0.14f);
            const Vector3 p2(-p1.X, h, -p1.Z);
            DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.045f) * Matrix::CreateTranslation(p1) * frame, Vector3(0.9f, 0.3f, 0.3f));
            DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.045f) * Matrix::CreateTranslation(p2) * frame, Vector3(0.3f, 0.5f, 0.95f));
            DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.26f, 0.012f, 0.012f) * Matrix::CreateRotationY(-a) * Matrix::CreateTranslation(0, h, 0) * frame, Vector3(0.8f, 0.8f, 0.6f));
        }
    }
}

void RoomEffects::DrawWorkshop(const DrawContext& ctx, const Room& r, RoomState& st)
{
    const School::RoomLayout& l = layouts_.Get(r.id);
    const float y0 = plan_.FloorY(r.level);
    const Shapes& sh = *s_.shapes;
    int i = 0;
    for (const Fixture& f : l.fixtures) {
        if (f.type != FixtureType::Machine) continue;
        const Matrix frame = FixtureFrame(f, y0);
        const float phase = st.time * (3.0f + static_cast<float>(i % 3)) + static_cast<float>(i);
        // a spinning wheel on the drum and a piston going up and down beside it
        const float hx = f.sx * 0.5f;
        DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.26f, 0.03f, 0.03f) * Matrix::CreateRotationY(phase) * Matrix::CreateTranslation(hx * 0.2f, f.sy + 0.09f, 0.0f) * frame, Vector3(0.85f, 0.75f, 0.2f));
        DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.03f, 0.03f, 0.26f) * Matrix::CreateRotationY(phase) * Matrix::CreateTranslation(hx * 0.2f, f.sy + 0.09f, 0.0f) * frame, Vector3(0.85f, 0.75f, 0.2f));
        const float stroke = 0.08f * (0.5f + 0.5f * std::sin(phase * 2.0f));
        DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.08f, 0.18f, 0.08f) * Matrix::CreateTranslation(-hx * 0.4f, f.sy * 0.65f + stroke, 0.0f) * frame, Vector3(0.7f, 0.72f, 0.75f));
        ++i;
    }
}

void RoomEffects::DrawArt(const DrawContext& ctx, const Room& r, RoomState& st)
{
    // A still life on the central table, lit per pixel with a strong specular term: the materials demonstration.
    const School::RoomLayout& l = layouts_.Get(r.id);
    const Fixture* table = FirstOf(l, FixtureType::Table);
    if (table == nullptr) return;
    const float y0 = plan_.FloorY(r.level);
    const Matrix frame = FixtureFrame(*table, y0);
    const float top = table->sy;
    const Shapes& sh = *s_.shapes;
    BasicEffect& lit = ctx.fx.Lit();
    lit.setPreferPerPixelLightingProperty(true);
    lit.setSpecularColorProperty(Vector3(0.9f, 0.9f, 0.9f));
    lit.setSpecularPowerProperty(48.0f);
    const float spin = st.time * 0.4f;
    // a glossy sphere, a matte cone, a tall vase (stacked cylinders) and a cube, slowly turning on a plate
    DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(1.0f, 0.03f, 1.0f) * Matrix::CreateRotationY(spin) * Matrix::CreateTranslation(0, top, 0) * frame, Vector3(0.35f, 0.3f, 0.28f));
    DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.3f) * Matrix::CreateTranslation(0.25f, top + 0.18f, 0.1f) * Matrix::CreateRotationY(spin) * frame, Vector3(0.9f, 0.2f, 0.2f));
    lit.setSpecularColorProperty(Vector3(0.05f, 0.05f, 0.05f));
    DrawShape(ctx, sh.Cone(), Matrix::CreateScale(0.26f, 0.4f, 0.26f) * Matrix::CreateTranslation(-0.25f, top + 0.03f, 0.15f) * Matrix::CreateRotationY(spin) * frame, Vector3(0.95f, 0.85f, 0.55f));
    lit.setSpecularColorProperty(Vector3(0.6f, 0.6f, 0.7f));
    lit.setSpecularPowerProperty(16.0f);
    DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.14f, 0.35f, 0.14f) * Matrix::CreateTranslation(-0.05f, top + 0.03f, -0.25f) * Matrix::CreateRotationY(spin) * frame, Vector3(0.2f, 0.35f, 0.6f));
    DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.1f, 0.12f, 0.1f) * Matrix::CreateTranslation(-0.05f, top + 0.38f, -0.25f) * Matrix::CreateRotationY(spin) * frame, Vector3(0.2f, 0.35f, 0.6f));
    DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.2f) * Matrix::CreateRotationY(0.6f) * Matrix::CreateTranslation(0.25f, top + 0.13f, -0.25f) * Matrix::CreateRotationY(spin) * frame, Vector3(0.9f, 0.9f, 0.9f));
    lit.setPreferPerPixelLightingProperty(false);
    lit.setSpecularColorProperty(Vector3(0.25f, 0.25f, 0.25f));
    lit.setSpecularPowerProperty(24.0f);
}

void RoomEffects::DrawMusic(const DrawContext& ctx, const Room& r, RoomState& st)
{
    // A pyramid-shaped metronome on the piano: the arm swings once per beat pair, 72 beats a minute.
    const School::RoomLayout& l = layouts_.Get(r.id);
    const Fixture* piano = FirstOf(l, FixtureType::Piano);
    if (piano == nullptr) return;
    const Matrix frame = FixtureFrame(*piano, plan_.FloorY(r.level));
    const Shapes& sh = *s_.shapes;
    const float top = piano->sy;
    const Vector3 at(piano->sx * 0.32f, top, 0.0f);
    DrawShape(ctx, sh.Cone(), Matrix::CreateScale(0.1f, 0.22f, 0.1f) * Matrix::CreateTranslation(at) * frame, Vector3(0.45f, 0.28f, 0.16f));
    DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.14f, 0.02f, 0.1f) * Matrix::CreateTranslation(at.X, top, at.Z) * frame, Vector3(0.3f, 0.19f, 0.1f));
    const float beat = st.time * 72.0f / 60.0f;                       // beats so far
    const float angle = 0.5f * std::sin(beat * 3.14159265f);          // one full swing every two beats
    const Matrix swing = Matrix::CreateRotationZ(angle) * Matrix::CreateTranslation(at.X, top + 0.03f, at.Z - 0.052f) * frame;
    DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.006f, 0.2f, 0.006f) * swing, Vector3(0.8f, 0.8f, 0.82f));
    DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.035f, 0.028f, 0.02f) * Matrix::CreateTranslation(0, 0.13f, 0) * swing, Vector3(0.85f, 0.7f, 0.2f));
}

void RoomEffects::DrawKitchen(const DrawContext& ctx, const Room& r, RoomState& st)
{
    // A pot on every counter and a column of steam above it: soft white puffs that rise, swell and fade.
    const School::RoomLayout& l = layouts_.Get(r.id);
    const float y0 = plan_.FloorY(r.level);
    const Shapes& sh = *s_.shapes;
    int n = 0;
    for (const Fixture& f : l.fixtures) {
        if (f.type != FixtureType::Counter) continue;
        const Matrix frame = FixtureFrame(f, y0);
        const float x = (n % 2 == 0 ? -0.25f : 0.25f) * f.sx;
        DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.2f, 0.2f, 0.2f) * Matrix::CreateTranslation(x, f.sy, 0.0f) * frame, Vector3(0.55f, 0.57f, 0.6f));
        DrawShape(ctx, sh.Cylinder(), Matrix::CreateScale(0.17f, 0.012f, 0.17f) * Matrix::CreateTranslation(x, f.sy + 0.19f, 0.0f) * frame, Vector3(0.9f, 0.62f, 0.2f));   // the soup
        for (int k = 0; k < 5; ++k) {
            const float phase = std::fmod(st.time * 0.32f + static_cast<float>(k) * 0.2f + static_cast<float>(n) * 0.13f, 1.0f);
            const float sway = 0.06f * std::sin(phase * 7.0f + static_cast<float>(k + n));
            DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.05f + 0.16f * phase) * Matrix::CreateTranslation(x + sway, f.sy + 0.24f + phase * 1.1f, sway * 0.7f) * frame,
                      Vector3(0.95f, 0.96f, 0.98f), 0.38f * (1.0f - phase));
        }
        ++n;
    }
}

void RoomEffects::DrawServer(const DrawContext& ctx, const Vector3& eye, const Room& r, RoomState& st)
{
    // Activity lights on the racks: each rack unit has an amber or blue light that flickers at its own rate. Only racks
    // within a few metres are drawn; further away they are too small to matter.
    const School::RoomLayout& l = layouts_.Get(r.id);
    const float y0 = plan_.FloorY(r.level);
    const Shapes& sh = *s_.shapes;
    BasicEffect& lit = ctx.fx.Lit();
    int rack = 0;
    for (const Fixture& f : l.fixtures) {
        if (f.type != FixtureType::Rack) continue;
        ++rack;
        const Matrix frame = FixtureFrame(f, y0);
        const Vector3 centre = Vector3::Transform(Vector3(0, f.sy * 0.5f, 0), frame);
        if (Vector3::DistanceSquared(centre, eye) > 12.0f * 12.0f) continue;
        const float hz = f.sz * 0.5f;
        const int steps = static_cast<int>(st.time * 7.0f);
        for (int k = 0; k < 14; ++k) {
            const float y = 0.2f + static_cast<float>(k) * 0.12f;
            if (y > f.sy - 0.4f) break;
            const std::uint64_t h = School::Hash(static_cast<std::uint64_t>(rack), static_cast<std::uint64_t>(k), static_cast<std::uint64_t>(steps + k * 3));
            const float u = static_cast<float>(h % 1000U) / 1000.0f;
            if (u > 0.55f) continue;
            const Vector3 colour = (h >> 20U) % 3U == 0U ? Vector3(0.2f, 0.5f, 1.0f) : Vector3(1.0f, 0.6f, 0.1f);
            lit.setEmissiveColorProperty(colour);
            DrawShape(ctx, sh.Cube(), Matrix::CreateScale(0.014f, 0.008f, 0.006f) * Matrix::CreateTranslation(f.sx * 0.5f - 0.1f - 0.025f * static_cast<float>(h % 3U), y, -hz - 0.04f) * frame, Vector3(0, 0, 0));
        }
    }
    lit.setEmissiveColorProperty(Vector3(0, 0, 0));
}

void RoomEffects::DrawGym(const DrawContext& ctx, const Room&, RoomState& st)
{
    const Shapes& sh = *s_.shapes;
    for (const Ball& b : st.balls) DrawShape(ctx, sh.Sphere(), Matrix::CreateScale(0.24f) * Matrix::CreateTranslation(b.pos), Vector3(0.9f, 0.45f, 0.15f));
}

void RoomEffects::Draw(const DrawContext& ctx)
{
    if (active_.empty()) return;
    RenderStates::SetOpaque3D(ctx.device);
    BasicEffect& lit = ctx.fx.BeginLit(nullptr);
    lit.setViewProperty(ctx.view.view);
    lit.setProjectionProperty(ctx.view.projection);
    lit.setAmbientLightColorProperty(Vector3(0.4f, 0.4f, 0.45f));
    lit.setSpecularColorProperty(Vector3(0.25f, 0.25f, 0.25f));
    lit.setSpecularPowerProperty(24.0f);
    lit.setEmissiveColorProperty(Vector3(0, 0, 0));
    DirectionalLight& l0 = lit.getDirectionalLight0Property();
    l0.setEnabledProperty(true);
    l0.setDirectionProperty(Vector3::Normalize(Vector3(0.3f, -0.8f, 0.4f)));
    l0.setDiffuseColorProperty(Vector3(0.75f, 0.73f, 0.7f));
    DirectionalLight& l1 = lit.getDirectionalLight1Property();
    l1.setEnabledProperty(true);
    l1.setDirectionProperty(Vector3::Normalize(Vector3(-0.6f, -0.5f, -0.3f)));
    l1.setDiffuseColorProperty(Vector3(0.35f, 0.38f, 0.45f));
    l1.setSpecularColorProperty(Vector3(0.5f, 0.5f, 0.6f));
    lit.getDirectionalLight2Property().setEnabledProperty(false);
    // Opaque first, then the translucent glassware (alpha < 1 draws are issued in the same pass; the
    // NonPremultiplied blend handles both, with depth writes on so the small shapes sort well enough).
    ctx.device.setBlendStateProperty(BlendState::NonPremultiplied);
    for (auto& [id, st] : active_) {
        const Room& r = plan_.room(id);
        switch (r.type) {
        case RoomType::PhysicsLab:
        case RoomType::ChemistryLab:
        case RoomType::BiologyLab: DrawLab(ctx, r, st); break;
        case RoomType::Workshop: DrawWorkshop(ctx, r, st); break;
        case RoomType::Gym: DrawGym(ctx, r, st); break;
        case RoomType::ArtStudio: DrawArt(ctx, r, st); break;
        case RoomType::MusicRoom: DrawMusic(ctx, r, st); break;
        case RoomType::Kitchen: DrawKitchen(ctx, r, st); break;
        case RoomType::ServerRoom: DrawServer(ctx, ctx.view.eye, r, st); break;
        default: break;
        }
    }
    lit.setAlphaProperty(1.0f);
    ctx.device.setBlendStateProperty(BlendState::Opaque);
}
