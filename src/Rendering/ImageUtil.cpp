#include "Rendering/ImageUtil.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

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

namespace {

std::uint32_t Crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0)
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1U) ? 0xEDB88320U ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    return ~crc;
}

void PutU32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(static_cast<std::uint8_t>(x >> 24)); v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8)); v.push_back(static_cast<std::uint8_t>(x));
}

void PutChunk(std::vector<std::uint8_t>& out, const char* tag, const std::vector<std::uint8_t>& body)
{
    PutU32(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> tagged(tag, tag + 4);
    tagged.insert(tagged.end(), body.begin(), body.end());
    out.insert(out.end(), tagged.begin(), tagged.end());
    PutU32(out, Crc32(tagged.data(), tagged.size()));
}

}  // namespace

bool WritePng(const char* path, const Color* pixels, int width, int height)
{
    // Stored (uncompressed) deflate blocks: no compressor needed for debug screenshots.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (static_cast<std::size_t>(width) * 4 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            const Color& c = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            raw.push_back(c.getRProperty()); raw.push_back(c.getGProperty());
            raw.push_back(c.getBProperty()); raw.push_back(255);
        }
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::size_t pos = 0;
    while (pos < raw.size()) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n)); z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n)); z.push_back(static_cast<std::uint8_t>((~n) >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    }
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : raw) { a = (a + byte) % 65521U; b = (b + a) % 65521U; }
    PutU32(z, (b << 16) | a);

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    PutU32(ihdr, static_cast<std::uint32_t>(width));
    PutU32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    PutChunk(png, "IHDR", ihdr);
    PutChunk(png, "IDAT", z);
    PutChunk(png, "IEND", {});

    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

}  // namespace ImageUtil
