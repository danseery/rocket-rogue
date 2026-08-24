#pragma once

namespace rocket::flight_instrument_layout {

inline constexpr float kAspectRatio = 864.0F / 1821.0F;
inline constexpr float kMaximumWidthPixels = 380.0F;
inline constexpr float kMinimumWidthPixels = 240.0F;
inline constexpr float kSceneInsetPixels = 12.0F;

struct Rect {
    float left;
    float top;
    float width;
    float height;
};

// All coordinates are normalized against the 1821 by 864 bezel. Readouts are
// deliberately centered under their matching dial and below every needle arc.
inline constexpr float kTemperatureDialCenterX = 0.267F;
inline constexpr float kTemperatureDialCenterY = 0.468F;
inline constexpr float kTemperatureNeedleRadius = 0.073F;
inline constexpr float kSpeedDialCenterX = 0.500F;
inline constexpr float kSpeedDialCenterY = 0.374F;
inline constexpr float kSpeedNeedleRadius = 0.114F;
inline constexpr float kFuelDialCenterX = 0.733F;
inline constexpr float kFuelDialCenterY = 0.468F;
inline constexpr float kFuelNeedleRadius = 0.073F;
// The lower physical bays are intentionally not centered under their dials;
// use their bezel-aperture centers for the live numeric readouts.
inline constexpr float kTemperatureReadoutCenterX = 0.274F;
inline constexpr float kFuelReadoutCenterX = 0.725F;

inline constexpr Rect kTemperatureLabel {0.207F, 0.535F, 0.120F, 0.040F};
inline constexpr Rect kSpeedLabel {0.440F, 0.496F, 0.120F, 0.040F};
inline constexpr Rect kFuelLabel {0.673F, 0.535F, 0.120F, 0.040F};
inline constexpr Rect kTemperatureReadout {0.1515F, 0.704F, 0.245F, 0.066F};
inline constexpr Rect kSpeedReadout {0.400F, 0.688F, 0.200F, 0.105F};
inline constexpr Rect kFuelReadout {0.6025F, 0.704F, 0.245F, 0.066F};
inline constexpr Rect kThrottleTray {0.415F, 0.829F, 0.170F, 0.045F};
inline constexpr Rect kNavigationWarning {0.430F, 0.037F, 0.140F, 0.060F};

} // namespace rocket::flight_instrument_layout
