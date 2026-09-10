#pragma once

#include <algorithm>

namespace style {

enum class Theme { kSystem, kLight, kDark };
enum class Provenance { kDefault, kImported, kUser, kFeedback };

struct StyleProfile {
    Theme theme{Theme::kSystem};
    float density{0.5f};
    float motion{0.35f};
    float cornerRadius{8.0f};
    float opacity{1.0f};
    float typographyScale{1.0f};
    float informationDensity{0.5f};
    Provenance provenance{Provenance::kDefault};
    float confidence{0.0f};
};

struct PreferenceMatcher {
    static float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

    static StyleProfile normalize(StyleProfile profile) {
        profile.density = clamp01(profile.density);
        profile.motion = clamp01(profile.motion);
        profile.opacity = clamp01(profile.opacity);
        profile.informationDensity = clamp01(profile.informationDensity);
        profile.typographyScale = std::clamp(profile.typographyScale, 0.75f, 1.5f);
        profile.cornerRadius = std::clamp(profile.cornerRadius, 0.0f, 32.0f);
        profile.confidence = clamp01(profile.confidence);
        return profile;
    }

    static StyleProfile resolve(const StyleProfile& defaults,
                                const StyleProfile* preference) {
        return normalize(preference ? *preference : defaults);
    }

    static unsigned char surfaceAlpha(Theme theme, bool hasBackdrop) {
        return (theme == Theme::kDark || !hasBackdrop) ? 255 : 210;
    }

    static bool prefersDark(const StyleProfile& profile, bool systemDark) {
        if (profile.theme == Theme::kDark) return true;
        if (profile.theme == Theme::kLight) return false;
        return systemDark;
    }
};

}  // namespace style
