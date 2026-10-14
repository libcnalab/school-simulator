#pragma once

// Deterministic randomness. Everything random in the simulation derives from a seed through these
// functions, so a run can be reproduced exactly. There is no global generator and no use of time.

#include <cstdint>

namespace School {

/** SplitMix64 finaliser: a good 64-bit mix, used both to hash and to advance the generator. */
constexpr std::uint64_t Mix64(std::uint64_t z)
{
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/** Order-dependent hash of a few integers; the way per-agent, per-day random values are derived. */
constexpr std::uint64_t HashCombine(std::uint64_t a, std::uint64_t b)
{
    return Mix64(a ^ (Mix64(b) + 0x9E3779B97F4A7C15ULL + (a << 6) + (a >> 2)));
}
constexpr std::uint64_t Hash(std::uint64_t a, std::uint64_t b, std::uint64_t c = 0, std::uint64_t d = 0)
{
    return HashCombine(HashCombine(HashCombine(a, b), c), d);
}

/** Uniform float in [0, 1) from a hash value. */
constexpr float Unit(std::uint64_t h)
{
    return static_cast<float>(h >> 40) * (1.0f / 16777216.0f);
}

/** Small stream generator (xoshiro-free: SplitMix64 counter). Cheap, deterministic, copyable. */
class Rng {
public:
    constexpr explicit Rng(std::uint64_t seed = 1) : state_(seed) {}
    constexpr std::uint64_t Next() { return Mix64(state_ += 0x9E3779B97F4A7C15ULL); }
    constexpr float Uniform() { return Unit(Next()); }
    constexpr float Range(float lo, float hi) { return lo + (hi - lo) * Uniform(); }
    /** Integer in [0, n). n must be positive. */
    constexpr int Below(int n) { return static_cast<int>(Next() % static_cast<std::uint64_t>(n)); }
    constexpr bool Chance(float p) { return Uniform() < p; }
    [[nodiscard]] constexpr std::uint64_t State() const { return state_; }

private:
    std::uint64_t state_;
};

}  // namespace School
