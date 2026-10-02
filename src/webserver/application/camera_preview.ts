// Live preview of the built-in camera for the Camera screensaver mode. The
// panel only runs the camera for the preview while this page keeps asking.

const PREVIEW_URL = "/api/v1/camera/preview";
const REFRESH_MS = 700;

export interface CameraPreviewControls {
    readonly element: HTMLElement;
    stop(): void;
}

export function createCameraPreview(document: Document, fetchPreview: typeof fetch): CameraPreviewControls {
    const wrap = document.createElement("div");
    wrap.className = "sp-field";
    const button = document.createElement("button");
    button.type = "button";
    button.className = "sp-secondary-btn";
    button.textContent = "Show Camera Preview";
    const frame = document.createElement("div");
    frame.className = "sp-camera-preview";
    frame.style.display = "none";
    const image = document.createElement("img");
    image.alt = "Camera preview";
    const status = document.createElement("div");
    status.className = "sp-field-hint";
    frame.appendChild(image);
    frame.appendChild(status);
    wrap.appendChild(button);
    wrap.appendChild(frame);

    let running = false;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let objectUrl = "";

    function stop(): void {
        running = false;
        if (timer !== undefined) clearTimeout(timer);
        timer = undefined;
        button.textContent = "Show Camera Preview";
        frame.style.display = "none";
        image.removeAttribute("src");
        if (objectUrl) URL.revokeObjectURL(objectUrl);
        objectUrl = "";
    }

    function showPicture(blob: Blob, headers: Headers): void {
        const next = URL.createObjectURL(blob);
        image.src = next;
        if (objectUrl) URL.revokeObjectURL(objectUrl);
        objectUrl = next;
        const level = headers.get("X-Camera-Motion-Level") || "0";
        const brightness = headers.get("X-Camera-Brightness") || "0";
        const motion = headers.get("X-Camera-Motion") === "1" ? "Movement detected. " : "";
        status.textContent = motion + "Red outlines show areas that changed. Motion level " + level +
            "%, picture brightness " + brightness + "%.";
    }

    async function refresh(): Promise<void> {
        // Stop quietly once the preview is hidden (card closed, tab or mode changed).
        if (!running) return;
        if (!wrap.isConnected || wrap.offsetParent === null) {
            stop();
            return;
        }
        try {
            const response = await fetchPreview(PREVIEW_URL, {
                cache: "no-store",
                credentials: "include",
                headers: { "X-EspControl-Request": "camera-preview" },
            });
            if (!running) return;
            if (response.status === 200) {
                const blob = await response.blob();
                if (running) showPicture(blob, response.headers);
            } else if (response.status === 202) {
                status.textContent = "Starting the camera…";
            } else if (response.status === 404) {
                status.textContent = "Update the panel firmware to use the camera preview.";
            } else {
                status.textContent = "Camera preview is unavailable (" + response.status + ").";
            }
        } catch (_) {
            if (running) status.textContent = "Camera preview is unavailable. Check the connection.";
        }
        if (running) timer = setTimeout(refresh, REFRESH_MS);
    }

    button.addEventListener("click", function () {
        if (running) {
            stop();
            return;
        }
        running = true;
        button.textContent = "Hide Camera Preview";
        frame.style.display = "";
        status.textContent = "Starting the camera…";
        void refresh();
    });

    return { element: wrap, stop };
}
