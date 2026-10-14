#pragma once

#include <deque>
#include <string>
#include <vector>

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

struct HudState {
    std::string clock;            // "08:45"
    std::string day;              // "Monday"
    std::string phase;            // "Lesson 2"
    std::string speed;            // "x5", "paused"
    std::string roomTitle;        // current room
    std::string roomSubtitle;     // what happens there now
    std::string prompt;           // interaction text for the crosshair target, empty when none
    float bannerAlpha = 0.0f;     // room banner fade
    bool paused = false;
    bool minimal = false;         // only the crosshair-less background (the big map is up)
    bool showHelp = false;
    bool showStats = false;
    bool drill = false;
    std::string drillText;
    float bellFlash = 0.0f;       // 0..1, brief white edge flash when the bell rings
    float fade = 0.0f;
    std::string fadeText;
    std::vector<std::string> statsLines;
    std::string rendererName;
    float mouseSensitivityScale = 1.0f;
    float volume = 1.0f;
    bool audioMuted = false;
    int population = 0;
    int onCampus = 0;
};

/** 2D overlay drawn with SpriteBatch + SpriteFont after the 3D scene. */
class Hud {
public:
    explicit Hud(Services& services) : s_(services) {}

    void Toast(const std::string& message, float seconds = 2.5f);
    void Update(float dt);
    /** Begins the sprite batch, draws the overlay and leaves the batch open for the minimap; call End() after. */
    void Draw(const HudState& state);
    void End();

    void Text(const Microsoft::Xna::Framework::Graphics::SpriteFont& font, const std::string& text, Microsoft::Xna::Framework::Vector2 position,
              Microsoft::Xna::Framework::Color color, float scale = 1.0f, bool shadow = true);
    void TextCentred(const Microsoft::Xna::Framework::Graphics::SpriteFont& font, const std::string& text, float centreX, float y, Microsoft::Xna::Framework::Color color,
                     float scale = 1.0f);
    void Rect(int x, int y, int w, int h, Microsoft::Xna::Framework::Color color);
    void Panel(int x, int y, int w, int h);
    /** Layout size the overlay works in (1280x720 scaled to the window). */
    [[nodiscard]] int Width() const { return w_; }
    [[nodiscard]] int Height() const { return h_; }
    [[nodiscard]] float Scale() const { return ui_; }

private:
    struct ToastItem {
        std::string text;
        float remaining;
    };
    void DrawCrosshair(bool focused);
    void DrawPause(const HudState& state);
    void DrawHelp();
    void DrawStats(const HudState& state);

    Services& s_;
    std::deque<ToastItem> toasts_;
    int w_ = 1280, h_ = 720;
    float ui_ = 1.0f;
};
