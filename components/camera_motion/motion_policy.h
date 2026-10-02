#pragma once

// Motion detection and auto exposure maths for camera_motion. Kept free of
// ESP-IDF and ESPHome types so the host firmware tests can check it.

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace esphome::camera_motion {

constexpr uint32_t EXPOSURE_MIN = 4;
constexpr uint32_t GAIN_MIN_X16 = 16;   // 1x
constexpr uint32_t GAIN_MAX_X16 = 128;  // 8x keeps noise manageable
// Auto exposure works on the 8-bit raw grid average.
constexpr float AE_TARGET = 70.0f;
constexpr float AE_LOW = 48.0f;
constexpr float AE_HIGH = 96.0f;

// Share of the grid that must change to count as motion: about a quarter of
// the picture at sensitivity 1, 3.5% at 50, and 0.5% at 100.
inline uint16_t min_changed_cells(float sensitivity, int cells) {
  const float percent = 25.0f * std::pow(0.02f, sensitivity / 100.0f);
  return std::max<uint16_t>(2, static_cast<uint16_t>(std::lround(cells * percent / 100.0f)));
}

// Each area must also change more at low sensitivity: 2x at 1, 1x at 50,
// 0.5x at 100.
inline float cell_threshold_scale(float sensitivity) { return std::pow(2.0f, (50.0f - sensitivity) / 50.0f); }

// Counts grid areas whose brightness changed, after compensating for overall
// brightness drift so gradual light changes and exposure steps are not
// mistaken for movement. `threshold_scale` covers sensor gain and sensitivity.
template<size_t N>
uint16_t count_changed_cells(const std::array<uint8_t, N> &previous, const std::array<uint8_t, N> &current,
                             float previous_mean, float current_mean, float threshold_scale,
                             std::bitset<N> *changed) {
  const float scale = current_mean > 1.0f ? previous_mean / current_mean : 1.0f;
  uint16_t count = 0;
  for (size_t i = 0; i < N; i++) {
    const float before = previous[i];
    const float threshold = (10.0f + before / 16.0f) * threshold_scale;
    if (std::fabs(current[i] * scale - before) > threshold) {
      count++;
      if (changed != nullptr)
        changed->set(i);
    }
  }
  return count;
}

inline bool exposure_in_band(float mean) { return mean >= AE_LOW && mean <= AE_HIGH; }

struct ExposureSetting {
  uint32_t exposure_lines;
  uint32_t gain_x16;
};

// Next exposure and gain for a picture whose grid average is `mean`. Exposure
// is raised before gain, and each step is limited so one odd frame cannot
// swing the picture too far (wider steps while warming up).
inline ExposureSetting next_exposure(float mean, ExposureSetting current, uint32_t exposure_max, bool warming_up) {
  const float max_step = warming_up ? 8.0f : 4.0f;
  const float ratio = std::clamp(AE_TARGET / std::max(mean, 1.0f), 1.0f / max_step, max_step);
  const float wanted = static_cast<float>(current.exposure_lines) * current.gain_x16 * ratio;
  const uint32_t exposure = static_cast<uint32_t>(
      std::clamp(wanted / GAIN_MIN_X16, static_cast<float>(EXPOSURE_MIN), static_cast<float>(exposure_max)));
  const uint32_t gain = static_cast<uint32_t>(
      std::clamp(wanted / exposure, static_cast<float>(GAIN_MIN_X16), static_cast<float>(GAIN_MAX_X16)));
  return {exposure, gain};
}

}  // namespace esphome::camera_motion
