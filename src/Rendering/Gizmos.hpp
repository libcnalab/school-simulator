#pragma once

#include <string>
#include <vector>

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

/**
 * Visual aids for the maths and geometry exhibits: thin lines (batched into one DrawUserPrimitives
 * call), solid lit arrows and spheres, and text labels anchored to world positions.
 * Usage per frame: queue lines and labels during DrawOpaque, call Flush(); queue/draw labels
 * with DrawLabels() from DrawOverlay (after the 3D scene, in screen space).
 */
class Gizmos {
public:
    explicit Gizmos(Services& services) : s_(services) {}

    // --- thin line primitives, queued -------------------------------------------------------
    void Line(const Microsoft::Xna::Framework::Vector3& a, const Microsoft::Xna::Framework::Vector3& b,
              const Microsoft::Xna::Framework::Color& color);
    void Arrow(const Microsoft::Xna::Framework::Vector3& from, const Microsoft::Xna::Framework::Vector3& to,
               const Microsoft::Xna::Framework::Color& color, float headSize = 0.15f);
    void Box(const Microsoft::Xna::Framework::BoundingBox& box, const Microsoft::Xna::Framework::Color& color);
    void Sphere(const Microsoft::Xna::Framework::BoundingSphere& sphere, const Microsoft::Xna::Framework::Color& color, int segments = 32);
    void Circle(const Microsoft::Xna::Framework::Vector3& centre, const Microsoft::Xna::Framework::Vector3& normal, float radius,
                const Microsoft::Xna::Framework::Color& color, int segments = 48);
    void Frustum(const Microsoft::Xna::Framework::BoundingFrustum& frustum, const Microsoft::Xna::Framework::Color& color);
    void Axes(const Microsoft::Xna::Framework::Matrix& world, float length = 1.0f);
    void Grid(const Microsoft::Xna::Framework::Vector3& centre, float halfSize, float step, const Microsoft::Xna::Framework::Color& color);
    void PlaneQuad(const Microsoft::Xna::Framework::Plane& plane, const Microsoft::Xna::Framework::Vector3& near, float halfSize,
                   const Microsoft::Xna::Framework::Color& color);
    void Matrix4(const Microsoft::Xna::Framework::Matrix& transform, const Microsoft::Xna::Framework::BoundingBox& local,
                 const Microsoft::Xna::Framework::Color& color);

    /** Draws the queued lines (depth tested unless `onTop`) and clears the queue. */
    void Flush(const DrawContext& ctx, bool onTop = false);

    // --- solid shapes, drawn immediately ----------------------------------------------------
    /** A shaft and cone head from `from` to `to`, lit with the default three-light rig. */
    void SolidArrow(const DrawContext& ctx, const Microsoft::Xna::Framework::Vector3& from, const Microsoft::Xna::Framework::Vector3& to,
                    const Microsoft::Xna::Framework::Vector3& colour, float radius = 0.04f);
    void SolidSphere(const DrawContext& ctx, const Microsoft::Xna::Framework::Vector3& centre, float radius,
                     const Microsoft::Xna::Framework::Vector3& colour, float alpha = 1.0f);

    // --- text anchored in the world -----------------------------------------------------------
    void Label(const Microsoft::Xna::Framework::Vector3& position, const std::string& text,
               const Microsoft::Xna::Framework::Color& color = Microsoft::Xna::Framework::Color::White, float scale = 0.55f);
    /** Projects and draws the queued labels with SpriteBatch; call from Exhibit::DrawOverlay. Clears the queue. */
    void DrawLabels();

    [[nodiscard]] int QueuedLines() const { return static_cast<int>(lines_.size()) / 2; }

private:
    struct LabelItem {
        Microsoft::Xna::Framework::Vector3 position;
        std::string text;
        Microsoft::Xna::Framework::Color color;
        float scale;
    };
    Services& s_;
    std::vector<Microsoft::Xna::Framework::Graphics::VertexPositionColor> lines_;
    std::vector<LabelItem> labels_;
};

/** Rotation that takes +Y onto `direction` (unit), as a row-vector basis matrix; used for shapes that stand on +Y. */
Microsoft::Xna::Framework::Matrix AlignYTo(const Microsoft::Xna::Framework::Vector3& direction);
