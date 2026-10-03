#pragma once
#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c_bus.h"
#include "ov02c10_sensor.h"
#include <esp_cam_ctlr.h>
#include <driver/isp.h>
#include <freertos/FreeRTOS.h>
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif

namespace esphome::jc4880_camera_capture {
class Capture : public Component {
 public:
  void set_bus(i2c::InternalI2CBus *bus) { bus_ = bus; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  void request_preview();
  bool set_exposure(uint16_t lines, uint16_t gain_x16);
  uint16_t exposure_lines() const { return exposure_lines_; }
  uint16_t gain_x16() const { return gain_x16_; }
  float get_setup_priority() const override { return setup_priority::LATE; }
 protected:
  static constexpr size_t FRAME_BYTES = 1280 * 720 * 2;
  static bool next_frame_(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *, void *);
  static bool finished_frame_(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *, void *);
  void start_();
  void cleanup_();
  void fail_(const char *step, esp_err_t error);
  void process_(int index);
  void emit_preview_();
  i2c::InternalI2CBus *bus_{nullptr};
  ov02c10::Sensor sensor_;
  esp_cam_ctlr_handle_t csi_{nullptr};
  isp_proc_handle_t isp_{nullptr};
  uint8_t *buffers_[2]{};
  uint8_t grid_[160 * 90]{};
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  volatile int frozen_{-1};
  int inflight_{-1};
  volatile bool armed_{false};
  volatile uint32_t received_{0}, incomplete_{0};
  bool csi_enabled_{false}, isp_enabled_{false}, started_{false};
  uint32_t sampled_{0}, last_arm_{0}, last_report_{0}, last_frame_{0};
  uint32_t last_loop_{0}, max_loop_gap_{0}, max_process_us_{0};
  uint64_t process_us_{0};
  uint32_t checksum_{0};
  uint8_t *preview_{nullptr};
  bool preview_requested_{false};
  uint32_t preview_id_{0}, preview_last_emit_{0};
  size_t preview_offset_{0};
  uint16_t exposure_lines_{ov02c10::kTableExposureLines};
  uint16_t gain_x16_{ov02c10::kTableAnalogGainReg >> 4};
};
#ifdef USE_BUTTON
class PreviewButton : public button::Button {
 public:
  explicit PreviewButton(Capture *capture) : capture_(capture) {}
 protected:
  void press_action() override { capture_->request_preview(); }
  Capture *capture_;
};
#endif
}  // namespace esphome::jc4880_camera_capture
