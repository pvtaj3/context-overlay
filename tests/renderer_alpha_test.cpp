#include "../src/renderer_alpha.h"
#include <cstdio>
int main() {
    renderer::Pixel p{100, 150, 200, 128}; renderer::premultiply(p);
    if (p.b != 50 || p.g != 75 || p.r != 100 ||
        renderer::surfaceAlpha(true, true, 1.0f) != 255 ||
        renderer::surfaceAlpha(false, true, 1.0f) != 210 ||
        renderer::surfaceAlpha(true, true, .5f) != 128 ||
        renderer::surfaceAlpha(false, false, 2.0f) != 255) return 1;
    std::puts("renderer alpha tests passed"); return 0;
}
