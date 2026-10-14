#pragma once

#include <algorithm>

/**
 * The overlays (HUD, directory) are laid out for a 1280 x 720 window. In another size they are drawn through a scale
 * matrix so they keep their proportions: `Scale` is that factor and `Virtual` the size the layout code should work in.
 */
namespace UiScale {

[[nodiscard]] inline float Scale(int width, int height)
{
    return std::clamp(std::min(static_cast<float>(width) / 1280.0f, static_cast<float>(height) / 720.0f), 0.6f, 2.5f);
}
[[nodiscard]] inline int Virtual(int pixels, float scale) { return static_cast<int>(static_cast<float>(pixels) / scale); }

}  // namespace UiScale
