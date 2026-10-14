#include "Rendering/NpcRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Rendering/EffectBank.hpp"
#include "Rendering/RenderStates.hpp"
#include "Rendering/TextureLibrary.hpp"
#include "World/Rng.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using School::Npc;
using School::Pose;
using School::Role;

namespace {

constexpr int kMaxBoxes = 2730;   // 24 vertices each: just under the 16-bit index limit
const Vector3 kLightDir = Vector3::Normalize(Vector3(0.35f, -0.8f, 0.45f));

Vector3 Hsv(float h, float s, float v)
{
    h = h - std::floor(h);
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0, g = 0, b = 0;
    const int i = static_cast<int>(h * 6.0f) % 6;
    switch (i) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
    }
    return Vector3(r + m, g + m, b + m);
}

Color ToColor(const Vector3& c)
{
    const auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return Color(b(c.X), b(c.Y), b(c.Z), 255);
}

}  // namespace

NpcRenderer::Look NpcRenderer::LookOf(const Npc& npc, const School::Population& pop)
{
    Look l;
    School::Rng rng(School::Hash(npc.look, 0x5EED, static_cast<std::uint64_t>(npc.id)));
    static const Vector3 kSkins[] = {Vector3(0.98f, 0.85f, 0.75f), Vector3(0.9f, 0.72f, 0.58f), Vector3(0.75f, 0.55f, 0.4f), Vector3(0.55f, 0.38f, 0.26f), Vector3(0.4f, 0.27f, 0.18f)};
    static const Vector3 kHairs[] = {Vector3(0.1f, 0.08f, 0.06f), Vector3(0.3f, 0.2f, 0.12f), Vector3(0.55f, 0.4f, 0.22f), Vector3(0.85f, 0.7f, 0.4f), Vector3(0.5f, 0.25f, 0.12f), Vector3(0.6f, 0.6f, 0.62f)};
    l.skin = kSkins[rng.Below(5)];
    l.hair = kHairs[rng.Below(6)];
    l.shoes = Vector3(0.12f, 0.1f, 0.1f);
    switch (npc.role) {
    case Role::Student: {
        const int year = npc.classId >= 0 ? pop.classes[static_cast<std::size_t>(npc.classId)].year : 9;
        l.height = 1.42f + 0.055f * static_cast<float>(std::clamp(year - 7, 0, 5)) + rng.Range(-0.05f, 0.07f);
        l.shirt = Hsv(rng.Uniform(), rng.Range(0.35f, 0.9f), rng.Range(0.45f, 0.95f));
        l.trousers = rng.Chance(0.6f) ? Vector3(0.2f, 0.25f, 0.4f) * rng.Range(0.7f, 1.3f) : Hsv(rng.Uniform(), 0.3f, rng.Range(0.2f, 0.5f));
        break;
    }
    case Role::Teacher:
        l.height = rng.Range(1.62f, 1.86f);
        l.shirt = rng.Chance(0.5f) ? Hsv(rng.Uniform(), 0.25f, rng.Range(0.3f, 0.6f)) : Hsv(rng.Uniform(), 0.5f, 0.8f);
        l.trousers = Vector3(0.15f, 0.15f, 0.18f) * rng.Range(0.8f, 1.6f);
        break;
    case Role::Cook:
    case Role::Nurse:
        l.height = rng.Range(1.6f, 1.82f);
        l.shirt = Vector3(0.95f, 0.95f, 0.95f);
        l.trousers = npc.role == Role::Cook ? Vector3(0.2f, 0.2f, 0.25f) : Vector3(0.9f, 0.9f, 0.95f);
        break;
    case Role::Security:
        l.height = rng.Range(1.7f, 1.9f);
        l.shirt = Vector3(0.12f, 0.15f, 0.3f);
        l.trousers = Vector3(0.1f, 0.1f, 0.14f);
        break;
    case Role::Janitor:
    case Role::Maintenance:
        l.height = rng.Range(1.62f, 1.85f);
        l.shirt = Vector3(0.3f, 0.35f, 0.42f);
        l.trousers = Vector3(0.3f, 0.35f, 0.42f);
        break;
    default:
        l.height = rng.Range(1.6f, 1.85f);
        l.shirt = Hsv(rng.Uniform(), 0.35f, rng.Range(0.5f, 0.9f));
        l.trousers = Vector3(0.2f, 0.2f, 0.25f) * rng.Range(0.8f, 1.5f);
        break;
    }
    return l;
}

NpcRenderer::NpcRenderer(Services& services) : s_(services), maxBoxes_(kMaxBoxes)
{
    vertexBuffer_ = std::make_unique<DynamicVertexBuffer>(*s_.device, VertexPositionColorTexture::getVertexDeclarationStatic(), maxBoxes_ * 24, BufferUsage::WriteOnly);
    std::vector<std::uint16_t> indices;
    indices.reserve(static_cast<std::size_t>(maxBoxes_) * 36);
    for (int b = 0; b < maxBoxes_; ++b)
        for (int face = 0; face < 6; ++face) {
            const auto v = static_cast<std::uint16_t>(b * 24 + face * 4);
            indices.insert(indices.end(), {v, static_cast<std::uint16_t>(v + 1), static_cast<std::uint16_t>(v + 2), v, static_cast<std::uint16_t>(v + 2), static_cast<std::uint16_t>(v + 3)});
        }
    indexBuffer_ = std::make_unique<IndexBuffer>(*s_.device, IndexElementSize::SixteenBits, static_cast<int>(indices.size()), BufferUsage::WriteOnly);
    indexBuffer_->SetData(indices.data(), static_cast<int>(indices.size()));
    vertices_.reserve(static_cast<std::size_t>(maxBoxes_) * 24);
}

void NpcRenderer::AddBox(const Matrix& world, const Vector3& min, const Vector3& max, const Vector3& colour)
{
    if (boxes_ >= maxBoxes_) Flush(*ctx_);
    // Six faces, counter-clockwise from outside; the shade comes from the transformed normal.
    struct Face {
        Vector3 n, a, b, c, d;
    };
    const Vector3& lo = min;
    const Vector3& hi = max;
    const Face faces[6] = {
        {Vector3(1, 0, 0), Vector3(hi.X, lo.Y, hi.Z), Vector3(hi.X, lo.Y, lo.Z), Vector3(hi.X, hi.Y, lo.Z), Vector3(hi.X, hi.Y, hi.Z)},
        {Vector3(-1, 0, 0), Vector3(lo.X, lo.Y, lo.Z), Vector3(lo.X, lo.Y, hi.Z), Vector3(lo.X, hi.Y, hi.Z), Vector3(lo.X, hi.Y, lo.Z)},
        {Vector3(0, 0, 1), Vector3(lo.X, lo.Y, hi.Z), Vector3(hi.X, lo.Y, hi.Z), Vector3(hi.X, hi.Y, hi.Z), Vector3(lo.X, hi.Y, hi.Z)},
        {Vector3(0, 0, -1), Vector3(hi.X, lo.Y, lo.Z), Vector3(lo.X, lo.Y, lo.Z), Vector3(lo.X, hi.Y, lo.Z), Vector3(hi.X, hi.Y, lo.Z)},
        {Vector3(0, 1, 0), Vector3(lo.X, hi.Y, hi.Z), Vector3(hi.X, hi.Y, hi.Z), Vector3(hi.X, hi.Y, lo.Z), Vector3(lo.X, hi.Y, lo.Z)},
        {Vector3(0, -1, 0), Vector3(lo.X, lo.Y, lo.Z), Vector3(hi.X, lo.Y, lo.Z), Vector3(hi.X, lo.Y, hi.Z), Vector3(lo.X, lo.Y, hi.Z)},
    };
    for (const Face& f : faces) {
        const Vector3 n = Vector3::Normalize(Vector3::TransformNormal(f.n, world));
        const float shade = 0.55f + 0.45f * std::max(0.0f, -Vector3::Dot(n, kLightDir)) + 0.1f * std::max(0.0f, n.Y);
        const Color c = ToColor(colour * shade);
        // Real texture coordinates even on a plain white texture: a constant uv over a triangle has zero
        // derivatives, and anisotropic sampling then returns black on the EasyGL renderer (see KNOWN_ISSUES).
        vertices_.emplace_back(Vector3::Transform(f.a, world), c, Vector2(0, 0));
        vertices_.emplace_back(Vector3::Transform(f.b, world), c, Vector2(1, 0));
        vertices_.emplace_back(Vector3::Transform(f.c, world), c, Vector2(1, 1));
        vertices_.emplace_back(Vector3::Transform(f.d, world), c, Vector2(0, 1));
    }
    ++boxes_;
}

void NpcRenderer::AddFigure(const Figure& f)
{
    const Look& k = f.look;
    const float H = k.height;
    const float legLen = H * 0.47f;
    const float thigh = legLen * 0.52f, shin = legLen * 0.48f;
    const float torsoH = H * 0.30f;
    const float headH = H * 0.13f;
    const float shoulderW = H * 0.22f;
    const float torsoD = H * 0.11f;
    const float armLen = H * 0.33f;
    const Matrix body = Matrix::CreateRotationY(f.yaw) * Matrix::CreateTranslation(f.pos);

    // Pose parameters: hip height, lean, limb pitches (positive = forward/up, about the local x axis).
    const bool sitting = f.pose == Pose::Sit || f.pose == Pose::SitWrite || f.pose == Pose::SitType || f.pose == Pose::Eat;
    float hipY = legLen;
    float leanPitch = 0.0f;
    float thighL = 0.0f, thighR = 0.0f, shinL = 0.0f, shinR = 0.0f;
    float armL = 0.0f, armR = 0.0f, foreL = 0.0f, foreR = 0.0f;
    const float t = f.time;
    if (sitting) {
        hipY = 0.46f;
        thighL = thighR = -MathHelper::PiOver2;     // horizontal, forward
        shinL = shinR = MathHelper::PiOver2;         // back down
        if (f.pose == Pose::SitWrite || f.pose == Pose::SitType) {
            armL = armR = -0.6f;
            foreL = foreR = -1.3f;
            leanPitch = 0.12f;
            if (f.pose == Pose::SitType) {
                foreL += 0.08f * std::sin(t * 9.0f);
                foreR += 0.08f * std::sin(t * 9.0f + 1.7f);
            }
        } else if (f.pose == Pose::Eat) {
            armL = -0.5f;
            armR = -0.5f;
            foreL = -1.6f;
            foreR = -1.3f + 0.6f * std::sin(t * 2.2f);
        }
    } else if (f.walking) {
        const float swing = std::sin(f.walkPhase);
        thighL = 0.55f * swing;
        thighR = -0.55f * swing;
        shinL = std::max(0.0f, 0.9f * -swing) * 0.6f;
        shinR = std::max(0.0f, 0.9f * swing) * 0.6f;
        armL = -0.5f * swing;
        armR = 0.5f * swing;
        foreL = -0.3f;
        foreR = -0.3f;
    } else {
        switch (f.pose) {
        case Pose::Lecture:
            armR = -0.9f + 0.25f * std::sin(t * 1.7f);
            foreR = -0.9f;
            armL = -0.2f;
            foreL = -0.4f;
            break;
        case Pose::Exercise: {
            const float e = std::sin(t * 3.0f);
            armL = armR = -1.6f - 1.3f * e;
            thighL = 0.2f * e;
            thighR = -0.2f * e;
            break;
        }
        case Pose::Sweep:
            leanPitch = 0.35f;
            armL = armR = -0.9f + 0.3f * std::sin(t * 2.5f);
            foreL = foreR = -0.6f;
            break;
        case Pose::Work:
        case Pose::Browse:
            armL = armR = -0.7f;
            foreL = foreR = -1.1f;
            break;
        default:
            armL = -0.05f + 0.03f * std::sin(t * 1.3f);
            armR = 0.05f;
            break;
        }
    }
    const float breathe = sitting ? 0.0f : 0.004f * std::sin(t * 1.9f);

    // Torso and head.
    const Matrix torsoM = Matrix::CreateRotationX(leanPitch) * Matrix::CreateTranslation(0, hipY + breathe, 0) * body;
    AddBox(torsoM, Vector3(-shoulderW * 0.5f, 0, -torsoD * 0.5f), Vector3(shoulderW * 0.5f, torsoH, torsoD * 0.5f), k.shirt);
    if (f.lod >= 1) {
        const float neck = torsoH + 0.02f;
        AddBox(torsoM, Vector3(-headH * 0.4f, neck, -headH * 0.45f), Vector3(headH * 0.4f, neck + headH, headH * 0.45f), k.skin);
        // hair cap
        AddBox(torsoM, Vector3(-headH * 0.42f, neck + headH * 0.62f, -headH * 0.42f), Vector3(headH * 0.42f, neck + headH + 0.01f, headH * 0.47f), k.hair);
    } else {
        AddBox(torsoM, Vector3(-headH * 0.4f, torsoH + 0.02f, -headH * 0.45f), Vector3(headH * 0.4f, torsoH + 0.02f + headH, headH * 0.45f), k.skin);
    }
    if (f.lod == 0) {
        // One block for both legs.
        AddBox(body, Vector3(-shoulderW * 0.4f, 0, -torsoD * 0.45f), Vector3(shoulderW * 0.4f, hipY, torsoD * 0.45f), k.trousers);
        return;
    }
    // Legs: thigh from the hip down, shin from the knee down.
    const float hipX = shoulderW * 0.22f;
    const float legW = H * 0.075f;
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -hipX : hipX;
        const float tp = side == 0 ? thighL : thighR;
        const float sp = side == 0 ? shinL : shinR;
        const Matrix thighM = Matrix::CreateRotationX(tp) * Matrix::CreateTranslation(x, hipY, 0) * body;
        if (f.lod >= 2 || sitting) {
            AddBox(thighM, Vector3(-legW * 0.5f, -thigh, -legW * 0.5f), Vector3(legW * 0.5f, 0.02f, legW * 0.5f), k.trousers);
            const Matrix shinM = Matrix::CreateRotationX(sp) * Matrix::CreateTranslation(0, -thigh, 0) * thighM;
            AddBox(shinM, Vector3(-legW * 0.45f, -shin, -legW * 0.45f), Vector3(legW * 0.45f, 0.0f, legW * 0.45f), k.trousers * 0.95f);
            AddBox(shinM, Vector3(-legW * 0.5f, -shin - 0.01f, -legW * 0.9f), Vector3(legW * 0.5f, -shin + 0.06f, legW * 0.5f), k.shoes);
        } else {
            AddBox(thighM, Vector3(-legW * 0.5f, -legLen, -legW * 0.5f), Vector3(legW * 0.5f, 0.02f, legW * 0.5f), k.trousers);
        }
    }
    if (f.lod < 2) return;
    // Arms: upper arm from the shoulder, forearm from the elbow.
    const float armW = H * 0.055f;
    const float upper = armLen * 0.5f, fore = armLen * 0.5f;
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -(shoulderW * 0.5f + armW * 0.55f) : (shoulderW * 0.5f + armW * 0.55f);
        const float ap = side == 0 ? armL : armR;
        const float fp = side == 0 ? foreL : foreR;
        const Matrix armM = Matrix::CreateRotationX(ap) * Matrix::CreateTranslation(x, torsoH - 0.03f, 0) * torsoM;
        AddBox(armM, Vector3(-armW * 0.5f, -upper, -armW * 0.5f), Vector3(armW * 0.5f, 0.03f, armW * 0.5f), k.shirt * 0.95f);
        const Matrix foreM = Matrix::CreateRotationX(fp) * Matrix::CreateTranslation(0, -upper, 0) * armM;
        AddBox(foreM, Vector3(-armW * 0.45f, -fore, -armW * 0.45f), Vector3(armW * 0.45f, 0.0f, armW * 0.45f), k.skin);
    }
}

void NpcRenderer::Flush(const DrawContext& ctx)
{
    if (boxes_ == 0) return;
    vertexBuffer_->SetData(vertices_.data(), 0, static_cast<int>(vertices_.size()), SetDataOptions::Discard);
    ctx.device.SetVertexBuffer(vertexBuffer_.get());
    ctx.device.setIndicesProperty(indexBuffer_.get());
    ctx.device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, static_cast<int>(vertices_.size()), 0, boxes_ * 12);
    vertices_.clear();
    boxes_ = 0;
}

void NpcRenderer::Draw(const DrawContext& ctx, const School::SchoolSim& sim, const WorldRenderer& world, NpcDrawStats& stats)
{
    ctx_ = &ctx;
    stats = NpcDrawStats{};
    const School::Population& pop = sim.Pop();
    const School::SchoolPlan& plan = *s_.plan;
    const Vector3 eye = ctx.view.eye;
    const float far2 = farRadius * farRadius;
    vertices_.clear();
    boxes_ = 0;
    RenderStates::SetOpaque3D(ctx.device);
    BasicEffect& fx = ctx.fx.World();
    fx.setWorldProperty(Matrix::getIdentityProperty());
    fx.setViewProperty(ctx.view.view);
    fx.setProjectionProperty(ctx.view.projection);
    fx.setDiffuseColorProperty(Vector3(1, 1, 1));
    fx.setTextureProperty(s_.textures->White());
    EffectBank::Apply(fx);
    const double now = sim.Time();
    for (const Npc& npc : pop.npcs) {
        const School::Agent& a = sim.AgentOf(npc.id);
        if (a.mode == School::Mode::Offsite) continue;
        ++stats.onCampus;
        if (a.mode == School::Mode::Transit) ++stats.walking;
        School::Vec3 p;
        float yaw;
        School::RoomId room;
        sim.Observe(npc.id, p, yaw, room);
        const Vector3 pos(p.x, p.y, p.z);
        const float d2 = Vector3::DistanceSquared(pos, eye);
        if (d2 > far2) continue;
        if (room != School::kNone && !world.IsLoaded(room)) continue;
        // Inside a cab, only full-fidelity riders are at the cab's real height; the others are behind closed doors.
        if (room != School::kNone && plan.room(room).type == School::RoomType::ElevatorCab && a.fidelity != School::Fidelity::Full) continue;
        if (!ctx.view.frustum.Intersects(BoundingSphere(pos + Vector3(0, 0.9f, 0), 1.1f))) continue;
        Figure f;
        f.pos = pos;
        f.yaw = yaw;
        f.pose = a.mode == School::Mode::Transit ? Pose::Stand : a.pose;
        f.walking = a.mode == School::Mode::Transit;
        const float phaseSeed = static_cast<float>(npc.id % 97) * 0.37f;
        f.walkPhase = a.fidelity == School::Fidelity::Full ? a.walkPhase : static_cast<float>(now) * npc.speed * 3.2f + phaseSeed;
        f.time = static_cast<float>(now) + phaseSeed * 3.0f;
        const float d = std::sqrt(d2);
        f.lod = d < detailRadius ? 2 : (d < mediumRadius ? 1 : 0);
        if (looks_.size() != pop.npcs.size()) {
            looks_.clear();
            looks_.reserve(pop.npcs.size());
            for (const Npc& n : pop.npcs) looks_.push_back(LookOf(n, pop));
        }
        f.look = looks_[static_cast<std::size_t>(npc.id)];
        AddFigure(f);
        ++stats.drawn;
        if (f.lod == 2) ++stats.detailed;
    }
    Flush(ctx);
    (void)plan;
}
