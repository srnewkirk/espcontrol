#include "camera_motion.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_WEBSERVER
#include "esphome/components/web_server_idf/web_server_idf.h"
#endif

namespace esphome::camera_motion {

static const char *const TAG = "camera_motion";

// OV02C10 registers (addresses and encodings match the upstream Linux driver).
static constexpr uint16_t REG_STREAM = 0x0100;
static constexpr uint16_t REG_SOFT_RESET = 0x0103;
static constexpr uint16_t REG_CHIP_ID = 0x300A;
static constexpr uint16_t REG_EXPOSURE_H = 0x3501;  // exposure in lines, 16-bit
static constexpr uint16_t REG_EXPOSURE_L = 0x3502;
static constexpr uint16_t REG_GAIN_H = 0x3508;  // analog gain x16, shifted left by 4
static constexpr uint16_t REG_GAIN_L = 0x3509;
static constexpr uint16_t REG_VTS_H = 0x380E;  // frame length in lines, 16-bit
static constexpr uint16_t REG_VTS_L = 0x380F;
static constexpr uint16_t REG_MIPI_CTRL00 = 0x4800;
static constexpr uint16_t CHIP_ID = 0x5602;
static constexpr uint32_t DEFAULT_VTS = 0x048C;

static constexpr uint32_t EXPOSURE_MARGIN = 16;

static constexpr size_t INIT_WRITES_PER_LOOP = 24;
static constexpr uint32_t SENSOR_RESET_WAIT_MS = 10;
static constexpr uint32_t WARMUP_MS = 4000;
static constexpr uint32_t STOP_SETTLE_MS = 400;
static constexpr uint32_t FRAME_TIMEOUT_MS = 4000;
static constexpr uint32_t RETRY_DELAY_MS = 30000;
static constexpr uint8_t MAX_START_FAILURES = 3;
static constexpr uint32_t MOTION_HOLD_MS = 5000;
static constexpr uint32_t CALLBACK_MIN_INTERVAL_MS = 1000;
static constexpr uint32_t PUBLISH_INTERVAL_MS = 2000;
static constexpr uint32_t FPS_WINDOW_MS = 10000;
static constexpr uint32_t SUMMARY_INTERVAL_MS = 60000;
// Keep this much PSRAM free for image cards after the two camera buffers.
static constexpr size_t PSRAM_HEADROOM_BYTES = 3 * 1024 * 1024;
static constexpr int SAMPLES_PER_CELL_SIDE = 4;
// Settings-page preview: a 24-bit BMP, refreshed at most this often while a
// browser keeps asking for it, and kept running this long after the last ask.
static constexpr int PREVIEW_ROW_BYTES = CameraMotionComponent::PREVIEW_W * 3;
static constexpr size_t PREVIEW_HEADER_BYTES = 54;
static constexpr size_t PREVIEW_BMP_BYTES =
    PREVIEW_HEADER_BYTES + static_cast<size_t>(PREVIEW_ROW_BYTES) * CameraMotionComponent::PREVIEW_H;
static constexpr uint32_t PREVIEW_INTERVAL_MS = 400;
static constexpr uint32_t PREVIEW_HOLD_MS = 8000;
static constexpr uint32_t PREVIEW_MAX_AGE_MS = 3000;
static constexpr uint8_t RAW_BLACK_LEVEL = 16;

// Frame hand-off shared with the camera DMA interrupt. It stays in internal
// RAM because the callbacks run from IRAM.
struct IsrState {
  uint8_t *buffer;
  size_t length;
  volatile bool want_frame;
  volatile bool capturing;
  volatile bool frame_ready;
  volatile uint32_t frame_count;
};
static DRAM_ATTR IsrState s_isr = {};

// Survives a crash or watchdog reset (not a power cycle), so a camera fault
// cannot turn into a restart loop.
static constexpr uint32_t GUARD_ACTIVE = 0x43414D31;
static __NOINIT_ATTR uint32_t s_active_guard;

// Called at the end of every frame to choose where the next frame goes. The
// driver's spare buffer is used unless the main loop asked for a frame.
static bool IRAM_ATTR on_get_new_trans(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *trans, void *) {
  s_isr.frame_count = s_isr.frame_count + 1;
  if (s_isr.want_frame && !s_isr.capturing && !s_isr.frame_ready) {
    trans->buffer = s_isr.buffer;
    trans->buflen = s_isr.length;
    s_isr.want_frame = false;
    s_isr.capturing = true;
  }
  return false;
}

static bool IRAM_ATTR on_trans_finished(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *trans, void *) {
  if (trans->buffer == s_isr.buffer && s_isr.capturing) {
    s_isr.capturing = false;
    s_isr.frame_ready = true;
  }
  return false;
}

static void reset_isr_state(uint8_t *buffer, size_t length) {
  s_isr.want_frame = false;
  s_isr.capturing = false;
  s_isr.frame_ready = false;
  s_isr.buffer = buffer;
  s_isr.length = length;
}

const CameraMotionComponent::ModeInfo &CameraMotionComponent::mode_info_() const {
  // Lane rates follow the upstream driver's mode table.
  static const ModeInfo MODES[] = {
      {"MIPI 2-lane 1920x1080", OV02C10_MIPI_2LANE_RAW10_1920X1080,
       sizeof(OV02C10_MIPI_2LANE_RAW10_1920X1080) / sizeof(Ov02c10Reg), 1920, 1080, 2, 408},
      {"MIPI 1-lane 1288x728", OV02C10_MIPI_1LANE_RAW10_1288X728,
       sizeof(OV02C10_MIPI_1LANE_RAW10_1288X728) / sizeof(Ov02c10Reg), 1288, 728, 1, 400},
  };
  return MODES[static_cast<uint8_t>(this->sensor_mode_)];
}

#ifdef USE_WEBSERVER
static CameraMotionComponent *preview_camera = nullptr;
#endif

void CameraMotionComponent::setup() {
  this->preview_lock_ = xSemaphoreCreateMutex();
  // Display tone curve for the preview: remove the sensor's black level and
  // apply a gamma so the linear raw data looks natural.
  for (int i = 0; i < 256; i++) {
    const float v = std::max(0, i - RAW_BLACK_LEVEL) / static_cast<float>(255 - RAW_BLACK_LEVEL);
    this->preview_tone_[i] = static_cast<uint8_t>(std::lround(std::pow(v, 1.0f / 2.2f) * 255.0f));
  }
#ifdef USE_WEBSERVER
  preview_camera = this;
#endif

  const esp_reset_reason_t reason = esp_reset_reason();
  const bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                       reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
  if (s_active_guard == GUARD_ACTIVE && crashed) {
    this->disabled_after_crash_ = true;
    ESP_LOGE(TAG, "The panel restarted unexpectedly while the camera was running. "
                  "Camera motion stays off until the panel is restarted.");
  }
  s_active_guard = 0;
  reset_isr_state(nullptr, 0);

  if (this->motion_binary_sensor_ != nullptr)
    this->motion_binary_sensor_->publish_initial_state(false);

  if (this->disabled_after_crash_) {
    this->state_ = State::FAILED;
    this->publish_status_("Stopped after unexpected restart; restart panel to retry");
    return;
  }
  this->sensor_present_ = this->probe_sensor_();
  this->publish_status_(this->sensor_present_ ? "Standby" : "Camera not found");
}

void CameraMotionComponent::dump_config() {
  const ModeInfo &mode = this->mode_info_();
  ESP_LOGCONFIG(TAG,
                "Camera motion:\n"
                "  Sensor mode: %s\n"
                "  Line sync packets: %s\n"
                "  Frame rate divider: %u\n"
                "  Camera found: %s",
                mode.name, YESNO(this->line_sync_), this->frame_rate_divider_, YESNO(this->sensor_present_));
  LOG_I2C_DEVICE(this);
}

void CameraMotionComponent::set_run_request(bool wake_armed, bool test_mode) {
  this->wake_armed_ = wake_armed;
  this->test_mode_ = test_mode;
}

void CameraMotionComponent::set_sensitivity(float sensitivity) {
  if (!std::isnan(sensitivity))
    this->sensitivity_ = std::clamp(sensitivity, 1.0f, 100.0f);
}

bool CameraMotionComponent::read_reg_(uint16_t reg, uint8_t *value) {
  return this->read_register16(reg, value, 1) == i2c::ERROR_OK;
}

bool CameraMotionComponent::write_reg_(uint16_t reg, uint8_t value) {
  for (int attempt = 0; attempt < 2; attempt++) {
    if (this->write_register16(reg, &value, 1) == i2c::ERROR_OK)
      return true;
  }
  ESP_LOGW(TAG, "Camera register 0x%04X write failed", reg);
  return false;
}

bool CameraMotionComponent::probe_sensor_() {
  uint8_t id[2] = {0, 0};
  if (this->read_register16(REG_CHIP_ID, id, sizeof(id)) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "No reply from the camera at I2C address 0x%02X", this->address_);
    return false;
  }
  const uint16_t chip = (static_cast<uint16_t>(id[0]) << 8) | id[1];
  if (chip != CHIP_ID) {
    ESP_LOGW(TAG, "Unexpected camera chip ID 0x%04X (OV02C10 is 0x%04X)", chip, CHIP_ID);
    return false;
  }
  ESP_LOGI(TAG, "Found OV02C10 camera");
  return true;
}

void CameraMotionComponent::loop() {
  const uint32_t now = millis();
  const bool want_run = this->wake_armed_ || this->test_mode_ || this->preview_active_(now);

  switch (this->state_) {
    case State::FAILED:
      return;

    case State::IDLE:
      if (want_run && (this->retry_after_ms_ == 0 || static_cast<int32_t>(now - this->retry_after_ms_) >= 0)) {
        this->retry_after_ms_ = 0;
        this->begin_start_();
      }
      return;

    case State::SENSOR_RESET_WAIT:
      if (now - this->state_since_ms_ >= SENSOR_RESET_WAIT_MS)
        this->state_ = State::SENSOR_INIT;
      return;

    case State::SENSOR_INIT: {
      if (!this->continue_sensor_init_()) {
        this->handle_start_failure_("Camera setup failed");
        return;
      }
      if (this->state_ != State::SENSOR_INIT || this->init_index_ < this->mode_info_().reg_count)
        return;
      if (!this->finish_sensor_init_()) {
        this->handle_start_failure_("Camera setup failed");
        return;
      }
      this->sensor_initialized_ = true;
      if (!want_run) {
        this->state_ = State::IDLE;
        this->publish_status_("Standby");
        return;
      }
      this->start_pipeline_();
      return;
    }

    case State::RUNNING:
      if (!want_run) {
        this->begin_stop_();
        return;
      }
      if (s_isr.frame_ready) {
        this->process_frame_();
        if (this->preview_active_(now) && now - this->preview_rendered_ms_ >= PREVIEW_INTERVAL_MS)
          this->render_preview_();
        this->last_frame_ms_ = now;
        s_isr.frame_ready = false;
        s_isr.want_frame = true;
      } else if (now - this->last_frame_ms_ > FRAME_TIMEOUT_MS) {
        ESP_LOGW(TAG, "No picture from the camera for %" PRIu32 " ms (%" PRIu32 " frame interrupts so far)",
                 now - this->last_frame_ms_, s_isr.frame_count);
        this->handle_start_failure_("No picture from camera");
        return;
      }
      if (this->motion_active_ && now - this->last_motion_ms_ > MOTION_HOLD_MS) {
        this->motion_active_ = false;
        if (this->motion_binary_sensor_ != nullptr)
          this->motion_binary_sensor_->publish_state(false);
      }
      this->publish_periodic_(now);
      return;

    case State::STOPPING:
      if (now - this->state_since_ms_ >= STOP_SETTLE_MS) {
        this->release_pipeline_();
        this->state_ = State::IDLE;
        this->publish_status_("Standby");
      }
      return;
  }
}

void CameraMotionComponent::begin_start_() {
  if (!this->sensor_present_) {
    this->sensor_present_ = this->probe_sensor_();
    if (!this->sensor_present_) {
      this->handle_start_failure_("Camera not found");
      return;
    }
  }
  if (this->sensor_initialized_) {
    this->start_pipeline_();
    return;
  }
  this->publish_status_("Setting up camera");
  this->init_index_ = 0;
  // Stop any stream left from before a restart, then reset the sensor.
  if (!this->write_reg_(REG_STREAM, 0x00) || !this->write_reg_(REG_SOFT_RESET, 0x01)) {
    this->handle_start_failure_("Camera setup failed");
    return;
  }
  this->state_ = State::SENSOR_RESET_WAIT;
  this->state_since_ms_ = millis();
}

bool CameraMotionComponent::continue_sensor_init_() {
  const ModeInfo &mode = this->mode_info_();
  size_t written = 0;
  while (this->init_index_ < mode.reg_count && written < INIT_WRITES_PER_LOOP) {
    const Ov02c10Reg &entry = mode.regs[this->init_index_++];
    if (!this->write_reg_(entry.reg, entry.val))
      return false;
    written++;
    if (entry.reg == REG_SOFT_RESET) {
      // Let the sensor come out of reset before the next write.
      this->state_ = State::SENSOR_RESET_WAIT;
      this->state_since_ms_ = millis();
      return true;
    }
  }
  return true;
}

bool CameraMotionComponent::finish_sensor_init_() {
  uint8_t vts_h = 0, vts_l = 0;
  uint32_t base_vts = DEFAULT_VTS;
  if (this->read_reg_(REG_VTS_H, &vts_h) && this->read_reg_(REG_VTS_L, &vts_l) && (vts_h | vts_l) != 0)
    base_vts = (static_cast<uint32_t>(vts_h) << 8) | vts_l;

  // A longer frame lowers the frame rate (less memory traffic) and allows
  // longer exposures in dim rooms.
  this->vts_ = std::min<uint32_t>(base_vts * std::max<uint8_t>(this->frame_rate_divider_, 1), 0xFFFF);
  if (!this->write_reg_(REG_VTS_H, this->vts_ >> 8) || !this->write_reg_(REG_VTS_L, this->vts_ & 0xFF))
    return false;
  this->exposure_max_ = this->vts_ - EXPOSURE_MARGIN;

  if (this->exposure_lines_ == 0) {
    this->exposure_lines_ = this->exposure_max_ / 4;
    this->gain_x16_ = GAIN_MIN_X16;
  }
  this->exposure_lines_ = std::clamp(this->exposure_lines_, EXPOSURE_MIN, this->exposure_max_);
  if (!this->apply_exposure_(this->exposure_lines_, this->gain_x16_))
    return false;

  ESP_LOGI(TAG, "Camera configured: %s, frame length %" PRIu32 " lines (base %" PRIu32 ")", this->mode_info_().name,
           this->vts_, base_vts);
  return true;
}

bool CameraMotionComponent::apply_exposure_(uint32_t exposure_lines, uint32_t gain_x16) {
  const uint32_t gain_reg = gain_x16 << 4;
  if (!this->write_reg_(REG_EXPOSURE_H, exposure_lines >> 8) ||
      !this->write_reg_(REG_EXPOSURE_L, exposure_lines & 0xFF) || !this->write_reg_(REG_GAIN_H, gain_reg >> 8) ||
      !this->write_reg_(REG_GAIN_L, gain_reg & 0xFF))
    return false;
  this->exposure_lines_ = exposure_lines;
  this->gain_x16_ = gain_x16;
  return true;
}

bool CameraMotionComponent::set_sensor_stream_(bool enable) {
  // Same sequence as the upstream OV02C10 driver's stream control.
  const uint8_t on = enable ? 0x01 : 0x00;
  return this->write_reg_(REG_MIPI_CTRL00, 0x64) && this->write_reg_(0x3002, on) && this->write_reg_(0x3010, on) &&
         this->write_reg_(0x300D, on) && this->write_reg_(REG_STREAM, on);
}

#define CAMERA_START_STEP(expr, what) \
  do { \
    esp_err_t err_ = (expr); \
    if (err_ != ESP_OK) { \
      ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err_)); \
      this->handle_start_failure_(what " failed"); \
      return false; \
    } \
  } while (0)

bool CameraMotionComponent::start_pipeline_() {
  const ModeInfo &mode = this->mode_info_();
  // The driver syncs the CPU cache over the whole buffer, so keep its start
  // and length on cache-line boundaries.
  constexpr size_t CACHE_ALIGN = 128;
  const size_t frame_bytes = static_cast<size_t>(mode.width) * mode.height;  // RAW8
  const size_t buffer_bytes = (frame_bytes + CACHE_ALIGN - 1) / CACHE_ALIGN * CACHE_ALIGN;

  const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  // Our frame buffer plus the driver's spare buffer, keeping headroom for image cards.
  if (psram_largest < buffer_bytes || psram_free < 2 * buffer_bytes + PSRAM_HEADROOM_BYTES) {
    ESP_LOGW(TAG, "Not enough PSRAM for the camera: %u free, %u largest block, needs 2 x %u plus %u headroom",
             static_cast<unsigned>(psram_free), static_cast<unsigned>(psram_largest),
             static_cast<unsigned>(buffer_bytes), static_cast<unsigned>(PSRAM_HEADROOM_BYTES));
    this->state_ = State::IDLE;
    this->retry_after_ms_ = millis() + RETRY_DELAY_MS;
    this->publish_status_("Waiting for memory");
    return false;
  }

  s_active_guard = GUARD_ACTIVE;
  this->frame_buffer_ = static_cast<uint8_t *>(
      heap_caps_calloc(1, buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED));
  if (this->frame_buffer_ == nullptr) {
    this->handle_start_failure_("Camera memory allocation failed");
    return false;
  }
  this->frame_buffer_size_ = buffer_bytes;

  // Same order as Espressif's esp_video and IDF camera example: receiver,
  // callbacks and enable, then the image processor, then start.
  // The MIPI power rail (LDO channel 3) is shared with the display and already on.
  esp_cam_ctlr_csi_config_t csi_config = {};
  csi_config.ctlr_id = 0;
  csi_config.clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT;
  csi_config.h_res = mode.width;
  csi_config.v_res = mode.height;
  csi_config.data_lane_num = mode.lanes;
  csi_config.lane_bit_rate_mbps = mode.lane_bit_rate_mbps;
  csi_config.input_data_color_type = CAM_CTLR_COLOR_RAW10;
  csi_config.output_data_color_type = CAM_CTLR_COLOR_RAW8;
  csi_config.queue_items = 1;
  csi_config.byte_swap_en = false;
  csi_config.bk_buffer_dis = false;
  CAMERA_START_STEP(esp_cam_new_csi_ctlr(&csi_config, &this->csi_), "Camera receiver setup");

  reset_isr_state(this->frame_buffer_, this->frame_buffer_size_);
  s_isr.want_frame = true;
  esp_cam_ctlr_evt_cbs_t callbacks = {};
  callbacks.on_get_new_trans = on_get_new_trans;
  callbacks.on_trans_finished = on_trans_finished;
  CAMERA_START_STEP(esp_cam_ctlr_register_event_callbacks(this->csi_, &callbacks, nullptr),
                    "Camera receiver callbacks");
  CAMERA_START_STEP(esp_cam_ctlr_enable(this->csi_), "Camera receiver enable");
  this->csi_enabled_ = true;

  // The image processor only converts the 10-bit sensor data to 8-bit here;
  // colour processing is not needed for motion detection.
  esp_isp_processor_cfg_t isp_config = {};
  isp_config.clk_src = ISP_CLK_SRC_DEFAULT;
  isp_config.clk_hz = 240 * 1000 * 1000;
  isp_config.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
  isp_config.input_data_color_type = ISP_COLOR_RAW10;
  isp_config.output_data_color_type = ISP_COLOR_RAW8;
  isp_config.yuv_range = ISP_COLOR_RANGE_FULL;
  isp_config.yuv_std = ISP_YUV_CONV_STD_BT601;
  isp_config.has_line_start_packet = this->line_sync_;
  isp_config.has_line_end_packet = this->line_sync_;
  isp_config.h_res = mode.width;
  isp_config.v_res = mode.height;
  isp_config.bayer_order = COLOR_RAW_ELEMENT_ORDER_GBRG;
  CAMERA_START_STEP(esp_isp_new_processor(&isp_config, &this->isp_), "Image processor setup");
  CAMERA_START_STEP(esp_isp_enable(this->isp_), "Image processor start");
  this->isp_enabled_ = true;

  CAMERA_START_STEP(esp_cam_ctlr_start(this->csi_), "Camera receiver start");
  this->csi_started_ = true;

  if (!this->set_sensor_stream_(true)) {
    this->handle_start_failure_("Camera stream start failed");
    return false;
  }

  const uint32_t now = millis();
  this->state_ = State::RUNNING;
  this->running_since_ms_ = now;
  this->last_frame_ms_ = now;
  this->have_prev_ = false;
  this->skip_compare_frames_ = 0;
  this->ae_out_of_band_frames_ = 0;
  this->window_max_level_ = 0.0f;
  this->processed_frames_ = 0;
  this->fps_window_start_ms_ = now;
  this->fps_window_frames_ = s_isr.frame_count;
  this->last_summary_ms_ = now;
  ESP_LOGI(TAG, "Camera started (%u bytes PSRAM per buffer, %u PSRAM free before start)",
           static_cast<unsigned>(buffer_bytes), static_cast<unsigned>(psram_free));
  this->publish_status_("Starting");
  return true;
}

#undef CAMERA_START_STEP

void CameraMotionComponent::release_pipeline_() {
  // Same teardown order as esp_video: image processor, then receiver.
  if (this->isp_ != nullptr) {
    if (this->isp_enabled_)
      esp_isp_disable(this->isp_);
    esp_isp_del_processor(this->isp_);
    this->isp_ = nullptr;
  }
  this->isp_enabled_ = false;
  if (this->csi_ != nullptr) {
    if (this->csi_started_)
      esp_cam_ctlr_stop(this->csi_);
    if (this->csi_enabled_)
      esp_cam_ctlr_disable(this->csi_);
    esp_cam_ctlr_del(this->csi_);
    this->csi_ = nullptr;
  }
  this->csi_started_ = false;
  this->csi_enabled_ = false;
  reset_isr_state(nullptr, 0);
  if (this->frame_buffer_ != nullptr) {
    heap_caps_free(this->frame_buffer_);
    this->frame_buffer_ = nullptr;
    this->frame_buffer_size_ = 0;
  }
  // Skip freeing the preview if a browser is still downloading it; the next
  // stop frees it instead.
  if (this->preview_lock_ != nullptr && xSemaphoreTake(this->preview_lock_, 0) == pdTRUE) {
    heap_caps_free(this->preview_bmp_);
    this->preview_bmp_ = nullptr;
    this->preview_rendered_ms_ = 0;
    xSemaphoreGive(this->preview_lock_);
  }
  s_active_guard = 0;
}

void CameraMotionComponent::begin_stop_() {
  // Stop the sensor first so the last frame can finish before the receiver stops.
  s_isr.want_frame = false;
  this->set_sensor_stream_(false);
  this->state_ = State::STOPPING;
  this->state_since_ms_ = millis();
  this->clear_outputs_();
}

void CameraMotionComponent::clear_outputs_() {
  if (this->motion_active_) {
    this->motion_active_ = false;
    if (this->motion_binary_sensor_ != nullptr)
      this->motion_binary_sensor_->publish_state(false);
  }
  if (this->motion_level_sensor_ != nullptr)
    this->motion_level_sensor_->publish_state(NAN);
  if (this->brightness_sensor_ != nullptr)
    this->brightness_sensor_->publish_state(NAN);
  this->last_mean_ = NAN;
}

void CameraMotionComponent::handle_start_failure_(const char *reason) {
  const bool was_running = this->state_ == State::RUNNING;
  if (was_running)
    this->set_sensor_stream_(false);
  this->release_pipeline_();
  this->clear_outputs_();
  // Re-run the full sensor setup on the next attempt.
  this->sensor_initialized_ = false;
  this->start_failures_++;
  if (this->start_failures_ >= MAX_START_FAILURES) {
    ESP_LOGE(TAG, "%s; giving up until the panel restarts", reason);
    this->state_ = State::FAILED;
    this->publish_status_(std::string(reason) + "; restart panel to retry");
    return;
  }
  ESP_LOGW(TAG, "%s; retrying in %" PRIu32 " s", reason, RETRY_DELAY_MS / 1000);
  this->state_ = State::IDLE;
  this->retry_after_ms_ = millis() + RETRY_DELAY_MS;
  this->publish_status_(std::string(reason) + "; retrying");
}

uint16_t CameraMotionComponent::min_changed_cells_() const {
  return min_changed_cells(this->sensitivity_, GRID_CELLS);
}

void CameraMotionComponent::process_frame_() {
  const ModeInfo &mode = this->mode_info_();
  const uint32_t now = millis();
  const int width = mode.width;
  const int cell_w = width / GRID_W;
  const int cell_h = mode.height / GRID_H;
  const int step_x = cell_w / SAMPLES_PER_CELL_SIDE;
  const int step_y = cell_h / SAMPLES_PER_CELL_SIDE;
  const uint8_t *frame = this->frame_buffer_;

  // Average a few 2x2 Bayer blocks per grid cell. Each block holds one red,
  // one blue and two green pixels, so its sum tracks overall brightness.
  uint32_t total = 0;
  for (int gy = 0; gy < GRID_H; gy++) {
    uint32_t sums[GRID_W] = {};
    for (int sy = 0; sy < SAMPLES_PER_CELL_SIDE; sy++) {
      const int y = (gy * cell_h + sy * step_y + step_y / 2) & ~1;
      const uint8_t *row0 = frame + static_cast<size_t>(y) * width;
      const uint8_t *row1 = row0 + width;
      for (int gx = 0; gx < GRID_W; gx++) {
        uint32_t sum = 0;
        for (int sx = 0; sx < SAMPLES_PER_CELL_SIDE; sx++) {
          const int x = (gx * cell_w + sx * step_x + step_x / 2) & ~1;
          sum += row0[x] + row0[x + 1] + row1[x] + row1[x + 1];
        }
        sums[gx] += sum;
      }
    }
    for (int gx = 0; gx < GRID_W; gx++) {
      const uint8_t value = sums[gx] / (SAMPLES_PER_CELL_SIDE * SAMPLES_PER_CELL_SIDE * 4);
      this->grid_[gy * GRID_W + gx] = value;
      total += value;
    }
  }
  const float mean = static_cast<float>(total) / GRID_CELLS;
  this->processed_frames_++;
  if (this->processed_frames_ == 1) {
    ESP_LOGI(TAG, "First camera picture received (brightness %.0f)", mean);
    this->start_failures_ = 0;
  }

  const bool warming_up = now - this->running_since_ms_ < WARMUP_MS;
  this->changed_cells_.reset();
  this->last_level_ = 0.0f;
  if (this->have_prev_ && this->skip_compare_frames_ == 0 && !warming_up) {
    // Higher gain means more sensor noise, so each area must change more.
    const float threshold_scale =
        std::sqrt(static_cast<float>(this->gain_x16_) / GAIN_MIN_X16) * cell_threshold_scale(this->sensitivity_);
    const uint16_t changed = count_changed_cells(this->prev_grid_, this->grid_, this->prev_mean_, mean,
                                                 threshold_scale, &this->changed_cells_);
    const float level = changed * 100.0f / GRID_CELLS;
    this->last_level_ = level;
    this->window_max_level_ = std::max(this->window_max_level_, level);
    if (changed >= this->min_changed_cells_()) {
      if (!this->motion_active_) {
        ESP_LOGI(TAG, "Motion: %u of %d areas changed (%.1f%%), brightness %.0f, exposure %" PRIu32 "/%" PRIu32
                      ", gain %.1fx",
                 changed, GRID_CELLS, level, mean, this->exposure_lines_, this->exposure_max_,
                 this->gain_x16_ / 16.0f);
        this->motion_active_ = true;
        if (this->motion_binary_sensor_ != nullptr)
          this->motion_binary_sensor_->publish_state(true);
      }
      this->last_motion_ms_ = now;
      if (now - this->last_callback_ms_ >= CALLBACK_MIN_INTERVAL_MS) {
        this->last_callback_ms_ = now;
        this->motion_callback_.call();
      }
    }
  }
  if (this->skip_compare_frames_ > 0)
    this->skip_compare_frames_--;

  this->prev_grid_ = this->grid_;
  this->prev_mean_ = mean;
  this->have_prev_ = true;
  this->last_mean_ = mean;

  if (this->picture_log_requested_) {
    this->picture_log_requested_ = false;
    this->log_picture_(mean);
  }
  this->run_auto_exposure_(mean);
}

void CameraMotionComponent::run_auto_exposure_(float mean) {
  const bool warming_up = millis() - this->running_since_ms_ < WARMUP_MS;
  if (exposure_in_band(mean)) {
    this->ae_out_of_band_frames_ = 0;
    return;
  }
  // Outside warm-up, ignore a single odd frame (someone walking past).
  if (!warming_up && ++this->ae_out_of_band_frames_ < 2)
    return;
  this->ae_out_of_band_frames_ = 0;

  const ExposureSetting next =
      next_exposure(mean, {this->exposure_lines_, this->gain_x16_}, this->exposure_max_, warming_up);
  if (next.exposure_lines == this->exposure_lines_ && next.gain_x16 == this->gain_x16_)
    return;  // Already at a limit.
  if (this->apply_exposure_(next.exposure_lines, next.gain_x16)) {
    // New settings take a frame or two to show; do not compare across them.
    this->skip_compare_frames_ = 2;
  }
}

void CameraMotionComponent::publish_periodic_(uint32_t now) {
  if (now - this->fps_window_start_ms_ >= FPS_WINDOW_MS) {
    const uint32_t frames = s_isr.frame_count - this->fps_window_frames_;
    this->measured_fps_ = frames * 1000.0f / (now - this->fps_window_start_ms_);
    this->fps_window_start_ms_ = now;
    this->fps_window_frames_ = s_isr.frame_count;
  }

  if (now - this->last_publish_ms_ >= PUBLISH_INTERVAL_MS) {
    this->last_publish_ms_ = now;
    if (this->motion_level_sensor_ != nullptr)
      this->motion_level_sensor_->publish_state(this->window_max_level_);
    this->window_max_level_ = 0.0f;
    if (this->brightness_sensor_ != nullptr && !std::isnan(this->last_mean_))
      this->brightness_sensor_->publish_state(this->last_mean_ * 100.0f / 255.0f);

    std::string status;
    if (this->processed_frames_ == 0 || now - this->running_since_ms_ < WARMUP_MS) {
      status = "Starting";
    } else {
      if (this->test_mode_) {
        status = "Test mode";
      } else if (this->wake_armed_) {
        status = "Watching for motion";
      } else {
        status = "Preview";
      }
      if (this->exposure_lines_ >= this->exposure_max_ && this->gain_x16_ >= GAIN_MAX_X16 &&
          this->last_mean_ < AE_LOW / 2)
        status += " (too dark to see well)";
    }
    this->publish_status_(status);
  }

  if (now - this->last_summary_ms_ >= SUMMARY_INTERVAL_MS) {
    this->last_summary_ms_ = now;
    ESP_LOGI(TAG,
             "Camera running: %.1f frames/s, %" PRIu32 " pictures checked, brightness %.0f, exposure %" PRIu32
             "/%" PRIu32 ", gain %.1fx, sensitivity %.0f needs %u areas, PSRAM free %u",
             this->measured_fps_, this->processed_frames_, this->last_mean_, this->exposure_lines_,
             this->exposure_max_, this->gain_x16_ / 16.0f, this->sensitivity_, this->min_changed_cells_(),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  }
}

void CameraMotionComponent::log_picture_(float mean) const {
  static const char RAMP[] = " .:-=+*#%@";
  const auto [lo_it, hi_it] = std::minmax_element(this->grid_.begin(), this->grid_.end());
  const int lo = *lo_it;
  const int span = std::max(1, *hi_it - lo);
  ESP_LOGI(TAG, "Camera picture (%dx%d areas, brightness %.0f, range %d-%d, exposure %" PRIu32 "/%" PRIu32
                ", gain %.1fx):",
           GRID_W, GRID_H, mean, lo, static_cast<int>(*hi_it), this->exposure_lines_, this->exposure_max_,
           this->gain_x16_ / 16.0f);
  char line[GRID_W * 2 + 1];
  for (int y = 0; y < GRID_H; y++) {
    for (int x = 0; x < GRID_W; x++) {
      const char c = RAMP[(this->grid_[y * GRID_W + x] - lo) * 9 / span];
      line[x * 2] = c;
      line[x * 2 + 1] = c;
    }
    line[GRID_W * 2] = '\0';
    ESP_LOGI(TAG, "|%s|", line);
  }
}

void CameraMotionComponent::request_preview() { this->preview_until_ms_.store(millis() + PREVIEW_HOLD_MS); }

bool CameraMotionComponent::preview_active_(uint32_t now) {
  uint32_t until = this->preview_until_ms_.load();
  if (until == 0)
    return false;
  if (static_cast<int32_t>(until - now) > 0)
    return true;
  this->preview_until_ms_.compare_exchange_strong(until, 0);
  return false;
}

bool CameraMotionComponent::with_preview(
    const std::function<void(const uint8_t *, size_t, const PreviewInfo &)> &send) {
  if (this->preview_lock_ == nullptr || xSemaphoreTake(this->preview_lock_, pdMS_TO_TICKS(1000)) != pdTRUE)
    return false;
  const bool ready = this->preview_bmp_ != nullptr && this->preview_rendered_ms_ != 0 &&
                     millis() - this->preview_rendered_ms_ <= PREVIEW_MAX_AGE_MS;
  if (ready)
    send(this->preview_bmp_, PREVIEW_BMP_BYTES, this->preview_info_);
  xSemaphoreGive(this->preview_lock_);
  return ready;
}

static void put_le16(uint8_t *p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = v >> 8;
}

static void put_le32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++)
    p[i] = (v >> (8 * i)) & 0xFF;
}

void CameraMotionComponent::render_preview_() {
  // A browser may still be downloading the previous picture; try next frame.
  if (xSemaphoreTake(this->preview_lock_, 0) != pdTRUE)
    return;
  if (this->preview_bmp_ == nullptr) {
    this->preview_bmp_ = static_cast<uint8_t *>(heap_caps_malloc(PREVIEW_BMP_BYTES, MALLOC_CAP_SPIRAM));
    if (this->preview_bmp_ == nullptr) {
      xSemaphoreGive(this->preview_lock_);
      return;
    }
  }
  uint8_t *bmp = this->preview_bmp_;
  std::memset(bmp, 0, PREVIEW_HEADER_BYTES);
  bmp[0] = 'B';
  bmp[1] = 'M';
  put_le32(bmp + 2, PREVIEW_BMP_BYTES);
  put_le32(bmp + 10, PREVIEW_HEADER_BYTES);
  put_le32(bmp + 14, 40);
  put_le32(bmp + 18, PREVIEW_W);
  put_le32(bmp + 22, PREVIEW_H);  // positive height: rows stored bottom-up
  put_le16(bmp + 26, 1);
  put_le16(bmp + 28, 24);
  put_le32(bmp + 34, PREVIEW_BMP_BYTES - PREVIEW_HEADER_BYTES);

  // One 2x2 Bayer block (G B / R G) per preview pixel, with a grey-world
  // white balance carried over from the previous preview.
  const ModeInfo &mode = this->mode_info_();
  const int width = mode.width;
  const int step_x = (width / PREVIEW_W) & ~1;
  const int step_y = (mode.height / PREVIEW_H) & ~1;
  uint32_t red_sum = 0, green_sum = 0, blue_sum = 0;
  for (int oy = 0; oy < PREVIEW_H; oy++) {
    const uint8_t *row0 = this->frame_buffer_ + static_cast<size_t>(oy * step_y) * width;
    const uint8_t *row1 = row0 + width;
    uint8_t *out = bmp + PREVIEW_HEADER_BYTES + static_cast<size_t>(PREVIEW_H - 1 - oy) * PREVIEW_ROW_BYTES;
    for (int ox = 0; ox < PREVIEW_W; ox++) {
      const int x = ox * step_x;
      const int green = (row0[x] + row1[x + 1]) / 2;
      const int blue = row0[x + 1];
      const int red = row1[x];
      red_sum += red;
      green_sum += green;
      blue_sum += blue;
      out[ox * 3 + 0] = this->preview_tone_[std::min(255, static_cast<int>(blue * this->preview_blue_gain_))];
      out[ox * 3 + 1] = this->preview_tone_[green];
      out[ox * 3 + 2] = this->preview_tone_[std::min(255, static_cast<int>(red * this->preview_red_gain_))];
    }
  }
  if (red_sum > 0 && blue_sum > 0) {
    const float red_gain = std::clamp(static_cast<float>(green_sum) / red_sum, 0.25f, 4.0f);
    const float blue_gain = std::clamp(static_cast<float>(green_sum) / blue_sum, 0.25f, 4.0f);
    this->preview_red_gain_ = this->preview_red_gain_ * 0.7f + red_gain * 0.3f;
    this->preview_blue_gain_ = this->preview_blue_gain_ * 0.7f + blue_gain * 0.3f;
  }

  // Outline the grid areas that changed since the previous checked picture.
  constexpr int CELL_W = PREVIEW_W / GRID_W;
  constexpr int CELL_H = PREVIEW_H / GRID_H;
  auto mark = [bmp](int x, int y) {
    uint8_t *px = bmp + PREVIEW_HEADER_BYTES + static_cast<size_t>(PREVIEW_H - 1 - y) * PREVIEW_ROW_BYTES + x * 3;
    px[0] = 0;
    px[1] = 0;
    px[2] = 255;
  };
  for (int i = 0; i < GRID_CELLS; i++) {
    if (!this->changed_cells_.test(i))
      continue;
    const int x0 = (i % GRID_W) * CELL_W;
    const int y0 = (i / GRID_W) * CELL_H;
    for (int d = 0; d < CELL_W; d++) {
      mark(x0 + d, y0);
      mark(x0 + d, y0 + CELL_H - 1);
    }
    for (int d = 0; d < CELL_H; d++) {
      mark(x0, y0 + d);
      mark(x0 + CELL_W - 1, y0 + d);
    }
  }

  this->preview_info_.motion_level = this->last_level_;
  this->preview_info_.brightness = std::isnan(this->last_mean_) ? 0.0f : this->last_mean_ * 100.0f / 255.0f;
  this->preview_info_.motion = this->motion_active_;
  this->preview_rendered_ms_ = millis();
  xSemaphoreGive(this->preview_lock_);
}

void CameraMotionComponent::publish_status_(const std::string &status) {
  if (status == this->last_status_)
    return;
  this->last_status_ = status;
  ESP_LOGI(TAG, "Status: %s", status.c_str());
  if (this->status_text_sensor_ != nullptr)
    this->status_text_sensor_->publish_state(status);
}

}  // namespace esphome::camera_motion

#ifdef USE_WEBSERVER
// Provided by the espcontrol component: applies the panel's optional web login.
extern "C" bool espcontrol_authenticate_web_request(esphome::web_server_idf::AsyncWebServerRequest *request)
    __attribute__((weak));

namespace esphome::camera_motion {

static void send_json(httpd_req_t *raw, const char *status, const char *body) {
  httpd_resp_set_status(raw, status);
  httpd_resp_set_type(raw, "application/json");
  httpd_resp_set_hdr(raw, "Cache-Control", "no-store");
  httpd_resp_send(raw, body, HTTPD_RESP_USE_STRLEN);
}

// GET /api/v1/camera/preview returns the latest preview picture, starting the
// camera for a few seconds if needed. 202 means it is still starting.
class CameraPreviewHandler final : public web_server_idf::AsyncWebHandler {
 public:
  bool canHandle(web_server_idf::AsyncWebServerRequest *request) const override {
    char path[web_server_idf::AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_GET && request->url_to(path) == "/api/v1/camera/preview";
  }
  void handleRequest(web_server_idf::AsyncWebServerRequest *request) override {
    httpd_req_t *raw = *request;
#ifdef USE_WEBSERVER_AUTH
    if (espcontrol_authenticate_web_request == nullptr) {
      send_json(raw, "403 Forbidden", "{\"error\":\"Preview is unavailable with this web login\"}");
      return;
    }
#endif
    if (espcontrol_authenticate_web_request != nullptr && !espcontrol_authenticate_web_request(request))
      return;
    // Only the settings page sends this header, so a link or image on another
    // site cannot load the camera picture.
    if (request->get_header("X-EspControl-Request").value_or("") != "camera-preview") {
      send_json(raw, "403 Forbidden", "{\"error\":\"Open the preview from the settings page\"}");
      return;
    }
    if (preview_camera == nullptr) {
      send_json(raw, "404 Not Found", "{\"error\":\"No camera\"}");
      return;
    }
    preview_camera->request_preview();
    const bool sent = preview_camera->with_preview([raw](const uint8_t *data, size_t size, const PreviewInfo &info) {
      char level[16];
      char brightness[16];
      std::snprintf(level, sizeof(level), "%.1f", info.motion_level);
      std::snprintf(brightness, sizeof(brightness), "%.0f", info.brightness);
      httpd_resp_set_type(raw, "image/bmp");
      httpd_resp_set_hdr(raw, "Cache-Control", "no-store");
      httpd_resp_set_hdr(raw, "X-Camera-Motion-Level", level);
      httpd_resp_set_hdr(raw, "X-Camera-Brightness", brightness);
      httpd_resp_set_hdr(raw, "X-Camera-Motion", info.motion ? "1" : "0");
      httpd_resp_send(raw, reinterpret_cast<const char *>(data), size);
    });
    if (!sent)
      send_json(raw, "202 Accepted", "{\"status\":\"starting\"}");
  }
};

}  // namespace esphome::camera_motion

extern "C" void camera_motion_register_web_handlers(esphome::web_server_idf::AsyncWebServer *server) {
  static bool registered = false;
  if (!registered) {
    server->addHandler(new esphome::camera_motion::CameraPreviewHandler());
    registered = true;
  }
}
#endif
