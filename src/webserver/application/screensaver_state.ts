import { state } from "../state/app_instance";
export function getActiveScreensaverMode() {
        if (state.screensaverMode === "sensor")
            return "sensor";
        if (state.screensaverMode === "timer")
            return "timer";
        if (state.screensaverMode === "camera")
            return "camera";
        return "disabled";
}
