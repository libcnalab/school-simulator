#include "Rendering/Gizmos.hpp"

#include <algorithm>
#include <cmath>

#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Rendering/EffectBank.hpp"
#include "Rendering/LitMesh.hpp"
#include "Rendering/RenderStates.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

Matrix AlignYTo(const Vector3& d)
{
    Vector3 y = d;
    if (y.LengthSquared() < 1e-10f) y = Vector3::Up;
    y = Vector3::Normalize(y);
    Vector3 helper = std::abs(y.Y) > 0.95f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
    const Vector3 x = Vector3::Normalize(Vector3::Cross(helper, y));
    const Vector3 z = Vector3::Cross(x, y);
    return Matrix(x.X, x.Y, x.Z, 0, y.X, y.Y, y.Z, 0, z.X, z.Y, z.Z, 0, 0, 0, 0, 1);
}

void Gizmos::Line(const Vector3& a, const Vector3& b, const Color& c)
{
    lines_.emplace_back(a, c);
    lines_.emplace_back(b, c);
}

void Gizmos::Arrow(const Vector3& from, const Vector3& to, const Color& c, float head)
{
    Line(from, to, c);
    const Vector3 d = to - from;
    const float len = d.Length();
    if (len < 1e-4f) return;
    const Vector3 dir = d / len;
    const Vector3 helper = std::abs(dir.Y) > 0.95f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
    const Vector3 side = Vector3::Normalize(Vector3::Cross(helper, dir)) * (head * 0.5f);
    const Vector3 up = Vector3::Normalize(Vector3::Cross(dir, side)) * (head * 0.5f);
    const Vector3 base = to - dir * head;
    for (const Vector3& o : {side, -side, up, -up}) Line(to, base + o, c);
}

void Gizmos::Box(const BoundingBox& b, const Color& c)
{
    const Vector3 lo = b.Min, hi = b.Max;
    const Vector3 v[8] = {{lo.X, lo.Y, lo.Z}, {hi.X, lo.Y, lo.Z}, {hi.X, lo.Y, hi.Z}, {lo.X, lo.Y, hi.Z},
                          {lo.X, hi.Y, lo.Z}, {hi.X, hi.Y, lo.Z}, {hi.X, hi.Y, hi.Z}, {lo.X, hi.Y, hi.Z}};
    for (int i = 0; i < 4; ++i) {
        Line(v[i], v[(i + 1) % 4], c);
        Line(v[4 + i], v[4 + (i + 1) % 4], c);
        Line(v[i], v[4 + i], c);
    }
}

void Gizmos::Circle(const Vector3& centre, const Vector3& normal, float radius, const Color& c, int segments)
{
    const Vector3 n = Vector3::Normalize(normal);
    const Vector3 helper = std::abs(n.Y) > 0.95f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
    const Vector3 u = Vector3::Normalize(Vector3::Cross(helper, n));
    const Vector3 v = Vector3::Cross(n, u);
    Vector3 prev = centre + u * radius;
    for (int i = 1; i <= segments; ++i) {
        const float a = MathHelper::TwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const Vector3 p = centre + (u * std::cos(a) + v * std::sin(a)) * radius;
        Line(prev, p, c);
        prev = p;
    }
}

void Gizmos::Sphere(const BoundingSphere& s, const Color& c, int segments)
{
    Circle(s.Center, Vector3(1, 0, 0), s.Radius, c, segments);
    Circle(s.Center, Vector3(0, 1, 0), s.Radius, c, segments);
    Circle(s.Center, Vector3(0, 0, 1), s.Radius, c, segments);
}

void Gizmos::Frustum(const BoundingFrustum& f, const Color& c)
{
    const std::vector<Vector3> corners = f.GetCorners();
    if (corners.size() < 8) return;
    for (int i = 0; i < 4; ++i) {
        Line(corners[i], corners[(i + 1) % 4], c);
        Line(corners[4 + i], corners[4 + (i + 1) % 4], c);
        Line(corners[i], corners[4 + i], c);
    }
}

void Gizmos::Axes(const Matrix& world, float length)
{
    const Vector3 o = world.getTranslationProperty();
    Arrow(o, o + Vector3::TransformNormal(Vector3(length, 0, 0), world), Color(255, 70, 70, 255), length * 0.12f);
    Arrow(o, o + Vector3::TransformNormal(Vector3(0, length, 0), world), Color(90, 255, 90, 255), length * 0.12f);
    Arrow(o, o + Vector3::TransformNormal(Vector3(0, 0, length), world), Color(90, 140, 255, 255), length * 0.12f);
}

void Gizmos::Grid(const Vector3& centre, float half, float step, const Color& c)
{
    for (float t = -half; t <= half + 1e-4f; t += step) {
        Line(centre + Vector3(t, 0, -half), centre + Vector3(t, 0, half), c);
        Line(centre + Vector3(-half, 0, t), centre + Vector3(half, 0, t), c);
    }
}

void Gizmos::PlaneQuad(const Plane& plane, const Vector3& nearPoint, float half, const Color& c)
{
    const Vector3 n = plane.Normal;
    const float dist = Vector3::Dot(nearPoint, n) + plane.D;
    const Vector3 centre = nearPoint - n * dist;
    const Vector3 helper = std::abs(n.Y) > 0.95f ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
    const Vector3 u = Vector3::Normalize(Vector3::Cross(helper, n)) * half;
    const Vector3 v = Vector3::Normalize(Vector3::Cross(n, u)) * half;
    const Vector3 q[4] = {centre - u - v, centre + u - v, centre + u + v, centre - u + v};
    for (int i = 0; i < 4; ++i) Line(q[i], q[(i + 1) % 4], c);
    Line(centre, centre + n * (half * 0.5f), c);
}

void Gizmos::Matrix4(const Matrix& m, const BoundingBox& local, const Color& c)
{
    const Vector3 lo = local.Min, hi = local.Max;
    Vector3 v[8] = {{lo.X, lo.Y, lo.Z}, {hi.X, lo.Y, lo.Z}, {hi.X, lo.Y, hi.Z}, {lo.X, lo.Y, hi.Z},
                    {lo.X, hi.Y, lo.Z}, {hi.X, hi.Y, lo.Z}, {hi.X, hi.Y, hi.Z}, {lo.X, hi.Y, hi.Z}};
    for (Vector3& p : v) p = Vector3::Transform(p, m);
    for (int i = 0; i < 4; ++i) {
        Line(v[i], v[(i + 1) % 4], c);
        Line(v[4 + i], v[4 + (i + 1) % 4], c);
        Line(v[i], v[4 + i], c);
    }
}

void Gizmos::Flush(const DrawContext& ctx, bool onTop)
{
    if (lines_.empty()) return;
    GraphicsDevice& device = ctx.device;
    RenderStates::SetOpaque3D(device);
    if (onTop) device.setDepthStencilStateProperty(DepthStencilState::None);
    BasicEffect& flat = ctx.fx.Flat();
    flat.setWorldProperty(Matrix::getIdentityProperty());
    flat.setViewProperty(ctx.view.view);
    flat.setProjectionProperty(ctx.view.projection);
    EffectBank::Apply(flat);
    // One call per batch; a very large queue is split to stay inside vertex limits.
    constexpr std::size_t kMax = 60000;
    for (std::size_t i = 0; i < lines_.size(); i += kMax) {
        const std::size_t n = std::min(kMax, lines_.size() - i) & ~static_cast<std::size_t>(1);
        device.DrawUserPrimitives(PrimitiveType::LineList, lines_.data() + i, 0, static_cast<int>(n / 2));
    }
    lines_.clear();
}

namespace {
BasicEffect& SolidEffect(const DrawContext& ctx, const Vector3& colour, float alpha)
{
    BasicEffect& fx = ctx.fx.BeginLit();
    fx.EnableDefaultLighting();
    fx.setPunctualLightEXT(PunctualLightEXT{});
    fx.setViewProperty(ctx.view.view);
    fx.setProjectionProperty(ctx.view.projection);
    fx.setDiffuseColorProperty(colour);
    fx.setEmissiveColorProperty(colour * 0.15f);
    fx.setSpecularColorProperty(Vector3(0.25f, 0.25f, 0.25f));
    fx.setSpecularPowerProperty(16.0f);
    fx.setAlphaProperty(alpha);
    return fx;
}
}  // namespace

void Gizmos::SolidArrow(const DrawContext& ctx, const Vector3& from, const Vector3& to, const Vector3& colour, float radius)
{
    const Vector3 d = to - from;
    const float len = d.Length();
    if (len < 1e-4f) return;
    const Vector3 dir = d / len;
    BasicEffect& fx = SolidEffect(ctx, colour, 1.0f);
    const float headLen = std::min(len * 0.5f, std::max(radius * 5.0f, 0.12f));
    const Matrix rot = AlignYTo(dir);
    fx.setWorldProperty(Matrix::CreateScale(radius * 2.0f, std::max(0.001f, len - headLen), radius * 2.0f) * rot * Matrix::CreateTranslation(from));
    EffectBank::Apply(fx);
    s_.shapes->Cylinder().Draw(ctx.device);
    fx.setWorldProperty(Matrix::CreateScale(radius * 5.0f, headLen, radius * 5.0f) * rot * Matrix::CreateTranslation(from + dir * (len - headLen)));
    EffectBank::Apply(fx);
    s_.shapes->Cone().Draw(ctx.device);
}

void Gizmos::SolidSphere(const DrawContext& ctx, const Vector3& centre, float radius, const Vector3& colour, float alpha)
{
    BasicEffect& fx = SolidEffect(ctx, colour, alpha);
    fx.setWorldProperty(Matrix::CreateScale(radius * 2.0f) * Matrix::CreateTranslation(centre));
    EffectBank::Apply(fx);
    s_.shapes->Sphere().Draw(ctx.device);
}

void Gizmos::Label(const Vector3& position, const std::string& text, const Color& color, float scale)
{
    labels_.push_back({position, text, color, scale});
}

void Gizmos::DrawLabels()
{
    if (labels_.empty() || s_.mainView == nullptr || s_.font == nullptr) {
        labels_.clear();
        return;
    }
    GraphicsDevice& device = *s_.device;
    const Viewport vp = device.getViewportProperty();
    const SceneView& view = *s_.mainView;
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
    for (const LabelItem& l : labels_) {
        // Behind the camera or beyond 25 m: skip. The frustum test is cheap and removes most of them.
        const Vector3 toLabel = l.position - view.eye;
        if (toLabel.LengthSquared() > 25.0f * 25.0f) continue;
        const Vector4 clip = Vector4::Transform(Vector4(l.position, 1.0f), view.view * view.projection);
        if (clip.W <= 0.05f) continue;
        const Vector3 p = vp.Project(l.position, view.projection, view.view, Matrix::getIdentityProperty());
        const Vector2 size = s_.font->MeasureString(l.text) * l.scale;
        const Vector2 pos(p.X - size.X * 0.5f, p.Y - size.Y * 0.5f);
        s_.sprites->Draw(*s_.whiteTexture, Rectangle(static_cast<int>(pos.X) - 3, static_cast<int>(pos.Y) - 1, static_cast<int>(size.X) + 6, static_cast<int>(size.Y) + 2),
                         Color(0, 0, 0, 140));
        s_.sprites->DrawString(*s_.font, l.text, pos, l.color, 0.0f, Vector2(0, 0), l.scale, SpriteEffects::None, 0.0f);
    }
    s_.sprites->End();
    labels_.clear();
}
