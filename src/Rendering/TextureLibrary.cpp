#include "Rendering/TextureLibrary.hpp"

#include <filesystem>
#include <system_error>

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

    // Short texture names and run-time labels, used verbatim.
    textures_.SetNormalizeKeys(false);
    textures_.RegisterLoader<Texture2D>([this](const std::string& name) {
        Texture2D loaded = content_.Load<Texture2D>("Textures/" + name);
        const int w = loaded.getWidthProperty();
        const int h = loaded.getHeightProperty();
        std::vector<Color> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        loaded.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return std::shared_ptr<Texture2D>(ImageUtil::CreateMipmapped(device_, pixels, w, h));
    });
    textures_.SetFallback<Texture2D>(missingTexture_);
}

Texture2D* TextureLibrary::Register(const std::string& name, std::unique_ptr<Texture2D> texture)
{
    textures_.Invalidate<Texture2D>(name);
    return textures_.GetOrCreate<Texture2D>(name, [&] { return texture.release(); }).get();
}

void TextureLibrary::Preload(const std::string& directory)
{
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == ".png") (void)Get(entry.path().stem().string());
}

Texture2D* TextureLibrary::Get(const std::string& name)
{
    if (name == "white") return white_.get();
    const std::size_t failures = textures_.Failures().size();
    Texture2D* texture = textures_.Get<Texture2D>(name).get();
    // The checker is cached under the failed name: the load is not retried every frame, and reported once.
    if (textures_.Failures().size() > failures) {
        School::Log("textures", "missing or unreadable texture '%s': %s", name.c_str(), textures_.Failures().back().message.c_str());
        ++missing_;
    }
    return texture;
}
