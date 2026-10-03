import { CAMERA_CONTROLS, normalizeCameraControl } from "../model/camera_controls";
import { state } from "../state/app_instance";
import type { UiRuntimeState } from "./state";

export function syncCameraControls(runtime: UiRuntimeState): void {
  const els = runtime.els;
  for (const control of CAMERA_CONTROLS) {
    const input = els[control.key];
    if (!input) continue;
    if (control.domain === "switch") input.checked = state.cameraControls[control.key] === true;
    else input.value = String(state.cameraControls[control.key]);
  }
}

export function cameraControlHandlers(runtime: UiRuntimeState): Record<string, (value?: any, data?: any) => void> {
  const els = runtime.els;
  const handlers: Record<string, (value?: any, data?: any) => void> = {};
  for (const control of CAMERA_CONTROLS) {
    handlers[`${control.domain}-${control.objectId}`] = (value, data) => {
      state.cameraControls[control.key] = normalizeCameraControl(control, data?.value ?? value);
      syncCameraControls(runtime);
    };
  }
  handlers["binary_sensor-camera__occupancy"] = (value, data) => {
    if (els.cameraOccupancyStatus) els.cameraOccupancyStatus.textContent =
      data?.value === true || value === "ON" ? "Occupied" : "Clear";
  };
  handlers["sensor-camera_light__relative_level"] = (value) => {
    if (els.cameraLightStatus) els.cameraLightStatus.textContent =
      Number.isFinite(parseFloat(value)) ? parseFloat(value).toFixed(4) : "Unavailable";
  };
  return handlers;
}
