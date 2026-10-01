#include "components/jc4880_camera_capture/capture_grid.h"
#include <cassert>

int main() {
  using esphome::jc4880_camera_capture::rgb565_luma;
  using esphome::jc4880_camera_capture::frame_timed_out;
  assert(!frame_timed_out(31000, 31004));  // Conversion advanced the frame clock.
  assert(!frame_timed_out(5000, 0));
  assert(frame_timed_out(5001, 0));
  assert(!frame_timed_out(20, 0xfffffff0u));  // Millisecond rollover.
  assert(frame_timed_out(6000, 0xfffffff0u));
  assert(rgb565_luma(0x0000) == 0);
  assert(rgb565_luma(0xffff) == 255);
  assert(rgb565_luma(0xf800) == 76);   // Pure red.
  assert(rgb565_luma(0x07e0) == 149);  // Pure green.
  assert(rgb565_luma(0x001f) == 28);   // Pure blue.
  unsigned previous = 0;
  for (unsigned level = 0; level < 32; level++) {
    auto gray = static_cast<uint16_t>((level << 11) | ((level * 2) << 5) | level);
    unsigned current = rgb565_luma(gray);
    assert(current >= previous);
    previous = current;
  }
}
