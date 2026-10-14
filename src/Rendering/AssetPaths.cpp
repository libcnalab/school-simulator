#include "Rendering/AssetPaths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Microsoft/Xna/Framework/TitleLocation.hpp"

namespace fs = std::filesystem;

namespace {
bool LooksLikeContent(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p / "Textures", ec);
}
}  // namespace

std::string AssetPaths::FindContentRoot()
{
    if (const char* env = std::getenv("SCHOOL_CONTENT")) {
        if (LooksLikeContent(env)) return env;
    }
    const fs::path title = Microsoft::Xna::Framework::TitleLocation::Path();
    if (LooksLikeContent(title / "Content")) return (title / "Content").string();
#ifdef SCHOOL_SOURCE_CONTENT_DIR
    if (LooksLikeContent(SCHOOL_SOURCE_CONTENT_DIR)) return SCHOOL_SOURCE_CONTENT_DIR;
#endif
    return {};
}

bool AssetPaths::ReadTextFile(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}
