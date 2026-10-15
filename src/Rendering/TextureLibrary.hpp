#pragma once

#include <map>
#include <memory>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

/**
 * Textures by short name ("brick" -> Content/Textures/brick.png), loaded through
 * CNA's ContentManager. Every texture is re-uploaded with a CPU mip chain so
 * distant surfaces do not shimmer. A missing asset yields a loud magenta checker
 * instead of an exception, so one bad file never stops the school from opening.
 */
class TextureLibrary {
public:
    TextureLibrary(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                   Microsoft::Xna::Framework::Content::ContentManager& content);

    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* Get(const std::string& name);
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* White() const { return white_.get(); }
    /** Adds a texture created at run time (signs, generated images) under `name`. */
    Microsoft::Xna::Framework::Graphics::Texture2D*
    Register(const std::string& name, std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> texture);
    /** Releases a texture added with Register(). Unknown names are ignored. */
    void Unregister(const std::string& name) { textures_.erase(name); }
    /** Loads every PNG of `directory` now (decoding, mip chain and upload cost milliseconds each: better at start-up than mid-frame). */
    void Preload(const std::string& directory);
    [[nodiscard]] int MissingCount() const { return missing_; }
    [[nodiscard]] std::size_t Count() const { return textures_.size(); }

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    Microsoft::Xna::Framework::Content::ContentManager& content_;
    std::map<std::string, std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>> textures_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> white_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> missingTexture_;
    int missing_ = 0;
};
