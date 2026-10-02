#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "motion_policy.h"

using namespace esphome::camera_motion;

namespace {

constexpr int CELLS = 576;  // 32 x 18 grid
using Grid = std::array<uint8_t, CELLS>;

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

Grid filled(uint8_t value) {
  Grid grid{};
  grid.fill(value);
  return grid;
}

float mean_of(const Grid &grid) {
  float total = 0.0f;
  for (uint8_t value : grid)
    total += value;
  return total / grid.size();
}

uint16_t changed_between(const Grid &before, const Grid &after, float scale, std::bitset<CELLS> *changed = nullptr) {
  return count_changed_cells(before, after, mean_of(before), mean_of(after), scale, changed);
}

void test_sensitivity_mapping() {
  check(min_changed_cells(1, CELLS) == 138, "sensitivity 1 needs about a quarter of the picture");
  check(min_changed_cells(50, CELLS) == 20, "sensitivity 50 needs about 3.5% of the picture");
  check(min_changed_cells(100, CELLS) == 3, "sensitivity 100 needs about 0.5% of the picture");
  bool decreasing = true;
  for (int s = 2; s <= 100; s++)
    decreasing = decreasing && min_changed_cells(s, CELLS) <= min_changed_cells(s - 1, CELLS);
  check(decreasing, "higher sensitivity never needs more of the picture to change");
  check(min_changed_cells(100, 20) == 2, "at least two areas must always change");

  check(std::fabs(cell_threshold_scale(50) - 1.0f) < 1e-6f, "sensitivity 50 keeps the base area threshold");
  check(std::fabs(cell_threshold_scale(100) - 0.5f) < 1e-6f, "sensitivity 100 halves the area threshold");
  check(cell_threshold_scale(1) > 1.9f, "sensitivity 1 roughly doubles the area threshold");
}

void test_change_counting() {
  const Grid scene = filled(70);
  check(changed_between(scene, scene, 1.0f) == 0, "an unchanged picture has no changed areas");

  Grid noisy = scene;
  for (int i = 0; i < CELLS; i++)
    noisy[i] = static_cast<uint8_t>(70 + (i % 7) - 3);
  check(changed_between(scene, noisy, 1.0f) == 0, "small sensor noise is ignored");

  check(changed_between(scene, filled(140), 1.0f) == 0, "a whole-picture brightness change is not movement");

  Grid person = scene;
  for (int i = 100; i < 140; i++)
    person[i] = 20;
  std::bitset<CELLS> changed;
  check(changed_between(scene, person, 1.0f, &changed) >= 40, "a dark shape entering the picture is counted");
  check(changed.test(100) && changed.test(139), "changed areas are marked for the preview");

  Grid moderate = scene;
  for (int i = 0; i < 30; i++)
    moderate[i] = 90;
  check(changed_between(scene, moderate, 1.0f) == 30, "a moderate change counts at normal sensitivity");
  check(changed_between(scene, moderate, 2.0f) == 0, "a moderate change is ignored at low sensitivity");
}

void test_auto_exposure() {
  const uint32_t max_lines = 4640;
  check(exposure_in_band(70.0f), "the target brightness needs no adjustment");
  check(!exposure_in_band(20.0f) && !exposure_in_band(150.0f), "dark and bright pictures need adjustment");

  const ExposureSetting dark = next_exposure(10.0f, {1000, GAIN_MIN_X16}, max_lines, false);
  check(dark.exposure_lines > 1000, "a dark picture lengthens the exposure");
  check(dark.gain_x16 == GAIN_MIN_X16, "exposure is raised before gain");

  const ExposureSetting darker = next_exposure(5.0f, {max_lines, GAIN_MIN_X16}, max_lines, false);
  check(darker.exposure_lines == max_lines && darker.gain_x16 > GAIN_MIN_X16,
        "gain rises once the exposure is at its longest");

  const ExposureSetting capped = next_exposure(1.0f, {max_lines, GAIN_MAX_X16}, max_lines, true);
  check(capped.exposure_lines == max_lines && capped.gain_x16 == GAIN_MAX_X16,
        "exposure and gain stop at their limits");

  const ExposureSetting bright = next_exposure(250.0f, {2000, 64}, max_lines, false);
  check(static_cast<float>(bright.exposure_lines) * bright.gain_x16 < 2000.0f * 64,
        "a bright picture reduces exposure");
  check(bright.gain_x16 == GAIN_MIN_X16, "gain is lowered before exposure is shortened");

  const ExposureSetting floor = next_exposure(255.0f, {EXPOSURE_MIN, GAIN_MIN_X16}, max_lines, true);
  check(floor.exposure_lines == EXPOSURE_MIN && floor.gain_x16 == GAIN_MIN_X16,
        "exposure never drops below its minimum");

  const ExposureSetting step = next_exposure(1.0f, {100, GAIN_MIN_X16}, max_lines, false);
  check(step.exposure_lines == 400, "one adjustment is limited to four times brighter after warm-up");
}

}  // namespace

int main() {
  test_sensitivity_mapping();
  test_change_counting();
  test_auto_exposure();
  if (failures != 0) {
    std::fprintf(stderr, "%d camera motion policy check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("Camera motion policy tests passed.");
  return EXIT_SUCCESS;
}
