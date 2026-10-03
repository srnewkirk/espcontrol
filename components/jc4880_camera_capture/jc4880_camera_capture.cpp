// CSI/ISP lifecycle informed by Apache-2.0 HomeTiles; see PROVENANCE.md.
#include "jc4880_camera_capture.h"
#include "capture_grid.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <esp_cam_ctlr_csi.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstring>

namespace esphome::jc4880_camera_capture {
static const char *const TAG = "onboard_capture";

void Capture::setup() {
  // Leave a stock UI/network interval before acquiring the camera resources.
  last_loop_ = millis();
  set_timeout("camera_start", 30000, [this]() { start_(); });
}

bool Capture::next_frame_(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *trans, void *arg) {
  auto *self = static_cast<Capture *>(arg);
  portENTER_CRITICAL_ISR(&self->lock_);
  int next = self->frozen_ >= 0 ? 1 - self->frozen_ : (self->inflight_ < 0 ? 0 : 1 - self->inflight_);
  self->inflight_ = next;
  trans->buffer = self->buffers_[next];
  trans->buflen = FRAME_BYTES;
  portEXIT_CRITICAL_ISR(&self->lock_);
  return false;
}

bool Capture::finished_frame_(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *trans, void *arg) {
  auto *self = static_cast<Capture *>(arg);
  portENTER_CRITICAL_ISR(&self->lock_);
  if (trans->received_size != 0 && trans->received_size < FRAME_BYTES) {
    self->incomplete_++;
  } else {
    self->received_++;
    if (self->armed_) {
      self->frozen_ = trans->buffer == self->buffers_[0] ? 0 : 1;
      self->armed_ = false;
    }
  }
  portEXIT_CRITICAL_ISR(&self->lock_);
  return false;
}

void Capture::fail_(const char *step, esp_err_t error) {
  ESP_LOGE(TAG, "%s failed: %s; capture stopped", step, esp_err_to_name(error));
  cleanup_();
  mark_failed();
}

void Capture::start_() {
  ESP_LOGI(TAG, "Before capture: internal=%u largest=%u PSRAM=%u",
      (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  i2c_master_bus_handle_t handle = nullptr;
  esp_err_t err = i2c_master_get_bus_handle(bus_->get_port(), &handle);
  if (err != ESP_OK) return fail_("Existing I2C bus", err);
  err = sensor_.attach(handle);
  if (err != ESP_OK) return fail_("SCCB attach", err);
  uint16_t chip = 0;
  err = sensor_.probe(&chip);
  ESP_LOGI(TAG, "Sensor ID: 0x%04x", chip);
  if (err != ESP_OK) return fail_("OV02C10 probe", err);
  err = sensor_.loadDefaultMode(true);
  if (err != ESP_OK) return fail_("Sensor mode", err);
  err = sensor_.setExposure(exposure_lines_, gain_x16_);
  if (err != ESP_OK) return fail_("Fixed exposure", err);

  // LDO3 is already held at 2.5 V by the unchanged display package.
  esp_cam_ctlr_csi_config_t csi = {};
  csi.ctlr_id = 0;
  csi.clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT;
  csi.h_res = 1280;
  csi.v_res = 720;
  csi.data_lane_num = 1;
  csi.lane_bit_rate_mbps = 400;
  csi.input_data_color_type = CAM_CTLR_COLOR_RAW10;
  csi.output_data_color_type = CAM_CTLR_COLOR_RGB565;
  csi.queue_items = 1;
  csi.bk_buffer_dis = 1;
  err = esp_cam_new_csi_ctlr(&csi, &csi_);
  if (err != ESP_OK) return fail_("CSI create", err);
  for (auto &buffer : buffers_) {
    buffer = static_cast<uint8_t *>(heap_caps_aligned_calloc(128, 1, FRAME_BYTES, MALLOC_CAP_SPIRAM));
    if (!buffer) return fail_("PSRAM frame allocation", ESP_ERR_NO_MEM);
    err = esp_cache_msync(buffer, FRAME_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (err != ESP_OK) return fail_("Frame cache initialization", err);
  }
  esp_cam_ctlr_evt_cbs_t callbacks = {};
  callbacks.on_get_new_trans = next_frame_;
  callbacks.on_trans_finished = finished_frame_;
  err = esp_cam_ctlr_register_event_callbacks(csi_, &callbacks, this);
  if (err != ESP_OK) return fail_("CSI callbacks", err);
  err = esp_cam_ctlr_enable(csi_);
  if (err != ESP_OK) return fail_("CSI enable", err);
  csi_enabled_ = true;

  esp_isp_processor_cfg_t isp = {};
  isp.clk_src = ISP_CLK_SRC_DEFAULT;
  isp.clk_hz = 80000000;
  isp.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
  isp.input_data_color_type = ISP_COLOR_RAW10;
  isp.output_data_color_type = ISP_COLOR_RGB565;
  isp.yuv_range = ISP_COLOR_RANGE_FULL;
  isp.yuv_std = ISP_YUV_CONV_STD_BT601;
  isp.has_line_start_packet = true;
  isp.has_line_end_packet = true;
  isp.h_res = 1280;
  isp.v_res = 720;
  isp.bayer_order = COLOR_RAW_ELEMENT_ORDER_GBRG;
  err = esp_isp_new_processor(&isp, &isp_);
  if (err != ESP_OK) return fail_("ISP create", err);
  err = esp_isp_enable(isp_);
  if (err != ESP_OK) return fail_("ISP enable", err);
  isp_enabled_ = true;
  esp_isp_demosaic_config_t demosaic = {};
  demosaic.grad_ratio.integer = 1;
  demosaic.grad_ratio.decimal = 8;
  demosaic.padding_mode = ISP_DEMOSAIC_EDGE_PADDING_MODE_SRND_DATA;
  err = esp_isp_demosaic_configure(isp_, &demosaic);
  if (err == ESP_OK) err = esp_isp_demosaic_enable(isp_);
  if (err != ESP_OK) return fail_("Demosaic", err);
  err = esp_cam_ctlr_start(csi_);
  if (err != ESP_OK) return fail_("CSI start", err);
  started_ = true;
  err = sensor_.setStream(true);
  if (err != ESP_OK) return fail_("Sensor stream", err);
  last_arm_ = last_report_ = last_frame_ = millis();
  portENTER_CRITICAL(&lock_);
  armed_ = true;
  portEXIT_CRITICAL(&lock_);
  ESP_LOGI(TAG, "Capture started: RAW10 1280x720, 1 lane; RGB565, 2 PSRAM buffers (%u bytes); grid 160x90 at 2 Hz; fixed sensor exposure/gain", (unsigned) (2 * FRAME_BYTES));
}

void Capture::process_(int index) {
  const int64_t start = esp_timer_get_time();
  esp_err_t err = esp_cache_msync(buffers_[index], FRAME_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  if (err != ESP_OK) return fail_("DMA frame cache sync", err);
  auto *rgb = reinterpret_cast<const uint16_t *>(buffers_[index]);
  uint32_t hash = 2166136261u;
  uint8_t low = 255, high = 0;
  // Centre-sample each 8x8 cell. This is a capture grid, not a brightness metric.
  for (unsigned y = 0; y < 90; y++) {
    for (unsigned x = 0; x < 160; x++) {
      uint16_t pixel = rgb[(y * 8 + 4) * 1280 + x * 8 + 4];
      uint8_t luma = rgb565_luma(pixel);
      grid_[y * 160 + x] = luma;
      low = std::min(low, luma);
      high = std::max(high, luma);
      hash = (hash ^ luma) * 16777619u;
    }
  }
  checksum_ = hash;
  if (preview_requested_) {
    preview_requested_ = false;
    std::memcpy(preview_, grid_, sizeof(grid_));
    preview_offset_ = 0;
    preview_id_ = millis();
    preview_last_emit_ = millis();
    ESP_LOGI(TAG, "Preview begin id=%u width=160 height=90 bytes=%u checksum=%08x exposure_lines=%u gain_x16=%u",
        (unsigned) preview_id_, (unsigned) sizeof(grid_), (unsigned) checksum_,
        (unsigned) exposure_lines_, (unsigned) gain_x16_);
  }
  sampled_++;
  uint32_t elapsed = esp_timer_get_time() - start;
  process_us_ += elapsed;
  max_process_us_ = std::max(max_process_us_, elapsed);
  last_frame_ = millis();
  if (sampled_ == 1) ESP_LOGI(TAG, "First frame captured: 160x90 grid, checksum=%08x, range=%u..%u, conversion=%u us", (unsigned) checksum_, (unsigned) low, (unsigned) high, (unsigned) elapsed);
  portENTER_CRITICAL(&lock_);
  frozen_ = -1;
  portEXIT_CRITICAL(&lock_);
}

void Capture::request_preview() {
  if (!started_ || is_failed()) {
    ESP_LOGW(TAG, "Preview unavailable: camera not running");
    return;
  }
  if (preview_ != nullptr) {
    ESP_LOGW(TAG, "Preview already pending; wait for completion");
    return;
  }
  preview_ = static_cast<uint8_t *>(heap_caps_malloc(sizeof(grid_), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!preview_) {
    ESP_LOGW(TAG, "Preview unavailable: snapshot allocation failed");
    return;
  }
  preview_requested_ = true;
}

bool Capture::set_exposure(uint16_t lines, uint16_t gain_x16) {
  lines = std::clamp(lines, ov02c10::kMinExposureLines, ov02c10::kMaxExposureLines);
  gain_x16 = std::clamp(gain_x16, ov02c10::kMinGainX16, ov02c10::kMaxTotalGainX16);
  if (started_) {
    if (preview_) {
      ESP_LOGW(TAG, "Wait for preview completion before changing exposure");
      return false;
    }
    esp_err_t err = sensor_.setExposure(lines, gain_x16);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Exposure change failed: %s", esp_err_to_name(err));
      return false;
    }
  }
  exposure_lines_ = lines;
  gain_x16_ = gain_x16;
  ESP_LOGI(TAG, "Fixed exposure requested: lines=%u gain_x16=%u", (unsigned) lines, (unsigned) gain_x16);
  return true;
}

void Capture::emit_preview_() {
  if (!preview_ || preview_requested_ || millis() - preview_last_emit_ < 20) return;
  // Keep each log line below the logger buffer; emit one chunk per loop.
  // The camera continues using its existing buffers while this copy is read.
  constexpr size_t CHUNK = 128;
  constexpr char HEX[] = "0123456789abcdef";
  char encoded[CHUNK * 2 + 1];
  size_t count = std::min(CHUNK, sizeof(grid_) - preview_offset_);
  for (size_t i = 0; i < count; ++i) {
    uint8_t value = preview_[preview_offset_ + i];
    encoded[i * 2] = HEX[value >> 4];
    encoded[i * 2 + 1] = HEX[value & 15];
  }
  encoded[count * 2] = '\0';
  ESP_LOGI(TAG, "Preview chunk id=%u offset=%u data=%s", (unsigned) preview_id_,
      (unsigned) preview_offset_, encoded);
  preview_offset_ += count;
  preview_last_emit_ = millis();
  if (preview_offset_ == sizeof(grid_)) {
    ESP_LOGI(TAG, "Preview end id=%u", (unsigned) preview_id_);
    heap_caps_free(preview_);
    preview_ = nullptr;
  }
}

void Capture::loop() {
  uint32_t now = millis();
  max_loop_gap_ = std::max(max_loop_gap_, now - last_loop_);
  last_loop_ = now;
  if (!started_) return;
  int frozen;
  portENTER_CRITICAL(&lock_);
  frozen = frozen_;
  portEXIT_CRITICAL(&lock_);
  if (frozen >= 0) process_(frozen);
  if (is_failed()) return;
  // process_ updates last_frame_. Refresh now so unsigned subtraction cannot
  // mistake its few milliseconds of conversion time for a wrapped timeout.
  now = millis();
  if (frame_timed_out(now, last_frame_)) return fail_("Frame timeout", ESP_ERR_TIMEOUT);
  if (now - last_arm_ >= 500) {
    portENTER_CRITICAL(&lock_);
    if (frozen_ < 0) armed_ = true;
    portEXIT_CRITICAL(&lock_);
    last_arm_ = now;
  }
  emit_preview_();
  if (now - last_report_ >= 10000) {
    uint32_t received, incomplete;
    portENTER_CRITICAL(&lock_);
    received = received_;
    incomplete = incomplete_;
    portEXIT_CRITICAL(&lock_);
    ESP_LOGI(TAG, "Health: uptime=%u s CSI=%u sampled=%u incomplete=%u checksum=%08x conversion_total=%llu us max=%u us loop_gap=%u ms internal=%u largest=%u PSRAM=%u",
        (unsigned) (now / 1000), (unsigned) received, (unsigned) sampled_, (unsigned) incomplete, (unsigned) checksum_,
        (unsigned long long) process_us_, (unsigned) max_process_us_, (unsigned) max_loop_gap_,
        (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    last_report_ = now;
    max_loop_gap_ = 0;
  }
}

void Capture::cleanup_() {
  cancel_timeout("camera_start");
  heap_caps_free(preview_);
  preview_ = nullptr;
  preview_requested_ = false;
  if (sensor_.attached()) sensor_.setStream(false);
  if (started_ && esp_cam_ctlr_stop(csi_) != ESP_OK) {
    // Retain buffers if DMA cannot be stopped; never free live DMA memory.
    ESP_LOGE(TAG, "CSI stop failed; retaining camera resources until reboot");
    return;
  }
  started_ = false;
  if (isp_enabled_) esp_isp_disable(isp_);
  isp_enabled_ = false;
  if (isp_) esp_isp_del_processor(isp_);
  isp_ = nullptr;
  if (csi_enabled_) esp_cam_ctlr_disable(csi_);
  csi_enabled_ = false;
  if (csi_) esp_cam_ctlr_del(csi_);
  csi_ = nullptr;
  for (auto &buffer : buffers_) { heap_caps_free(buffer); buffer = nullptr; }
  sensor_.detach();
}
void Capture::on_shutdown() { cleanup_(); }
void Capture::dump_config() { ESP_LOGCONFIG(TAG, "JC4880 Stage 2 onboard capture: shared I2C, fixed exposure, 2 Hz grid"); }
}  // namespace esphome::jc4880_camera_capture
