#pragma once

#include <functional>

#include "Interaction/FirstPersonCamera.hpp"
#include "Interaction/InputState.hpp"
#include "Scene/Collision.hpp"

/**
 * Turns input into camera motion: WASD / left stick to walk, mouse / right stick to look, Shift to run.
 * The player is a vertical cylinder that slides along colliders; the feet follow the plan's ground height,
 * which is how stairs are climbed (there is no jumping and no free fall).
 */
class PlayerController {
public:
    struct Settings {
        float walkSpeed = 3.4f;           // metres per second
        float sprintMultiplier = 1.9f;
        float eyeHeight = School::kEyeHeight;
        float radius = 0.3f;
        float mouseSensitivity = 0.0025f; // radians per pixel
        float stickLookSpeed = 2.4f;      // radians per second at full deflection
        bool invertY = false;
    };

    PlayerController(const Settings& settings, const School::SchoolPlan& plan) : settings_(settings), plan_(plan) {}

    Settings& GetSettings() { return settings_; }
    [[nodiscard]] const Settings& GetSettings() const { return settings_; }

    /** Puts the feet at `feet` (the eye goes eyeHeight above), looking along `yaw`. */
    void Place(const Microsoft::Xna::Framework::Vector3& feet, float yaw, float pitch, FirstPersonCamera& camera);
    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& Feet() const { return feet_; }
    [[nodiscard]] int Level() const { return level_; }

    /** Carried by an elevator cab: the feet follow `y` instead of the plan's ground. */
    void SetRide(bool riding, float y)
    {
        riding_ = riding;
        rideY_ = y;
    }
    [[nodiscard]] bool Riding() const { return riding_; }
    /** Debug: no collision and no ground; W flies where the camera looks. */
    void SetFlying(bool flying) { flying_ = flying; }
    [[nodiscard]] bool Flying() const { return flying_; }

    /** Walk and look for one frame. `mouseLook` is false while the pointer is released (menus). */
    void Update(float dt, FirstPersonCamera& camera, const CollisionWorld& collision, const InputState& input, bool mouseLook);

    /** Captures or releases the pointer. Falls back to re-centring the cursor when relative mode is unavailable. */
    void SetMouseCaptured(bool captured, int windowWidth, int windowHeight);
    [[nodiscard]] bool MouseCaptured() const { return captured_; }

    /** Test hook: walk with the given forward/strafe input (each -1..1) for the next `frames` updates. */
    void ScriptWalk(float forward, float strafe, int frames, float yawTurnPerFrame = 0.0f)
    {
        scriptForward_ = forward;
        scriptStrafe_ = strafe;
        scriptFrames_ = frames;
        scriptTurn_ = yawTurnPerFrame;
    }

    /** Called each time a footstep lands; the argument says whether the player is running. */
    std::function<void(bool running)> onFootstep;
    [[nodiscard]] float DistanceWalked() const { return walked_; }
    [[nodiscard]] bool IsMoving() const { return moving_; }

private:
    Settings settings_;
    const School::SchoolPlan& plan_;
    Microsoft::Xna::Framework::Vector3 feet_{0, 0, 0};
    float groundY_ = 0.0f;
    int level_ = 1;
    bool riding_ = false;
    float rideY_ = 0.0f;
    bool flying_ = false;
    bool captured_ = false;
    bool relativeMode_ = false;
    // The first read after switching relative mode on still reports where the pointer was, not a motion.
    bool skipRelativeRead_ = false;
    bool pointerUnavailable_ = false;   // the platform cannot re-centre the pointer (no window): mouse look off
    int verifyFrames_ = 0, verifiedFrames_ = 0;
    int centreX_ = 0;
    int centreY_ = 0;
    float stride_ = 0.0f;
    float walked_ = 0.0f;
    float bobPhase_ = 0.0f;
    bool moving_ = false;
    float scriptForward_ = 0.0f;
    float scriptStrafe_ = 0.0f;
    float scriptTurn_ = 0.0f;
    int scriptFrames_ = 0;
};
