#pragma once

#include <memory>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

namespace FontLoader {

/**
 * Builds a SpriteFont from Content/Fonts/<name>.png (glyph atlas) and <name>.txt
 * (metrics) through SpriteFont's public constructor. Returns null, after logging,
 * when either file is missing or malformed; callers then fall back to no text.
 */
std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont>
Load(Microsoft::Xna::Framework::Content::ContentManager& content, const std::string& contentRoot,
     const std::string& name);

}  // namespace FontLoader
