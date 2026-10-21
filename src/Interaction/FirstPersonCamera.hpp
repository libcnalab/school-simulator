#pragma once

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include "CnaEngine/Graphics/Camera.hpp"

// Eye position plus yaw/pitch. Yaw 0 looks down -Z (XNA's forward); positive
// yaw turns left. The camera owns no input handling: PlayerController drives it.
// Its matrices come from cna-engine's Camera, which it keeps in step with the
// pose and lens (XNA's CreateLookAt and CreatePerspectiveFieldOfView of them).
class FirstPersonCamera {
public:
    FirstPersonCamera();

    void SetPose(const Microsoft::Xna::Framework::Vector3& eye, float yaw, float pitch);
    void SetLens(float verticalFov, float aspectRatio, float nearPlane, float farPlane);

    void SetPosition(const Microsoft::Xna::Framework::Vector3& eye);
    void Rotate(float deltaYaw, float deltaPitch);

    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& Position() const { return position_; }
    [[nodiscard]] float Yaw() const { return yaw_; }
    [[nodiscard]] float Pitch() const { return pitch_; }
    [[nodiscard]] float AspectRatio() const { return aspect_; }
    [[nodiscard]] float VerticalFov() const { return fov_; }

    [[nodiscard]] Microsoft::Xna::Framework::Vector3 Forward() const;
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 Right() const;
    /** Forward flattened onto the floor plane: the direction W walks in. */
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 GroundForward() const;
    [[nodiscard]] Microsoft::Xna::Framework::Ray ViewRay() const;

    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& View() const { return camera_.View(); }
    [[nodiscard]] const Microsoft::Xna::Framework::Matrix& Projection() const { return camera_.Projection(); }

private:
    void SyncPose();

    Microsoft::Xna::Framework::Vector3 position_{0.0f, 1.65f, 0.0f};
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float fov_ = 1.2217305f;  // 70 degrees
    float aspect_ = 16.0f / 9.0f;
    float near_ = 0.08f;
    float far_ = 160.0f;
    CnaEngine::Graphics::Camera camera_;
};
