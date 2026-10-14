#pragma once

#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

/** This frame's and last frame's input, so key presses can be detected as edges. */
class InputState {
public:
    void Update()
    {
        previousKeyboard_ = keyboard_;
        previousMouse_ = mouse_;
        previousPad_ = pad_;
        keyboard_ = Microsoft::Xna::Framework::Input::Keyboard::GetState();
        mouse_ = Microsoft::Xna::Framework::Input::Mouse::GetState();
        pad_ = Microsoft::Xna::Framework::Input::GamePad::GetState(Microsoft::Xna::Framework::PlayerIndex::One);
    }

    [[nodiscard]] bool Down(Microsoft::Xna::Framework::Input::Keys key) const { return keyboard_.IsKeyDown(key); }
    [[nodiscard]] bool Pressed(Microsoft::Xna::Framework::Input::Keys key) const
    {
        return keyboard_.IsKeyDown(key) && previousKeyboard_.IsKeyUp(key);
    }
    [[nodiscard]] bool LeftClicked() const
    {
        using Microsoft::Xna::Framework::Input::ButtonState;
        return mouse_.getLeftButtonProperty() == ButtonState::Pressed &&
               previousMouse_.getLeftButtonProperty() == ButtonState::Released;
    }
    [[nodiscard]] bool LeftDown() const
    {
        return mouse_.getLeftButtonProperty() == Microsoft::Xna::Framework::Input::ButtonState::Pressed;
    }
    [[nodiscard]] bool RightClicked() const
    {
        using Microsoft::Xna::Framework::Input::ButtonState;
        return mouse_.getRightButtonProperty() == ButtonState::Pressed &&
               previousMouse_.getRightButtonProperty() == ButtonState::Released;
    }
    /** Wheel notches since the last frame (positive = scrolled away from the user). */
    [[nodiscard]] float WheelDelta() const
    {
        return static_cast<float>(mouse_.getScrollWheelValueProperty() - previousMouse_.getScrollWheelValueProperty()) / 120.0f;
    }
    [[nodiscard]] bool PadPressed(Microsoft::Xna::Framework::Input::ButtonState (Microsoft::Xna::Framework::Input::GamePadButtons::*button)() const) const
    {
        using Microsoft::Xna::Framework::Input::ButtonState;
        return pad_.getIsConnectedProperty() && (pad_.getButtonsProperty().*button)() == ButtonState::Pressed &&
               (previousPad_.getButtonsProperty().*button)() == ButtonState::Released;
    }

    [[nodiscard]] const Microsoft::Xna::Framework::Input::MouseState& Mouse() const { return mouse_; }
    [[nodiscard]] const Microsoft::Xna::Framework::Input::MouseState& PreviousMouse() const { return previousMouse_; }
    [[nodiscard]] const Microsoft::Xna::Framework::Input::GamePadState& Pad() const { return pad_; }

private:
    Microsoft::Xna::Framework::Input::KeyboardState keyboard_;
    Microsoft::Xna::Framework::Input::KeyboardState previousKeyboard_;
    Microsoft::Xna::Framework::Input::MouseState mouse_;
    Microsoft::Xna::Framework::Input::MouseState previousMouse_;
    Microsoft::Xna::Framework::Input::GamePadState pad_;
    Microsoft::Xna::Framework::Input::GamePadState previousPad_;
};
