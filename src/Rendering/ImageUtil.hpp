#pragma once

#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace ImageUtil {

/** Box-filtered mip chain; level 0 is `pixels` itself. Sizes halve down to 1x1. */
std::vector<std::vector<Microsoft::Xna::Framework::Color>>
BuildMipChain(const std::vector<Microsoft::Xna::Framework::Color>& pixels, int width, int height);

/** Uploads `pixels` as a texture with a complete CPU-generated mip chain. */
std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>
CreateMipmapped(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                const std::vector<Microsoft::Xna::Framework::Color>& pixels, int width, int height);

/** Same, but with caller-supplied mip levels (used to colour each level for the mip exhibit). */
std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>
CreateFromMipChain(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                   const std::vector<std::vector<Microsoft::Xna::Framework::Color>>& chain, int width, int height);

/** Writes an uncompressed RGBA PNG. Returns false when the file cannot be written. */
bool WritePng(const char* path, const Microsoft::Xna::Framework::Color* pixels, int width, int height);

}  // namespace ImageUtil
