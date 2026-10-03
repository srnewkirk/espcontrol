import {
  normalizeBackupPanelSettings,
  normalizeCameraMotionSensitivity,
  normalizeScreensaverMode,
  screensaverModeForDevice,
  screensaverModeOptions,
} from "../../src/webserver/model/settings";
import { CAMERA_CONTROLS, normalizeCameraControls } from "../../src/webserver/model/camera_controls";

function equal<T>(actual: T, expected: T, message: string): void {
  if (actual !== expected) throw new Error(`${message}: expected ${String(expected)}, received ${String(actual)}`);
}

const CURRENT = {
  timezone: "UTC",
  language: "en",
  clockFormat: "24h",
  clockFormatOptions: ["24h", "12h"],
  ntpDefaults: ["0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org"],
  ntpServer1: "0.pool.ntp.org",
  ntpServer2: "1.pool.ntp.org",
  ntpServer3: "2.pool.ntp.org",
  coverArtHomeAssistantProtocol: "http",
  coverArtHomeAssistantHost: "homeassistant.local",
  coverArtHomeAssistantPort: 8123,
  coverArtHomeAssistantEndpointMode: "Automatic",
  autoUpdate: true,
  updateFrequency: "Daily",
  updateFrequencyOptions: ["Daily"],
  screenRotationOptions: ["0"],
};

export function runCameraMotionSettingsTests(): void {
  equal(normalizeCameraMotionSensitivity(""), 50, "a missing sensitivity uses the default");
  equal(normalizeCameraMotionSensitivity("abc"), 50, "an invalid sensitivity uses the default");
  equal(normalizeCameraMotionSensitivity(0), 1, "sensitivity has a minimum of 1");
  equal(normalizeCameraMotionSensitivity(250), 100, "sensitivity has a maximum of 100");
  equal(normalizeCameraMotionSensitivity("37.6"), 38, "sensitivity is a whole number");
  equal(normalizeCameraMotionSensitivity(5), 5, "low sensitivity values are kept");

  equal(normalizeScreensaverMode("camera"), "camera", "camera is a screensaver mode");
  equal(normalizeScreensaverMode("bogus"), "disabled", "unknown modes disable the screensaver");

  const plainModes = screensaverModeOptions(false).map((option) => option[0]).join(",");
  equal(plainModes, "disabled,timer,sensor", "panels without a camera do not offer Camera mode");
  const cameraModes = screensaverModeOptions(true).map((option) => option[0]).join(",");
  equal(cameraModes, "disabled,timer,sensor,camera", "camera panels offer Camera mode last");

  equal(screensaverModeForDevice("camera", true), "camera", "camera panels restore Camera mode");
  equal(screensaverModeForDevice("camera", false), "timer", "other panels restore Camera mode as Timer");
  equal(screensaverModeForDevice("sensor", false), "sensor", "other modes restore unchanged");

  const restored = normalizeBackupPanelSettings(
    { screensaver_mode: "camera", camera_motion_sensitivity: 7 }, CURRENT);
  equal(restored.screensaverMode, "camera", "backups keep Camera mode");
  equal(restored.cameraMotionSensitivity, 7, "backups keep the camera sensitivity");
  const older = normalizeBackupPanelSettings({ screensaver_mode: "timer" }, CURRENT);
  equal(older.cameraMotionSensitivity, 50, "backups without a sensitivity use the default");
  equal(older.cameraControls.camera_auto_dimming, false, "older backups must not enable uncalibrated dimming");
  const controls = normalizeCameraControls({camera_occupancy_timeout: 0, camera_wake_on_occupancy: false,
    camera_auto_dimming: "ON", camera_light_dark: "invalid", camera_dim_maximum: 200});
  equal(controls.camera_occupancy_timeout, 1, "occupancy timeout must be positive");
  equal(controls.camera_wake_on_occupancy, false, "wake can be disabled independently");
  equal(controls.camera_auto_dimming, true, "switch state from the panel enables dimming");
  equal(controls.camera_light_dark, 0.1, "invalid calibration uses its default");
  equal(controls.camera_dim_maximum, 100, "dimming cannot exceed full brightness");
  equal(normalizeCameraControls({camera_light_dark: 0.0034}).camera_light_dark, 0.0034,
    "low-light calibration retains four decimal places");
  const sensingBackup = normalizeBackupPanelSettings({camera_occupancy_timeout: 45,
    camera_wake_on_occupancy: false, camera_auto_dimming: true, camera_light_dark: 0.42,
    camera_light_bright: 4.2, camera_dim_minimum: 15, camera_dim_maximum: 80}, CURRENT);
  equal(sensingBackup.cameraControls.camera_occupancy_timeout, 45, "backup preserves occupancy timeout");
  equal(sensingBackup.cameraControls.camera_wake_on_occupancy, false, "backup preserves disabled occupancy wake");
  equal(sensingBackup.cameraControls.camera_light_bright, 4.2, "backup preserves light calibration");
  equal(CAMERA_CONTROLS.length, 7, "all occupancy and light settings are covered by backup normalization");
}
