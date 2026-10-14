#include "Rendering/LiveScreen.hpp"

#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

LiveScreen::LiveScreen(Services& services, int width, int height) : s_(services), width_(width), height_(height)
{
    target_ = std::make_unique<RenderTarget2D>(*s_.device, width, height, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                               RenderTargetUsage::PreserveContents);
}

void LiveScreen::Render(const Color& background, const std::function<void(SpriteBatch&, int, int)>& draw)
{
    GraphicsDevice& device = *s_.device;
    device.SetRenderTarget(target_.get());
    device.Clear(background);
    const SamplerState sampler = SamplerState::LinearClamp;
    s_.sprites->Begin(SpriteSortMode::Deferred, &BlendState::NonPremultiplied, &sampler, nullptr, nullptr);
    draw(*s_.sprites, width_, height_);
    s_.sprites->End();
    device.SetRenderTarget(nullptr);
    rendered_ = true;
}

void LiveScreen::RenderCustom(const Color& background, const std::function<void(SpriteBatch&, int, int)>& draw)
{
    GraphicsDevice& device = *s_.device;
    device.SetRenderTarget(target_.get());
    device.Clear(background);
    draw(*s_.sprites, width_, height_);
    device.SetRenderTarget(nullptr);
    rendered_ = true;
}
