#pragma once
#include <algorithm>
#include <cstdint>
namespace renderer {
struct Pixel { uint8_t b{}, g{}, r{}, a{}; };
inline uint8_t scaled(uint8_t value, uint8_t alpha) {
    return static_cast<uint8_t>((static_cast<unsigned>(value) * alpha + 127u) / 255u);
}
inline void premultiply(Pixel& p) {
    p.b = scaled(p.b, p.a); p.g = scaled(p.g, p.a); p.r = scaled(p.r, p.a);
}
inline uint8_t surfaceAlpha(bool darkMode, bool backdrop, float profileOpacity) {
    const float clamped = std::clamp(profileOpacity, 0.0f, 1.0f);
    const unsigned base = (darkMode || !backdrop) ? 255u : 210u;
    return static_cast<uint8_t>(base * clamped + 0.5f);
}
}
