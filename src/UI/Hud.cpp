#include "UI/Hud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Rendering/TextureLibrary.hpp"
#include "UI/UiScale.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
Color Fade(Color c, float a)
{
    return Color(static_cast<int>(c.getRProperty()), static_cast<int>(c.getGProperty()), static_cast<int>(c.getBProperty()),
                 static_cast<int>(std::clamp(a, 0.0f, 1.0f) * static_cast<float>(c.getAProperty())));
}
const Color kGold(255, 214, 90, 255);
const Color kInk(235, 238, 245, 255);
const Color kPanel(10, 14, 24, 190);
}  // namespace

void Hud::Toast(const std::string& message, float seconds)
{
    toasts_.push_back({message, seconds});
    if (toasts_.size() > 5) toasts_.pop_front();
}

void Hud::Update(float dt)
{
    for (ToastItem& t : toasts_) t.remaining -= dt;
    while (!toasts_.empty() && toasts_.front().remaining <= 0.0f) toasts_.pop_front();
}

void Hud::Rect(int x, int y, int w, int h, Color color)
{
    s_.sprites->Draw(*s_.textures->White(), Rectangle(x, y, w, h), color);
}

void Hud::Panel(int x, int y, int w, int h)
{
    Rect(x, y, w, h, kPanel);
    Rect(x, y, w, 2, Color(200, 170, 90, 220));
    Rect(x, y + h - 2, w, 2, Color(200, 170, 90, 220));
}

void Hud::Text(const SpriteFont& font, const std::string& text, Vector2 position, Color color, float scale, bool shadow)
{
    if (shadow) s_.sprites->DrawString(font, text, position + Vector2(1.5f * scale, 1.5f * scale), Color(0, 0, 0, 200), 0.0f, Vector2(0, 0), scale, SpriteEffects::None, 0.0f);
    s_.sprites->DrawString(font, text, position, color, 0.0f, Vector2(0, 0), scale, SpriteEffects::None, 0.0f);
}

void Hud::TextCentred(const SpriteFont& font, const std::string& text, float centreX, float y, Color color, float scale)
{
    const Vector2 size = font.MeasureString(text);
    Text(font, text, Vector2(centreX - size.X * scale * 0.5f, y), color, scale);
}

void Hud::DrawCrosshair(bool focused)
{
    const int cx = w_ / 2, cy = h_ / 2;
    const Color c = focused ? Color(255, 214, 90, 240) : Color(255, 255, 255, 170);
    const int arm = focused ? 7 : 5;
    const int gap = focused ? 3 : 2;
    Rect(cx - arm - gap, cy - 1, arm, 2, c);
    Rect(cx + gap, cy - 1, arm, 2, c);
    Rect(cx - 1, cy - arm - gap, 2, arm, c);
    Rect(cx - 1, cy + gap, 2, arm, c);
}

void Hud::DrawHelp()
{
    const char* lines[] = {
        "CONTROLS",
        "W A S D   walk          Mouse   look          Shift   run          E / click   use",
        "Esc   pause             F1 / H   this help    F3   statistics      F4   debug overlays",
        "Tab   map               R   back to the entrance                  F11   fullscreen     F12   screenshot",
        "Space   pause the clock       1 2 3 4 5   speed x1 x2 x5 x10 x30        N   next lesson     , .   15 minutes back / on",
        "B   ring the bell       F   fire drill on/off         P   announcement       O   weather",
        "[ ]   mouse sensitivity       - =   volume          Page Up/Down, arrows   look",
        "Gamepad: left stick walk, right stick look, A use, Start pause, Y map",
    };
    const int n = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
    const int panelW = 900;
    const int panelH = 28 + n * 30;
    const int x = (w_ - panelW) / 2, y = (h_ - panelH) / 2;
    Panel(x, y, panelW, panelH);
    float ty = static_cast<float>(y) + 14.0f;
    for (int i = 0; i < n; ++i) {
        Text(*s_.font, lines[i], Vector2(static_cast<float>(x) + 24.0f, ty), i == 0 ? kGold : kInk, 0.9f);
        ty += 30.0f;
    }
}

void Hud::DrawPause(const HudState& state)
{
    Rect(0, 0, w_, h_, Color(0, 0, 0, 140));
    const int panelW = 560, panelH = 330;
    const int x = (w_ - panelW) / 2, y = (h_ - panelH) / 2;
    Panel(x, y, panelW, panelH);
    TextCentred(*s_.titleFont, "PAUSED", static_cast<float>(w_) / 2.0f, static_cast<float>(y) + 18.0f, kGold);
    char buf[160];
    float ty = static_cast<float>(y) + 96.0f;
    const auto line = [&](const std::string& text) {
        TextCentred(*s_.font, text, static_cast<float>(w_) / 2.0f, ty, kInk);
        ty += 32.0f;
    };
    line("Esc / click   resume");
    line("R   back to the entrance");
    std::snprintf(buf, sizeof buf, "[ ]   mouse sensitivity: %.2fx", static_cast<double>(state.mouseSensitivityScale));
    line(buf);
    std::snprintf(buf, sizeof buf, "- =   volume: %d%%   (F2 mute: %s)", static_cast<int>(state.volume * 100.0f + 0.5f), state.audioMuted ? "on" : "off");
    line(buf);
    line("F1   controls          F11   fullscreen");
    line("Q   quit");
}

void Hud::DrawStats(const HudState& state)
{
    const int lineH = 22;
    const int panelH = 16 + lineH * static_cast<int>(state.statsLines.size());
    Panel(10, 10, 520, panelH);
    float ty = 18.0f;
    for (const std::string& l : state.statsLines) {
        Text(*s_.font, l, Vector2(22.0f, ty), Color(150, 255, 160, 255), 0.72f);
        ty += static_cast<float>(lineH);
    }
}

void Hud::Draw(const HudState& state)
{
    GraphicsDevice& device = *s_.device;
    const int realW = device.getViewportProperty().getWidthProperty();
    const int realH = device.getViewportProperty().getHeightProperty();
    ui_ = UiScale::Scale(realW, realH);
    w_ = UiScale::Virtual(realW, ui_);
    h_ = UiScale::Virtual(realH, ui_);
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr, nullptr, Matrix::CreateScale(ui_, ui_, 1.0f));
    if (s_.font == nullptr) return;

    if (state.bellFlash > 0.01f) {
        const Color c = Fade(Color(255, 240, 200, 255), state.bellFlash * 0.35f);
        Rect(0, 0, w_, 6, c);
        Rect(0, h_ - 6, w_, 6, c);
        Rect(0, 0, 6, h_, c);
        Rect(w_ - 6, 0, 6, h_, c);
    }
    if (!state.paused && !state.minimal) {
        DrawCrosshair(!state.prompt.empty());
        // Clock panel, top centre.
        {
            const int pw = 300, ph = 64;
            const int x = (w_ - pw) / 2;
            Panel(x, 8, pw, ph);
            TextCentred(*s_.titleFont, state.clock, static_cast<float>(w_) / 2.0f, 10.0f, kGold, 0.78f);
            const std::string sub = state.day + "   " + state.phase + "   " + state.speed;
            TextCentred(*s_.font, sub, static_cast<float>(w_) / 2.0f, 42.0f, kInk, 0.68f);
        }
        if (state.drill) {
            const float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(toasts_.size()) + 0.0f);
            (void)pulse;
            const int pw = 520, ph = 44;
            Rect((w_ - pw) / 2, 80, pw, ph, Color(180, 20, 20, 210));
            TextCentred(*s_.font, state.drillText, static_cast<float>(w_) / 2.0f, 90.0f, Color(255, 240, 240, 255), 0.85f);
        }
        if (!state.prompt.empty()) {
            const std::string text = "[E]  " + state.prompt;
            const Vector2 size = s_.font->MeasureString(text) * 0.85f;
            const int pw = static_cast<int>(size.X) + 36;
            Rect(w_ / 2 - pw / 2, h_ / 2 + 46, pw, 36, Color(10, 14, 24, 200));
            Rect(w_ / 2 - pw / 2, h_ / 2 + 46, pw, 2, Color(255, 214, 90, 230));
            Text(*s_.font, text, Vector2(static_cast<float>(w_) / 2.0f - size.X / 2.0f, static_cast<float>(h_) / 2.0f + 50.0f), Color(255, 244, 214, 255), 0.85f);
        }
        if (state.bannerAlpha > 0.01f && !state.roomTitle.empty()) {
            const float a = state.bannerAlpha;
            const Vector2 ts = s_.titleFont->MeasureString(state.roomTitle) * 0.7f;
            const Vector2 ss = s_.font->MeasureString(state.roomSubtitle) * 0.75f;
            const int bw = static_cast<int>(std::max(ts.X, ss.X)) + 60;
            const int bx = 20, by = h_ - 110;
            Rect(bx, by, bw, 84, Fade(kPanel, a));
            Rect(bx, by, 4, 84, Fade(kGold, a));
            Text(*s_.titleFont, state.roomTitle, Vector2(static_cast<float>(bx) + 20.0f, static_cast<float>(by) + 8.0f), Fade(kGold, a), 0.7f);
            Text(*s_.font, state.roomSubtitle, Vector2(static_cast<float>(bx) + 20.0f, static_cast<float>(by) + 50.0f), Fade(kInk, a), 0.75f);
        }
        float ty = static_cast<float>(h_) - 150.0f;
        for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
            const float a = std::min(1.0f, it->remaining * 2.0f);
            TextCentred(*s_.font, it->text, static_cast<float>(w_) / 2.0f, ty, Fade(Color(255, 255, 255, 255), a), 0.85f);
            ty -= 30.0f;
        }
    }
    if (state.showStats) DrawStats(state);
    if (state.showHelp && !state.paused) DrawHelp();
    if (state.paused) DrawPause(state);
    if (state.fade > 0.001f) {
        Rect(0, 0, w_, h_, Fade(Color(0, 0, 0, 255), state.fade));
        if (!state.fadeText.empty()) TextCentred(*s_.titleFont, state.fadeText, static_cast<float>(w_) / 2.0f, static_cast<float>(h_) / 2.0f - 30.0f, Fade(kGold, state.fade), 0.8f);
    }
}

void Hud::End()
{
    s_.sprites->End();
}
