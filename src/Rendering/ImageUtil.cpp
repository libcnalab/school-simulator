#include "Rendering/ImageUtil.hpp"

#include <algorithm>
#include <cstdint>

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace ImageUtil {

std::vector<std::vector<Color>> BuildMipChain(const std::vector<Color>& pixels, int width, int height)
{
    std::vector<std::vector<Color>> chain;
    chain.push_back(pixels);
    int w = width;
    int h = height;
    while (w > 1 || h > 1) {
        const int nw = std::max(1, w / 2);
        const int nh = std::max(1, h / 2);
        const std::vector<Color>& src = chain.back();
        std::vector<Color> dst(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh));
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                int r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = std::min(w - 1, x * 2 + dx);
                        const int sy = std::min(h - 1, y * 2 + dy);
                        const Color& c = src[static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(sx)];
                        r += c.getRProperty(); g += c.getGProperty(); b += c.getBProperty(); a += c.getAProperty();
                        ++n;
                    }
                }
                dst[static_cast<std::size_t>(y) * static_cast<std::size_t>(nw) + static_cast<std::size_t>(x)] =
                    Color(static_cast<std::uint8_t>(r / n), static_cast<std::uint8_t>(g / n),
                          static_cast<std::uint8_t>(b / n), static_cast<std::uint8_t>(a / n));
            }
        }
        chain.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
    return chain;
}

std::unique_ptr<Texture2D> CreateFromMipChain(GraphicsDevice& device, const std::vector<std::vector<Color>>& chain,
                                              int width, int height)
{
    auto texture = std::make_unique<Texture2D>(device, width, height, true, SurfaceFormat::Color);
    int w = width;
    int h = height;
    const int levels = std::min(static_cast<int>(chain.size()), texture->getLevelCountProperty());
    for (int level = 0; level < levels; ++level) {
        const Rectangle rect(0, 0, w, h);
        texture->SetData(level, &rect, chain[static_cast<std::size_t>(level)].data(), 0,
                         static_cast<int>(chain[static_cast<std::size_t>(level)].size()));
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    return texture;
}

std::unique_ptr<Texture2D> CreateMipmapped(GraphicsDevice& device, const std::vector<Color>& pixels, int width, int height)
{
    return CreateFromMipChain(device, BuildMipChain(pixels, width, height), width, height);
}

}  // namespace ImageUtil
