#pragma once

#include <functional>
#include <memory>

#include "App/Services.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

/**
 * A screen inside the school: SpriteBatch + SpriteFont draw into a RenderTarget2D whose
 * texture is then applied to a quad. Contents persist until the next Render().
 */
class LiveScreen {
public:
    LiveScreen(Services& services, int width, int height);

    /** Redraws the screen. `draw` runs between SpriteBatch.Begin/End on a cleared target. */
    void Render(const Microsoft::Xna::Framework::Color& background,
                const std::function<void(Microsoft::Xna::Framework::Graphics::SpriteBatch&, int, int)>& draw);

    /**
     * Redraws the screen with `draw` in full control: it calls SpriteBatch.Begin/End itself with whatever
     * sort mode, blend, sampler, rasterizer state and transform it wants to demonstrate.
     */
    void RenderCustom(const Microsoft::Xna::Framework::Color& background, const std::function<void(Microsoft::Xna::Framework::Graphics::SpriteBatch&, int, int)>& draw);

    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* Texture() { return target_.get(); }
    [[nodiscard]] bool HasContent() const { return rendered_; }
    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }

private:
    Services& s_;
    int width_;
    int height_;
    bool rendered_ = false;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> target_;
};
