# camera_motion

## User-fork JC4880P443 adaptation

The opt-in profile is
`devices/guition-esp32-p4-jc4880p443/camera-motion.yaml`. It selects the existing
one-lane mode and shares the panel's touch bus and display supply. Prepare the
private eight-slot editor with `scripts/prepare_jc4880_camera_profile.py` in a
disposable source snapshot; stock profiles do not expose camera motion.
The current JC4880 profile continuously captures for occupancy even when screen
wake is disabled. Occupancy timeout, wake, and local light-dimming controls are
shared between web settings and HA. Dimming defaults off and uses experimental
relative light, not lux; calibrate dark/bright scene levels before enabling it.
See `PROVENANCE.md` and the JC4880 experiment document for source revisions,
local modifications, physical results and remaining tests. The original
contributor's historical description follows; its V2 package path is not
installed here and its wake-only lifecycle does not describe this profile.

Trial component: wakes the screensaver when the front camera on the 7-inch
Guition JC1060P470 (OmniVision OV02C10) sees movement. It is included only in
the V2 panel profile through `common/addon/camera_motion.yaml`.

## How it works

- The camera streams only while the panel requests it: when Screensaver Mode
  is **Camera** and the screensaver has the screen dimmed, showing the clock,
  or off, or while **Camera Motion: Test Mode** is on. Camera mode and its
  sensitivity are set on the panel's settings page; the option only appears
  for device profiles with the `camera_motion` package (`cameraMotion`
  feature).
- The sensor is configured over the shared touch I2C bus (address `0x36`)
  from the ESPHome main loop. The MIPI-CSI receiver and image processor turn
  the 10-bit sensor output into an 8-bit raw frame in PSRAM.
- The frame length is stretched (`frame_rate_divider`, default 4) to lower the
  frame rate. This keeps memory traffic down and allows longer exposures.
- Each checked frame is reduced to a 32 x 18 brightness grid. Motion is
  counted when enough grid areas change after compensating for overall
  brightness drift. A simple auto exposure keeps the grid near mid-brightness.
- The receiver, image processor and both 2 MB frame buffers are released when
  the camera stops, so nothing is held while the screen is in use.
- Frames are not stored. The settings page can show a 320 x 180 preview from
  `GET /api/v1/camera/preview` (BMP, changed grid areas outlined in red). It
  requires the `X-EspControl-Request: camera-preview` header, applies the
  panel's optional web login, and keeps the camera running for 8 seconds
  after the last request. **Camera Motion: Log Picture** prints the brightness
  grid as text in the device log.
- If the panel crashes while the camera is active, camera motion stays off
  until the next normal restart.

## Sources

- Register tables: `ov02c10_modes.h`, from the OV02C10 driver proposed in
  espressif/esp-video-components PR #46 (Apache-2.0, see `LICENSE.md`).
- Register addresses and encodings for exposure, gain, frame length and chip
  ID match the upstream Linux `ov02c10` driver.
- Receiver and image processor settings follow Espressif's `esp_video`
  component for this sensor mode.
