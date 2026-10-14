#pragma once

#include <string>

namespace AssetPaths {

/**
 * The directory holding Textures/, Models/, Audio/ and Fonts/. Searched in order:
 * $SCHOOL_CONTENT, <title directory>/Content (the build copies Content/ there), and
 * the source tree's Content/ (so an in-tree run works after a move of the build dir).
 * Returns an empty string when none of them exists.
 */
std::string FindContentRoot();

/** Reads a whole text file into `out`. Returns false when it cannot be opened. */
bool ReadTextFile(const std::string& path, std::string& out);

}  // namespace AssetPaths
