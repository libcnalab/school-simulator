#include "Interaction/FirstPersonCamera.hpp"

#include <algorithm>
#include <cmath>

#include "Microsoft/Xna/Framework/MathHelper.hpp"

using namespace Microsoft::Xna::Framework;

namespace {
constexpr float kMaxPitch = 1.5f;  // just short of straight up/down so LookAt stays well defined
}

FirstPersonCamera::FirstPersonCamera()
{
    camera_.SetPerspective(fov_, aspect_, near_, far_);
    SyncPose();
}

void FirstPersonCamera::SetPose(const Vector3& eye, float yaw, float pitch)
{
    position_ = eye;
    yaw_ = MathHelper::WrapAngle(yaw);
    pitch_ = std::clamp(pitch, -kMaxPitch, kMaxPitch);
    SyncPose();
}

void FirstPersonCamera::SetPosition(const Vector3& eye)
{
    position_ = eye;
    SyncPose();
}

void FirstPersonCamera::SyncPose()
{
    camera_.SetLookAt(position_, position_ + Forward(), Vector3::Up);
}

void FirstPersonCamera::SetLens(float verticalFov, float aspectRatio, float nearPlane, float farPlane)
{
    fov_ = verticalFov;
    aspect_ = aspectRatio;
    near_ = nearPlane;
    far_ = farPlane;
    camera_.SetPerspective(fov_, aspect_, near_, far_);
}

void FirstPersonCamera::Rotate(float deltaYaw, float deltaPitch)
{
    SetPose(position_, yaw_ + deltaYaw, pitch_ + deltaPitch);
}

Vector3 FirstPersonCamera::Forward() const
{
    const float cp = std::cos(pitch_);
    return Vector3(-std::sin(yaw_) * cp, std::sin(pitch_), -std::cos(yaw_) * cp);
}

Vector3 FirstPersonCamera::Right() const
{
    return Vector3(std::cos(yaw_), 0.0f, -std::sin(yaw_));
}

Vector3 FirstPersonCamera::GroundForward() const
{
    return Vector3(-std::sin(yaw_), 0.0f, -std::cos(yaw_));
}

Ray FirstPersonCamera::ViewRay() const
{
    return Ray(position_, Forward());
}
