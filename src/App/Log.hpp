#pragma once

#include <cstdarg>
#include <cstdio>

namespace School {

// Notes and warnings go to stderr so they never interleave with CNA's own stdout logging.
inline void Log(const char* tag, const char* format, ...)
{
    std::fprintf(stderr, "[school][%s] ", tag);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

}  // namespace School
