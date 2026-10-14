#include "UI/FontLoader.hpp"

#include <sstream>
#include <vector>

#include "App/Log.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Rendering/AssetPaths.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

std::unique_ptr<SpriteFont> FontLoader::Load(Content::ContentManager& content, const std::string& contentRoot,
                                              const std::string& name)
{
    std::string metrics;
    if (!AssetPaths::ReadTextFile(contentRoot + "/Fonts/" + name + ".txt", metrics)) {
        School::Log("font", "cannot read metrics for font '%s'", name.c_str());
        return nullptr;
    }

    std::vector<Rectangle> glyphBounds;
    std::vector<Rectangle> cropping;
    std::vector<Vector3> kerning;
    std::vector<SharpRuntime::charcs> characters;
    int lineSpacing = 0;
    float spacing = 0.0f;
    int defaultChar = '?';

    std::istringstream lines(metrics);
    std::string kind;
    while (lines >> kind) {
        if (kind == "font") {
            lines >> lineSpacing >> spacing >> defaultChar;
        } else if (kind == "glyph") {
            int code, x, y, w, h, cropX, cropY;
            float kl, kw, kr;
            lines >> code >> x >> y >> w >> h >> cropX >> cropY >> kl >> kw >> kr;
            characters.push_back(static_cast<SharpRuntime::charcs>(code));
            glyphBounds.emplace_back(x, y, w, h);
            // The kerning triple already carries the left bearing, so the crop rectangle only
            // positions the glyph vertically inside the line.
            cropping.emplace_back(0, cropY, w, h);
            kerning.emplace_back(kl, kw, kr);
            (void)cropX;
        } else {
            std::string rest;
            std::getline(lines, rest);
        }
    }
    if (characters.empty() || lineSpacing <= 0) {
        School::Log("font", "font '%s' has no usable metrics", name.c_str());
        return nullptr;
    }

    try {
        Texture2D atlas = content.Load<Texture2D>("Fonts/" + name);
        return std::make_unique<SpriteFont>(std::move(atlas), std::move(glyphBounds), std::move(cropping),
                                            std::move(characters), lineSpacing, spacing, std::move(kerning),
                                            static_cast<SharpRuntime::charcs>(defaultChar));
    } catch (const std::exception& e) {
        School::Log("font", "cannot load atlas for font '%s': %s", name.c_str(), e.what());
        return nullptr;
    }
}
