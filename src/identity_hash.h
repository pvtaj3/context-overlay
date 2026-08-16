#pragma once

// Pure, dependency-light hashing helpers for element identity.
//
// Kept free of Windows/UIA types so the fallback identity composition can be
// unit tested on any host (see tests/identity_hash_test.cpp). uia_identity.cpp
// is the only production consumer.

#include <cstdint>
#include <cstddef>
#include <string>

namespace identity_hash {

inline constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
inline constexpr uint64_t kFnvPrime = 1099511628211ULL;

// FNV-1a over raw bytes.
inline void hashBytes(uint64_t& h, const void* data, size_t len) {
    auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= bytes[i];
        h *= kFnvPrime;
    }
}

// Hash a string as a *framed* field: a discriminator tag, then an explicit
// length, then the payload. Framing is what makes the composite fallback
// identity collision-resistant — without it ("ab","c") and ("a","bc") fold to
// the same value, which is exactly how sibling elements with matching
// type/name collided.
inline void hashField(uint64_t& h, const std::wstring& s, unsigned char tag) {
    hashBytes(h, &tag, sizeof(tag));
    const uint32_t len = static_cast<uint32_t>(s.size());
    hashBytes(h, &len, sizeof(len));
    if (len) hashBytes(h, s.data(), s.size() * sizeof(wchar_t));
}

// Every discriminator the UIA probe can contribute to a fallback identity.
// `boundsBytes`/`boundsLen` carry the platform RECT opaquely so this header
// stays Windows-free.
struct FallbackInputs {
    uint64_t windowHandle{};      // full HWND value, never truncated
    std::wstring controlType;
    std::wstring name;
    std::wstring automationId;
    std::wstring className;
    const void* boundsBytes{nullptr};
    size_t boundsLen{0};
    bool haveBounds{false};
    int siblingIndex{-1};         // index among raw-view siblings, -1 = unknown
    uint64_t parentRuntimeHash{0};
};

// Compose the fallback element identity. Two elements collide only if every
// discriminator above matches, which for siblings means same parent, same
// index, same bounds — i.e. the same element.
inline uint64_t composeFallback(const FallbackInputs& in) {
    uint64_t h = kFnvOffsetBasis;
    hashBytes(h, &in.windowHandle, sizeof(in.windowHandle));
    hashField(h, in.controlType, 1);
    hashField(h, in.name, 2);
    hashField(h, in.automationId, 3);
    hashField(h, in.className, 4);
    const unsigned char boundsTag = in.haveBounds ? 5 : 6;
    hashBytes(h, &boundsTag, sizeof(boundsTag));
    if (in.haveBounds && in.boundsBytes && in.boundsLen)
        hashBytes(h, in.boundsBytes, in.boundsLen);
    hashBytes(h, &in.siblingIndex, sizeof(in.siblingIndex));
    hashBytes(h, &in.parentRuntimeHash, sizeof(in.parentRuntimeHash));
    return h;
}

}  // namespace identity_hash
