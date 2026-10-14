#include "Interaction/PlayerController.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>

#include "App/Log.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace {
constexpr float kStrideLength = 1.6f;
constexpr float kMaxStepSeconds = 0.04f;   // a long frame is walked in several small steps
constexpr int kVerifyFrames = 6;           // frames spent checking that re-centring the pointer works
}  // namespace

void PlayerController::Place(const Vector3& feet, float yaw, float pitch, FirstPersonCamera& camera)
{
    feet_ = feet;
    groundY_ = feet.Y;
    level_ = plan_.LevelAt(feet.Y);
    camera.SetPose(Vector3(feet.X, feet.Y + settings_.eyeHeight, feet.Z), yaw, pitch);
}

void PlayerController::SetMouseCaptured(bool captured, int windowWidth, int windowHeight)
{
    if (captured == captured_) return;
    captured_ = captured;
    centreX_ = windowWidth / 2;
    centreY_ = windowHeight / 2;
    if (captured) {
        try {
            Mouse::setIsRelativeMouseModeEXTProperty(true);
            relativeMode_ = Mouse::getIsRelativeMouseModeEXTProperty();
        } catch (const std::exception& e) {
            relativeMode_ = false;
            School::Log("input", "relative mouse mode unavailable (%s); re-centring the cursor instead", e.what());
        }
        verifyFrames_ = 0;
        verifiedFrames_ = 0;
        pointerUnavailable_ = false;
        if (!relativeMode_) Mouse::SetPosition(centreX_, centreY_);
    } else {
        try {
            Mouse::setIsRelativeMouseModeEXTProperty(false);
        } catch (const std::exception&) {
        }
        relativeMode_ = false;
    }
}

void PlayerController::Update(float dt, FirstPersonCamera& camera, const CollisionWorld& collision, const InputState& input, bool mouseLook)
{
    // Look ---------------------------------------------------------------
    float dyaw = 0.0f;
    float dpitch = 0.0f;
    if (mouseLook && captured_) {
        const MouseState& now = input.Mouse();
        float dx;
        float dy;
        if (relativeMode_) {
            // In relative mode CNA reports the motion since the last read in X/Y (consume-on-read), not a position.
            dx = static_cast<float>(now.getXProperty());
            dy = static_cast<float>(now.getYProperty());
        } else {
            // The pointer is re-centred every frame, so it must read back at the centre. A platform with no window
            // (CNA's SOFTWARE renderer presents a CPU buffer) keeps reporting one fixed position instead: that is
            // "no pointer", not a huge mouse movement. The first frames after capturing only verify that.
            const int x = now.getXProperty(), y = now.getYProperty();
            if (verifyFrames_ < kVerifyFrames) {
                ++verifyFrames_;
                const bool centred = std::abs(x - centreX_) <= 2 && std::abs(y - centreY_) <= 2;
                verifiedFrames_ += centred ? 1 : 0;
                Mouse::SetPosition(centreX_, centreY_);
                if (verifyFrames_ == kVerifyFrames && verifiedFrames_ == 0) {
                    pointerUnavailable_ = true;
                    School::Log("input", "the pointer cannot be re-centred on this platform: mouse look is off, use the arrow keys and Page Up/Down");
                }
                dx = dy = 0.0f;
            } else if (pointerUnavailable_) {
                dx = dy = 0.0f;
            } else {
                dx = static_cast<float>(x - centreX_);
                dy = static_cast<float>(y - centreY_);
                Mouse::SetPosition(centreX_, centreY_);
            }
        }
        dyaw -= dx * settings_.mouseSensitivity;
        dpitch -= dy * settings_.mouseSensitivity * (settings_.invertY ? -1.0f : 1.0f);
    }
    // Keyboard look: always available, and the only look control where there is no usable pointer.
    constexpr float kTurnRate = 1.9f, kPitchRate = 1.2f;
    if (input.Down(Keys::Left)) dyaw += kTurnRate * dt;
    if (input.Down(Keys::Right)) dyaw -= kTurnRate * dt;
    if (input.Down(Keys::PageUp)) dpitch += kPitchRate * dt;
    if (input.Down(Keys::PageDown)) dpitch -= kPitchRate * dt;
    const GamePadState& pad = input.Pad();
    Vector2 stickMove(0, 0);
    // With CNA's keyboard-emulated gamepad on, the keys already walk the player: do not count them twice.
    if (pad.getIsConnectedProperty() && !GamePad::getKeyboardEmulationEnabledEXT()) {
        const Vector2 look = pad.getThumbSticksProperty().getRightProperty();
        dyaw -= look.X * settings_.stickLookSpeed * dt;
        dpitch += look.Y * settings_.stickLookSpeed * dt * (settings_.invertY ? -1.0f : 1.0f);
        stickMove = pad.getThumbSticksProperty().getLeftProperty();
    }
    camera.Rotate(dyaw, dpitch);

    // Walk ---------------------------------------------------------------
    float forward = 0.0f;
    float strafe = 0.0f;
    if (input.Down(Keys::W) || input.Down(Keys::Up)) forward += 1.0f;
    if (input.Down(Keys::S) || input.Down(Keys::Down)) forward -= 1.0f;
    if (input.Down(Keys::D)) strafe += 1.0f;
    if (input.Down(Keys::A)) strafe -= 1.0f;
    forward += stickMove.Y;
    strafe += stickMove.X;
    if (scriptFrames_ > 0) {
        --scriptFrames_;
        forward += scriptForward_;
        strafe += scriptStrafe_;
        camera.Rotate(scriptTurn_, 0.0f);
    }
    const float len = std::sqrt(forward * forward + strafe * strafe);
    if (len > 1.0f) {
        forward /= len;
        strafe /= len;
    }
    const bool sprint = input.Down(Keys::LeftShift) || input.Down(Keys::RightShift) ||
                        (pad.getIsConnectedProperty() && pad.getButtonsProperty().getLeftStickProperty() == ButtonState::Pressed);
    const float speed = settings_.walkSpeed * (sprint ? settings_.sprintMultiplier : 1.0f);

    if (flying_) {
        // Free flight for inspection: along the view direction, three times walking speed, nothing in the way.
        const Vector3 fly = camera.Forward() * forward + camera.Right() * strafe;
        const Vector3 eye = camera.Position() + fly * (speed * 3.0f * dt);
        feet_ = Vector3(eye.X, eye.Y - settings_.eyeHeight, eye.Z);
        groundY_ = feet_.Y;
        level_ = plan_.LevelAt(feet_.Y);
        moving_ = fly.LengthSquared() > 1e-4f;
        camera.SetPosition(eye);
        return;
    }
    const Vector3 wish = camera.GroundForward() * forward + camera.Right() * strafe;
    const Vector3 velocity = wish * speed;
    moving_ = wish.LengthSquared() > 1e-4f;

    Vector3 position = feet_;
    float remaining = std::min(dt, 0.25f);
    float travelled = 0.0f;
    while (remaining > 1e-5f) {
        const float step = std::min(remaining, kMaxStepSeconds);
        Vector3 next = collision.Move(position, velocity * step, settings_.radius, level_);
        // The ground under the new place: a step up to 0.6 m is taken (stairs), anything higher is a wall.
        const float ground = riding_ ? rideY_ : plan_.GroundHeight(School::Vec2{next.X, next.Z}, position.Y);
        if (ground > position.Y + 0.6f) {
            next = position;   // blocked by a ledge
        } else {
            next.Y = ground;
        }
        travelled += Vector2::Distance(Vector2(position.X, position.Z), Vector2(next.X, next.Z));
        position = next;
        remaining -= step;
    }
    feet_ = position;
    level_ = plan_.LevelAt(feet_.Y);
    // Smooth the vertical motion so stair steps do not jolt the view (a cab ride is smooth already).
    if (riding_) groundY_ = feet_.Y;
    else groundY_ += (feet_.Y - groundY_) * std::min(1.0f, dt * 14.0f);

    walked_ += travelled;
    stride_ += travelled;
    if (stride_ >= kStrideLength * (sprint ? 1.25f : 1.0f)) {
        stride_ = 0.0f;
        if (onFootstep) onFootstep(sprint);
    }
    if (moving_) bobPhase_ += travelled * 4.0f;
    const float bob = moving_ ? std::sin(bobPhase_) * 0.025f * (sprint ? 1.4f : 1.0f) : 0.0f;
    camera.SetPosition(Vector3(feet_.X, groundY_ + settings_.eyeHeight + bob, feet_.Z));
}
