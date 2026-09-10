#include "../src/style_profile.h"
#include <cstdio>

int main() {
    style::StyleProfile defaults, raw;
    raw.density = -1.0f; raw.motion = 2.0f; raw.opacity = 0.7f;
    raw.typographyScale = 3.0f; raw.cornerRadius = -4.0f; raw.confidence = 4.0f;
    const auto r = style::PreferenceMatcher::resolve(defaults, &raw);
    if (r.density != 0.0f || r.motion != 1.0f || r.typographyScale != 1.5f ||
        r.cornerRadius != 0.0f || r.confidence != 1.0f || r.opacity != 0.7f) return 1;
    raw.theme = style::Theme::kDark;
    if (!style::PreferenceMatcher::prefersDark(raw, false) ||
        style::PreferenceMatcher::prefersDark(style::StyleProfile{}, false) ||
        !style::PreferenceMatcher::prefersDark(style::StyleProfile{}, true)) return 1;
    if (style::PreferenceMatcher::surfaceAlpha(style::Theme::kDark, true) != 255 ||
        style::PreferenceMatcher::surfaceAlpha(style::Theme::kLight, true) != 210 ||
        style::PreferenceMatcher::surfaceAlpha(style::Theme::kLight, false) != 255) return 1;
    std::puts("style profile tests passed"); return 0;
}
