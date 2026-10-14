#include "Rendering/TextureLibrary.hpp"

#include <exception>
#include <vector>

#include "App/Log.hpp"
#include "Rendering/ImageUtil.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

TextureLibrary::TextureLibrary(GraphicsDevice& device, Content::ContentManager& content)
    : device_(device), content_(content)
{
    white_ = std::make_unique<Texture2D>(device_, 1, 1);
    const Color white = Color::White;
    white_->SetData(&white, 1);

    std::vector<Color> checker(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            checker[static_cast<std::size_t>(y * 64 + x)] = (((x / 8) + (y / 8)) % 2 == 0) ? Color(255, 0, 255, 255) : Color(20, 20, 20, 255);
    missingTexture_ = ImageUtil::CreateMipmapped(device_, checker, 64, 64);
}

Texture2D* TextureLibrary::Register(const std::string& name, std::unique_ptr<Texture2D> texture)
{
    Texture2D* ptr = texture.get();
    textures_[name] = std::move(texture);
    return ptr;
}

Texture2D* TextureLibrary::Get(const std::string& name)
{
    if (name == "white") return white_.get();
    auto it = textures_.find(name);
    if (it != textures_.end()) return it->second ? it->second.get() : missingTexture_.get();

    std::unique_ptr<Texture2D> result;
    try {
        Texture2D loaded = content_.Load<Texture2D>("Textures/" + name);
        const int w = loaded.getWidthProperty();
        const int h = loaded.getHeightProperty();
        std::vector<Color> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        loaded.GetData(pixels.data(), static_cast<int>(pixels.size()));
        result = ImageUtil::CreateMipmapped(device_, pixels, w, h);
    } catch (const std::exception& e) {
        School::Log("textures", "missing or unreadable texture '%s': %s", name.c_str(), e.what());
        ++missing_;
    }
    Texture2D* ptr = result ? result.get() : missingTexture_.get();
    if (result) textures_[name] = std::move(result);
    else textures_[name] = nullptr;  // remember the failure; do not retry every frame
    return ptr;
}
