#pragma once
#include <cstdint>

namespace esphome::jc4880_camera_capture {
// RGB565 is the ISP output, not raw Bayer data. Integer BT.601 approximation.
inline uint8_t rgb565_luma(uint16_t pixel) {
  unsigned r = ((pixel >> 11) & 31) * 255 / 31;
  unsigned g = ((pixel >> 5) & 63) * 255 / 63;
  unsigned b = (pixel & 31) * 255 / 31;
  return (77 * r + 150 * g + 29 * b) >> 8;
}

inline bool frame_timed_out(uint32_t now, uint32_t last_frame) {
  uint32_t elapsed = now - last_frame;
  // Ignore a timestamp sampled just before last_frame; also handle millis wrap.
  return elapsed > 5000 && elapsed <= 0x7fffffffu;
}
}  // namespace esphome::jc4880_camera_capture
