#pragma once

#include <windows.h>

#include "types.h"

// A topmost, transparent, click-through layered window.
//
// Phase One responsibility: own the Win32 window, keep it non-activating and
// click-through, and paint a simple translucent test card on demand near the
// cursor. The real Direct2D/DirectWrite card renderer is a later phase; here we
// use a GDI DIB section + UpdateLayeredWindow with a single global alpha, which
// is the most robust, dependency-light way to stand up the layered shell.
class OverlayWindow {
public:
    bool create(HINSTANCE instance);
    void showCardAt(POINT anchor);
    void showIdentity(const HoverTarget& target);
    void hide();
    HWND hwnd() const { return hwnd_; }

    ~OverlayWindow();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void ensureDib(int width, int height);
    void paintCard(HDC hdc, int width, int height, POINT anchor);

    HWND hwnd_{};
    HBITMAP dib_{};        // 32-bit DIB section used as the layered surface
    void* dibBits_{};      // DIB pixel buffer (owned by dib_)
    int width_{};
    int height_{};
};
