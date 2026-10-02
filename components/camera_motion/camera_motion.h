#pragma once

// Motion detection from the built-in OV02C10 camera on the 7" JC1060P470
// panels. The camera only streams while the panel asks for it (normally while
// the screen is asleep); frames are reduced to a coarse brightness grid. The
// only picture that leaves the panel is the small settings-page preview, and
// only while that page is asking for it.

#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "motion_policy.h"
#include "ov02c10_modes.h"

#include "driver/isp.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace esphome::camera_motion {

enum class SensorMode : uint8_t {
  MIPI_2LANE_1920X1080 = 0,
  MIPI_1LANE_1288X728 = 1,
};

struct PreviewInfo {
  float motion_level;
  float brightness;
  bool motion;
};

class CameraMotionComponent : public Component, public i2c::I2CDevice {
 public:
  static constexpr int GRID_W = 32;
  static constexpr int GRID_H = 18;
  static constexpr int GRID_CELLS = GRID_W * GRID_H;
  static constexpr int PREVIEW_W = 320;
  static constexpr int PREVIEW_H = 180;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_sensor_mode(SensorMode mode) { this->sensor_mode_ = mode; }
  void set_line_sync(bool line_sync) { this->line_sync_ = line_sync; }
  void set_frame_rate_divider(uint8_t divider) { this->frame_rate_divider_ = divider; }
  void set_motion_binary_sensor(binary_sensor::BinarySensor *sensor) { this->motion_binary_sensor_ = sensor; }
  void set_motion_level_sensor(sensor::Sensor *sensor) { this->motion_level_sensor_ = sensor; }
  void set_brightness_sensor(sensor::Sensor *sensor) { this->brightness_sensor_ = sensor; }
  void set_status_text_sensor(text_sensor::TextSensor *sensor) { this->status_text_sensor_ = sensor; }

  template<typename F> void add_on_motion_callback(F &&callback) {
    this->motion_callback_.add(std::forward<F>(callback));
  }

  // Called from YAML about once a second. `wake_armed` means the screen is
  // asleep and camera wake is enabled; `test_mode` keeps the camera running
  // regardless so motion levels can be watched while the screen is on.
  void set_run_request(bool wake_armed, bool test_mode);
  // 1 (least sensitive) to 100 (most sensitive).
  void set_sensitivity(float sensitivity);
  // Logs the next processed frame as a small text picture.
  void request_picture_log() { this->picture_log_requested_ = true; }

  // Web server task: keeps the camera running for the settings-page preview
  // for a few more seconds.
  void request_preview();
  // Web server task: passes the latest preview (a BMP file) to `send` while it
  // is locked. Returns false when no recent preview is ready yet.
  bool with_preview(const std::function<void(const uint8_t *, size_t, const PreviewInfo &)> &send);

  bool is_running() const { return this->state_ == State::RUNNING; }

 protected:
  enum class State : uint8_t {
    IDLE,
    SENSOR_RESET_WAIT,
    SENSOR_INIT,
    RUNNING,
    STOPPING,
    FAILED,
  };

  struct ModeInfo {
    const char *name;
    const Ov02c10Reg *regs;
    size_t reg_count;
    uint16_t width;
    uint16_t height;
    uint8_t lanes;
    int lane_bit_rate_mbps;
  };

  const ModeInfo &mode_info_() const;
  bool probe_sensor_();
  bool write_reg_(uint16_t reg, uint8_t value);
  bool read_reg_(uint16_t reg, uint8_t *value);
  void begin_start_();
  bool continue_sensor_init_();
  bool finish_sensor_init_();
  bool start_pipeline_();
  void release_pipeline_();
  bool set_sensor_stream_(bool enable);
  void begin_stop_();
  void clear_outputs_();
  void handle_start_failure_(const char *reason);
  void process_frame_();
  void run_auto_exposure_(float mean);
  bool apply_exposure_(uint32_t exposure_lines, uint32_t gain_x16);
  uint16_t min_changed_cells_() const;
  void log_picture_(float mean) const;
  bool preview_active_(uint32_t now);
  void render_preview_();
  void publish_status_(const std::string &status);
  void publish_periodic_(uint32_t now);

  SensorMode sensor_mode_{SensorMode::MIPI_2LANE_1920X1080};
  bool line_sync_{true};
  uint8_t frame_rate_divider_{4};

  binary_sensor::BinarySensor *motion_binary_sensor_{nullptr};
  sensor::Sensor *motion_level_sensor_{nullptr};
  sensor::Sensor *brightness_sensor_{nullptr};
  text_sensor::TextSensor *status_text_sensor_{nullptr};
  CallbackManager<void()> motion_callback_{};

  State state_{State::IDLE};
  bool sensor_present_{false};
  bool sensor_initialized_{false};
  bool disabled_after_crash_{false};
  bool wake_armed_{false};
  bool test_mode_{false};
  bool picture_log_requested_{false};
  float sensitivity_{50.0f};
  std::string last_status_;

  size_t init_index_{0};
  uint32_t state_since_ms_{0};
  uint32_t retry_after_ms_{0};
  uint8_t start_failures_{0};

  uint32_t vts_{0};
  uint32_t exposure_max_{0};
  uint32_t exposure_lines_{0};
  uint32_t gain_x16_{16};
  uint8_t ae_out_of_band_frames_{0};
  uint8_t skip_compare_frames_{0};

  esp_cam_ctlr_handle_t csi_{nullptr};
  bool csi_enabled_{false};
  bool csi_started_{false};
  isp_proc_handle_t isp_{nullptr};
  bool isp_enabled_{false};
  uint8_t *frame_buffer_{nullptr};
  size_t frame_buffer_size_{0};

  std::array<uint8_t, GRID_CELLS> grid_{};
  std::array<uint8_t, GRID_CELLS> prev_grid_{};
  bool have_prev_{false};
  float prev_mean_{0.0f};
  float last_mean_{NAN};
  uint32_t running_since_ms_{0};
  uint32_t last_frame_ms_{0};
  uint32_t last_motion_ms_{0};
  uint32_t last_callback_ms_{0};
  bool motion_active_{false};
  float window_max_level_{0.0f};
  uint32_t last_publish_ms_{0};
  uint32_t last_summary_ms_{0};
  uint32_t fps_window_start_ms_{0};
  uint32_t fps_window_frames_{0};
  float measured_fps_{0.0f};
  uint32_t processed_frames_{0};

  std::bitset<GRID_CELLS> changed_cells_{};
  std::atomic<uint32_t> preview_until_ms_{0};
  SemaphoreHandle_t preview_lock_{nullptr};
  uint8_t *preview_bmp_{nullptr};
  uint32_t preview_rendered_ms_{0};
  PreviewInfo preview_info_{};
  float last_level_{0.0f};
  float preview_red_gain_{1.0f};
  float preview_blue_gain_{1.0f};
  std::array<uint8_t, 256> preview_tone_{};
};

}  // namespace esphome::camera_motion
