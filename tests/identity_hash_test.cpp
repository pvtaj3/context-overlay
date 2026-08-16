// Host-native unit tests for the fallback element identity composition.
//
// These are the regression tests for the review finding "sibling elements with
// matching control types/names collide in the fallback hash". They compile with
// the host compiler (no Windows headers needed) because identity_hash.h is
// deliberately platform-free.

#include "../src/identity_hash.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

struct FakeRect {
    long left, top, right, bottom;
};

// A sibling in a list: same control type and name as its neighbours, different
// position/index. Exactly the case that used to collide.
identity_hash::FallbackInputs sibling(int index, const FakeRect& rc,
                                      const FakeRect** keep) {
    identity_hash::FallbackInputs in;
    in.windowHandle = 0x00007FF6ABCD1234ULL;
    in.controlType = L"ListItem";
    in.name = L"Item";
    in.automationId = L"";
    in.className = L"";
    in.boundsBytes = &rc;
    in.boundsLen = sizeof(rc);
    in.haveBounds = true;
    in.siblingIndex = index;
    in.parentRuntimeHash = 0xAABBCCDDEEFF0011ULL;
    (void)keep;
    return in;
}

}  // namespace

int main() {
    using identity_hash::composeFallback;

    // 1. Two siblings with identical control type + name must NOT collide.
    {
        FakeRect r0{0, 0, 200, 24};
        FakeRect r1{0, 24, 200, 48};
        auto a = sibling(0, r0, nullptr);
        auto b = sibling(1, r1, nullptr);
        expect(composeFallback(a) != composeFallback(b),
               "siblings with same type+name differ (index+bounds)");
    }

    // 2. Same index but different bounds must differ (providers that don't
    //    expose a usable sibling walk still get separated by geometry).
    {
        FakeRect r0{0, 0, 200, 24};
        FakeRect r1{0, 24, 200, 48};
        auto a = sibling(-1, r0, nullptr);
        auto b = sibling(-1, r1, nullptr);
        expect(composeFallback(a) != composeFallback(b),
               "same index, different bounds differ");
    }

    // 3. Same geometry under different parents must differ.
    {
        FakeRect r{0, 0, 200, 24};
        auto a = sibling(0, r, nullptr);
        auto b = sibling(0, r, nullptr);
        b.parentRuntimeHash = 0x1122334455667788ULL;
        expect(composeFallback(a) != composeFallback(b),
               "different parent runtime id differs");
    }

    // 4. Field framing: concatenation ambiguity must not collide. Pre-fix the
    //    unframed mix() folded ("ab","c") and ("a","bc") together.
    {
        FakeRect r{0, 0, 10, 10};
        auto a = sibling(0, r, nullptr);
        a.controlType = L"ab";
        a.name = L"c";
        auto b = sibling(0, r, nullptr);
        b.controlType = L"a";
        b.name = L"bc";
        expect(composeFallback(a) != composeFallback(b),
               "field framing defeats concatenation ambiguity");
    }

    // 5. AutomationId alone must separate two otherwise identical elements.
    {
        FakeRect r{0, 0, 10, 10};
        auto a = sibling(0, r, nullptr);
        a.automationId = L"row-1";
        auto b = sibling(0, r, nullptr);
        b.automationId = L"row-2";
        expect(composeFallback(a) != composeFallback(b),
               "differing AutomationId differs");
    }

    // 6. ClassName alone must separate.
    {
        FakeRect r{0, 0, 10, 10};
        auto a = sibling(0, r, nullptr);
        a.className = L"Chrome_RenderWidgetHostHWND";
        auto b = sibling(0, r, nullptr);
        b.className = L"Edit";
        expect(composeFallback(a) != composeFallback(b),
               "differing ClassName differs");
    }

    // 7. The full HWND is hashed, not just its low 32 bits: two windows that
    //    differ only above bit 32 must not collide.
    {
        FakeRect r{0, 0, 10, 10};
        auto a = sibling(0, r, nullptr);
        a.windowHandle = 0x0000000100001234ULL;
        auto b = sibling(0, r, nullptr);
        b.windowHandle = 0x0000000200001234ULL;
        expect(composeFallback(a) != composeFallback(b),
               "full HWND hashed (high bits significant)");
    }

    // 8. Determinism: identical inputs give identical hashes.
    {
        FakeRect r{0, 0, 10, 10};
        auto a = sibling(3, r, nullptr);
        auto b = sibling(3, r, nullptr);
        expect(composeFallback(a) == composeFallback(b),
               "identical inputs are stable");
    }

    // 9. "No bounds" is distinguishable from "bounds that happen to be zero".
    {
        FakeRect zero{0, 0, 0, 0};
        auto a = sibling(0, zero, nullptr);
        a.haveBounds = true;
        auto b = sibling(0, zero, nullptr);
        b.haveBounds = false;
        expect(composeFallback(a) != composeFallback(b),
               "missing bounds distinct from zero bounds");
    }

    std::printf(failures ? "\n%d test(s) FAILED\n" : "\nall tests passed\n",
                failures);
    return failures ? 1 : 0;
}
