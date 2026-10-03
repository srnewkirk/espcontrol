export interface CameraControl {
  key: string;
  name: string;
  objectId: string;
  domain: "number" | "switch";
  label: string;
  defaultValue: number | boolean;
  min?: number;
  max?: number;
  step?: number;
}

export const CAMERA_CONTROLS: readonly CameraControl[] = [
  { key: "camera_occupancy_timeout", name: "Camera: Occupancy Timeout", objectId: "camera__occupancy_timeout", domain: "number", label: "Occupancy Timeout (seconds)", defaultValue: 120, min: 1, max: 3600, step: 1 },
  { key: "camera_wake_on_occupancy", name: "Camera: Wake Screen On Occupancy", objectId: "camera__wake_screen_on_occupancy", domain: "switch", label: "Wake Screen On Occupancy", defaultValue: true },
  { key: "camera_auto_dimming", name: "Camera Light: Automatic Dimming", objectId: "camera_light__automatic_dimming", domain: "switch", label: "Automatic Light Dimming", defaultValue: false },
  { key: "camera_light_dark", name: "Camera Light: Dark Level", objectId: "camera_light__dark_level", domain: "number", label: "Dark Room Light Level", defaultValue: 0.1, min: 0, max: 10000, step: 0.0001 },
  { key: "camera_light_bright", name: "Camera Light: Bright Level", objectId: "camera_light__bright_level", domain: "number", label: "Bright Room Light Level", defaultValue: 10, min: 0.0001, max: 10000, step: 0.0001 },
  { key: "camera_dim_minimum", name: "Camera Light: Minimum Brightness", objectId: "camera_light__minimum_brightness", domain: "number", label: "Minimum Screen Brightness (%)", defaultValue: 10, min: 1, max: 100, step: 1 },
  { key: "camera_dim_maximum", name: "Camera Light: Maximum Brightness", objectId: "camera_light__maximum_brightness", domain: "number", label: "Maximum Screen Brightness (%)", defaultValue: 100, min: 1, max: 100, step: 1 },
];

export function normalizeCameraControl(control: CameraControl, value: unknown): number | boolean {
  if (control.domain === "switch") {
    if (value == null) return control.defaultValue;
    return value === true || value === "ON" || value === "true";
  }
  const number = Number(value);
  if (value == null || value === "" || !Number.isFinite(number)) return control.defaultValue;
  const clamped = Math.max(control.min!, Math.min(control.max!, number));
  return Number((Math.round(clamped / control.step!) * control.step!).toFixed(4));
}

export function normalizeCameraControls(values: Record<string, unknown>): Record<string, number | boolean> {
  return Object.fromEntries(CAMERA_CONTROLS.map((control) => [control.key, normalizeCameraControl(control, values[control.key])]));
}
