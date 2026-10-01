---
title: JC4880P443 camera sensing experiment
description: Stock baseline, Stage 2 onboard capture, and proposed motion and relative ambient light experiments for a Guition JC4880P443C-I-W-Y running ESPControl.
---

# JC4880P443 camera sensing experiment

Stage 1 record, 1 October 2026. Determine whether the Guition
JC4880P443C-I-W-Y camera can detect room activity and distinguish useful room
light levels before adding external sensors. The stock ESPControl target builds,
and this physical display's vendor log confirms an OV02C10 camera. Capture,
motion, and ambient-light implementation required separate approval for later
stages. No firmware or device YAML changed in Stage 1; no flash was performed.
Stage 2 was subsequently approved; its opt-in capture configuration and
physical test results are recorded below. Motion and ambient-light algorithms
remain unimplemented.

## Fork and contribution boundary

Working fork: [srnewkirk/espcontrol](https://github.com/srnewkirk/espcontrol).
Branch: `codex/jc4880p443c-camera-sensing-test`, based on upstream main
`90f851904` (full commit recorded in the Stage 1 evidence). Existing canonical
checkout: `C:\Users\srnew\source\repos\espcontrol`; isolated working checkout:
`C:\Users\srnew\source\repos\espcontrol-camera-sensing`.

`origin` points to the user's fork. `upstream` fetches
`https://github.com/jtenniswood/espcontrol.git`; its push URL is deliberately
disabled. `remote.pushDefault=origin`; the experiment branch tracks its own
fork branch after push. Fetch upstream, then merge or rebase deliberately onto
this focused branch for future updates. Do not rewrite or merge upstream.

GitHub reports this fork as **public**, not access-restricted. Private development
here means user-owned experimental work with no upstream contribution. No upstream PR,
issue, discussion, review, comment, upstream push, merge, or upstream automation
is authorized. Any useful upstream finding stays in this document until a
separate user-authorized task. This boundary and the exact user-requested branch
name override the generic PR and branch-name guidance in `AGENTS.md`.

## Existing support and source ownership

Read `AGENTS.md`, `DEVELOPERS.md`, `docs/reference/contributing.md`,
`product/README.md`, device YAML, and `.github/workflows/firmware-compile.yml`.
The latest upstream snapshot uses the root developer pointer and contribution
guide; the older existing checkout's `dev-docs/` hierarchy is absent from this
snapshot. Use the existing `docs/reference/` convention.

| Source | Role |
| --- | --- |
| `product/v2/devices/guition-esp32-p4-jc4880p443.json` | Authoritative device entry; shared profiles in `product/v2/device_catalog.json` |
| `devices/guition-esp32-p4-jc4880p443/packages.yaml` | Existing package graph, six cards, shared infrastructure and UI |
| Same device's `device/device.yaml` | P4, hosted C6, PSRAM, display, touch and LVGL wiring |
| Same device's `device/lvgl.yaml`, `fonts.yaml`, `sensors.yaml` | Existing screen, fonts and subscriptions |
| Same device's `dev.yaml` | Local development entry; Wi-Fi secrets required |
| Same device's `esphome.yaml` | User installation entry, pulling published upstream packages |
| `builds/guition-esp32-p4-jc4880p443{,.factory,.recovery}.yaml` | Stock CI/release build entries; factory image used for baseline |
| `components/espcontrol`, `web_server_idf`, `artwork_image`, `sendspin` | Active custom firmware components |
| `components/mipi_dsi/models/guition.py` | Repository display-model reference; stock target resolves ESPHome's built-in MIPI DSI driver |
| `common/device/esp32_c6_firmware_update.yaml` | Existing C6 update/recovery integration |
| `common/addon/memory_diagnostics.yaml` | Existing heap and PSRAM diagnostics to reuse |

Do not edit generated manifests, product snapshots, web bundles, generated
screen docs, or generated blocks in package/sensor YAML. Their authored inputs
and generators are listed in `product/README.md`. The current task changes
authored documentation only. Later camera work should add a bounded component
and local experiment entry using the existing board packages, not another board
abstraction. Keep the stock entry available for comparisons.

The existing board configuration specifies ESP32-P4, 16 MB flash,
`engineering_sample: true`, ESP-IDF, experimental features, and code execution
from PSRAM. PSRAM runs at 200 MHz. Hosted Wi-Fi uses C6 over SDIO at 20 MHz:
reset GPIO54, CMD19, CLK18, D0â€“D3 GPIO14â€“17. Preserve dynamic hosted buffers and
PSRAM task stacks; internal DMA memory has already been a constraint.

The display is 480Ã—800 MIPI DSI with model `JC4880P443`, ST7701-family panel,
LDO3 at 2.5 V, reset GPIO5, and backlight PWM GPIO23. Repository model values
are two DSI lanes, 500 Mbps and 34 MHz pixel clock. GT911 touch shares I2C
SDA7/SCL8 at 400 kHz. LVGL uses a full-size buffer, little-endian pixels and
initial 180Â° rotation. Avoid changing these proven settings for the camera.

`screensaver_camera_supported` and `screen_camera_screensaver.yaml` mean
Home Assistant camera-image downloads, not onboard capture. Searches found no
onboard `esp_video`, `esp_cam_sensor`, or `esp32_camera` integration in stock
ESPControl. Image decoding and local CSI acquisition are separate workloads.

## Stock build and physical baseline

Unchanged target: `builds/guition-esp32-p4-jc4880p443.factory.yaml`.
Pinned container: `ghcr.io/esphome/esphome:2026.9.1`, digest
`sha256:776a845d508909bf426d2ee7bdd81796361ebe6a7f091807ce1d6d0b48985413`.
The Linux container command was:

```sh
esphome compile /config/builds/guition-esp32-p4-jc4880p443.factory.yaml
```

Directly mounting the Windows Git worktree first failed because `file:///config`
could not follow its Windows `.git` worktree pointer. Exporting unchanged HEAD
with `git archive`, unpacking it inside Linux, and creating a temporary container
Git snapshot allowed the stock component reference to resolve. No YAML/source
override was used. The exact host and container commands and logs are retained
in the task evidence; this temporary snapshot is not a durable repository.

**Result: successful compile, exit 0.** ESPHome 2026.9.1 resolved ESP-IDF 5.5.5,
RISC-V GCC 14.2.0 (`esp-14.2.0_20260121`), CMake 3.30.2 and Python 3.14.6.
Image generation reported IDF esptool.py 4.12.0; the container's standalone
esptool was 5.3.1. Hosted dependency resolved to 2.12.12. The resolved lockfile
is retained for repeatability; the framework version is a resolved default,
not an explicit pin in the board YAML.

Reported static RAM: 204,464 / 576,464 bytes (35.5%). Image size:
5,670,996 / 7,274,496 bytes (78.0% of app budget). These are linker figures,
not runtime free heap/PSRAM measurements. Factory and OTA images were generated
inside the temporary container, which was removed; no flashable binary is
delivered. The build log is retained.

Warnings included experimental ESP-IDF features, replaced external-stack
Kconfig option and automatic reconfiguration, Opus possible uninitialized value,
RISC-V sign conversions, and Sendspin possible dangling reference. No unrelated
fixes were made. A compile pass does not establish physical ESPControl operation.

Documentation validation passed: locked npm dependencies on Node 24.14.0,
installer regression check, VitePress build, and `scripts/check_docs_site.py`
(87 canonical pages, 59 FAQ answers, 20 redirects, internal links/anchors).
On Windows, the `docs:build` wrapper reached rendering but its final `python3`
command hit the Store alias. Run the checker with bundled Python and `-X utf8`
to avoid the alias and Windows default-encoding mismatch. No application-wide
CI claim is made for this documentation-only change.

The user reports USB-connected vendor firmware. A replacement cable exposed
**COM13**, Espressif USB JTAG/serial (`303A:1001`). An eight-second serial
observation, with DTR/RTS disabled and no transmitted commands, showed a boot
with `CHIP_USB_UART_RESET`: opening the interface appears to have restarted it.
Firmware and flash content were not changed, and the port was closed afterward.

Vendor logs confirm P4 revision v1.3, ESP-IDF 5.5.4-dirty, 16 MB flash,
32 MB PSRAM at 200 MHz, GT911 touch, ST7701 display, OV02C10 PID `0x5602`,
MIPI-CSI video 2.0.1, and negotiated **1288Ã—728**. C6 transport warned that
host 2.11.0 exceeds coprocessor 2.3.0. Before flashing, verify the pre-v3 P4
target assumption, vendor camera demo, lens orientation and unobstructed view,
correct USB data port, recovery path and vendor-firmware backup, stock display/
touch behavior, and C6 compatibility. Do not update C6 merely to conduct Stage 1.

## Camera evidence and driver choices

[Guition's model list](https://www.guition.com/model-selection) links the
[manufacturer reference archive](https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip).
Retrieved size: 477,630,572 bytes; SHA256:
`34e0f0aa80a9097f0649c7e93b1aece92c5e2bc237fd9fb6dd5875259fa61467`.
All ten download-shard checksums were verified. The archive contains OV02C10
datasheet, schematics, sensor driver and IDF camera/display examples. References
below are paths inside that archive, not new firmware copied into ESPControl.

`5-Schematic/2_LCD&CSI.png` shows a 15-pin, 0.3 mm CSI connector: data0 N/P
pins 2/3, data1 N/P 5/6, clock N/P 8/9, control pins 13 SCL and 14 SDA.
`3_ESP32-P4.png` maps SCL/SDA to GPIO8/7 and routes CSI differential signals
directly to the P4 module's dedicated CSI pins. Control therefore shares the
touch/audio bus; do not initialize another independent I2C driver on those pins.
The demo defaults reset and power-down pins to -1; connector control/power
sequencing remains a physical-module verification item, not an invented GPIO.

`1-Demo/idf_examples/components/esp_cam_sensor/sensors/ov02c10/` provides
RAW10 Bayer modes, SCCB address `0x36`, PID `0x5602`, 24 MHz input clock,
and exposure/gain setters and cached-value getters. Driver tables include
1288Ã—728 and 1920Ã—1080; the smaller table's name, register-table name, FPS
and lane fields disagree. Do not claim a native 160Ã—90 mode or 2â€“5 fps from
those labels. Verify the selected table and measure frame timestamps.

`ESP-IDF_5.5.4/video_lcd_display/` configures OV02C10 and an ISP controller;
`main/app_video.c` uses V4L2, two to three buffers and an existing I2C handle
when supplied. `main/main.c` demonstrates camera/display operation and PPA
processing. This proves a vendor integration path, not ESPControl UI performance.

[Espressif esp_video](https://github.com/espressif/esp-video-components/blob/master/esp_video/README.md)
provides P4 CSI capture and ISP conversion. Request YUV and sample Y where the
selected RAW10 pipeline supports it; otherwise use RGB565 and integer luminance
conversion. Enumerate negotiated formats and `sizeimage`; a JPEG encoder's
Gray8 support does not establish direct grayscale capture. Keep Bayer/ISP
configuration consistent. Do not use the S3 DVP `esp32-camera` path for this CSI
sensor. OV02C10 is absent from the inspected official sensor list and its
[driver PR 46](https://github.com/espressif/esp-video-components/pull/46) was
open/unmerged on the inspection date; vendor/community integration is available.

## Community precedents and reuse plan

| Project inspected | Evidence and useful reuse | Limits |
| --- | --- | --- |
| [HomeTiles exact-board camera support](https://github.com/GalusPeres/HomeTiles/blob/a980aa752a8af89e268396e17eb35af726d2c3ff/src/devices/guition_jc4880p443_portrait/README.md) | Covers I_W/Y and reports physical camera testing; `local_camera_board.cpp` reuses shared I2C and reference-counted LDO3 at 2.5 V. Its OV02C10 driver has explicit exposure/gain control. | Independent firmware architecture; release notes still request wider exact-board field feedback. Study integration, retain ESPControl's board layer. |
| [sullb ESPHome P4 CSI component](https://github.com/sullb/esphome-p4-csi-camera/tree/461aa9b178be63b40e1bb7c1f967c971640be600) | Reports OV02C10 capture, JPEG and HA API streaming on a Guition 10.1-inch P4 panel; ESPHome code generation and V4L2 lifecycle are relevant. | Author calls it experimental and unreviewed. Its inspected setup owns I2C anew, which requires adaptation for ESPControl touch. Streaming/JPEG are unnecessary for local sensing. |
| [Tasmota frame-difference source](https://github.com/arendst/Tasmota/blob/48da93dca8728c4c246b33a2d7f23c0ecaa056f0/tasmota/tasmota_xdrv_driver/xdrv_81_esp32_webcam_task_motion.ino) | Existing scaled grayscale differences, masks, pixel thresholds and normalized frame brightness establish the sensing approach. | Legacy webcam backend is not a drop-in P4 CSI implementation; brightness is not validated room illuminance. Study algorithm behavior; review licenses before copying code. |
| [Tasmota P4 CSI source](https://github.com/arendst/Tasmota/blob/48da93dca8728c4c246b33a2d7f23c0ecaa056f0/tasmota/tasmota_xdrv_driver/xdrv_81_2_esp32_webcam_CSI_h264.ino) | Existing P4 motion proxy from compressed P-frame sizes, with smoothing. | Adds H.264 work and depends on encoding settings; prefer simple image differences here. |

[Tasmota's OV02C10 field discussion](https://github.com/arendst/Tasmota/discussions/24497)
reports working capture alongside color, exposure and mode problems. It supports
reuse and targeted verification, not an exact-board room-light accuracy claim.
No inspected project established all four light classes on this exact display
while running ESPControl. Camera-based motion and frame brightness are established
approaches; the controlled experiment measures their fitness for this use.

## Proposed motion experiment

Start at **2 processed frames/s**, increasing to 5 only if latency warrants it.
Reduce a verified capture frame to a 160Ã—90 8-bit luminance grid (proposal,
not a native sensor mode). Use area averages or sparse sampling with consistent
Bayer/ISP handling. Prefer Y samples; otherwise convert RGB565. Keep current
and previous grids; no JPEG encoding, preview, network frames, ML, person,
face or object recognition.

Compute `d = abs(current - previous)`; count pixels where `d > noise_threshold`.
Log changed fraction (0â€“1) and mean absolute difference. Begin tuning around
8/255 difference and 3% changed pixels, explicitly provisional. Optional ROI
excludes windows, TV and display glare; small region scores distinguish local
movement from a whole-frame lighting change. Require two consecutive qualifying
samples, and about two seconds quiet to clear. Establish the first reference
without triggering. Suspend comparisons during exposure changes or report
global illumination transitions separately; do not silently suppress real motion.

At 2 fps, two-sample persistence contributes roughly 0.5â€“1 s latency, plus
capture/processing delays. Test slow/fast room crossings, distant movement,
stationary occupants, shadows, LEDs, television, sunlight and light switches.
Image motion is not stationary-occupancy detection.

## Proposed relative ambient light experiment

Measure mean, median and clipped-pixel fraction in a stable ROI. Auto exposure
and gain can hold luminance constant as the room gets darker, so average pixel
brightness alone is insufficient. Log exposure, gain and configuration with
each accepted measurement. Vendor getters return cached settings, not guaranteed
per-frame metadata; group-hold writes are commented out. Associate settings with
frames only after settling and measuring control delay.

Compare automatic operation with a short fixed-exposure/fixed-gain burst after
disabling the controller that would overwrite those settings. Discard settling
frames; restore automatic behavior afterward. An exploratory `Y/(exposure*gain)`
trend is valid only with consistent pipeline, gain units and non-clipped pixels;
gamma, scene reflectance and ISP processing prevent interpreting it as lux.

Reuse motion frames for continuous observation. If motion is disabled, compare
brief measurement bursts every 5â€“10 s with continuously running capture; sensor
startup, auto-exposure settling and power behavior determine the better choice.
Test dark/dim/normal/bright in the intended installation, repeating with dark
and pale objects, people, daylight, artificial light and different screen
brightness. Define thresholds from collected data, add hysteresis and a 3â€“5 s
stable-class dwell before eventual automation. If scene changes overwhelm light
classification, a dedicated lux sensor remains justified.

## Resource estimates and measurements

These are arithmetic planning estimates, not measured camera costs.

| Buffer or work | Estimate |
| --- | --- |
| Two 160Ã—90 luminance grids | 28,800 bytes; 28.1 KiB |
| Three 160Ã—90 grids | 43,200 bytes; 42.2 KiB |
| Differencing at 2â€“5 fps | 28,800â€“72,000 pixel comparisons/s, excluding conversion and capture |
| One 1288Ã—728 RGB565 frame | 1,875,328 bytes; 1.79 MiB |
| Two such capture frames | 3,750,656 bytes; 3.58 MiB |
| Three such capture frames | 5,625,984 bytes; 5.37 MiB |
| One 1288Ã—728 RAW10 frame | About 1.12 MiB if packed; 1.79 MiB if stored in 16-bit words, before stride/alignment |
| One 480Ã—800 RGB565 display-sized buffer | 768,000 bytes; 750 KiB; actual display/LVGL buffer count must be measured |

ISP/DMA queues, alignment, task stacks, code in PSRAM and image assets add to
these figures. Full-rate capture can remain near 30 fps even when only two frames
are processed each second: RGB565 traffic would be about 56 MB/s before extra
copies. Dequeue/requeue unused frames promptly. Do not claim low capture power
from a slow algorithm alone. Avoid unbounded queues and per-frame allocations.

CPU demand for the small difference grid should be modest; conversion, CSI/ISP
and memory contention are the larger unknowns. Display rotation may already use
PPA, so do not assume the accelerator is free. Keep LVGL work on its existing
thread; camera processing yields and never blocks the UI loop. Reuse the I2C
bus and shared PHY supply without changing display voltage or touch polling.

No camera pixels need cross the C6/SDIO/Wi-Fi link. Publish scalar diagnostics
at a limited rate later. Check network latency, reconnects and hosted buffer
headroom anyway because the P4 shares CPU and memory resources. Sensor/ISP
operation may increase power and temperature; values require measurement.

Record stock, capture-only, motion and light phases under the same UI/network
workload: per-core task CPU time, capture rate/drops, processing time p50/p95,
loop/touch response p50/p95, free/minimum/largest internal heap and PSRAM block,
buffer sizes/counts, API latency/reconnects, temperature and watchdog/reboot
counts. Soak capture for 24 h; exercise sleep/wake, touch, image cards, network
loss/recovery and OTA only when that stage explicitly authorizes it.

## Proposed acceptance criteria and stages

Targets are proposed experiment gates, not promises: at least 90% of 20 room
movement trials per tested light condition, p95 detection within 1 s, and at
most one false event/hour over an eight-hour quiet-room test, with causes logged.
UI should show no perceptible degradation and p95 touch response increase below
50 ms versus stock. No camera-induced watchdog reset or network reconnect,
and no progressive memory loss after warm-up over 24 h. Revisit processing rate
only after measuring failures.

For light, seek at least 90% correct class assignment over ten trials/class and
no more than one spurious transition per 30 minutes of steady lighting across
scene tests. Verify threshold margins and dwell stability before dimming or HA
automation. Failure to separate classes robustly answers the lux-sensor question;
it is not a reason to add ML.

1. **Stage 1, current:** fork/remotes/branch, unchanged stock compile,
   repository and camera evidence, community reuse analysis, experiment design.
2. **Stage 2, approval required:** preserve vendor recovery evidence, confirm
   pre-v3 target/C6 compatibility, establish stock ESPControl runtime; add bounded
   OV02C10 capture using existing board/I2C/LDO support. Capture one verified
   frame, then sustained capture producing a small luminance grid at 2 fps.
   Enumerate actual modes/formats and measure capture-only resource/UI impact.
3. **Stage 3, separate approval:** differences and scalar score, threshold/ROI/
   debounce tuning, latency/false-positive testing and UI/resource comparison.
4. **Stage 4, separate approval:** brightness/exposure/gain record, fixed-setting
   comparisons, four-class tests, stability and dedicated-lux-sensor decision.

**Exact next action:** after explicit Stage 2 approval, first preserve the
current vendor-firmware recovery/backup path and verify the stock pre-v3
ESPControl/C6 baseline on COM13; then select and pin the existing OV02C10 driver
integration, reuse shared I2C/LDO, and capture one frame without motion or
ambient-light processing. Do not wholesale import an alternate firmware or
duplicate the board definition.

Unresolved: visual camera-demo result and orientation; vendor driver license/
version compatibility with IDF 5.5.5; correct smaller-mode timing/lane table;
supported RAW10-to-YUV conversion; actual capture minimum rate and buffer layout;
exposure-to-frame synchronization; ESPControl runtime and thermal baselines;
whether the existing C6 firmware needs the project's recovery/update procedure.
These are Stage 2 verification items, not missing proof that the camera exists.

## Stage 2 capture configuration

Stage 2 was explicitly approved on 2026-10-01. Motion and ambient-light
algorithms still require separate approval. The opt-in entry point is
`devices/guition-esp32-p4-jc4880p443/camera-capture.yaml`; the normal factory
target and existing device packages are unchanged. It includes the stock
factory package and adds `components/jc4880_camera_capture` locally.

The sensor driver is adapted from the pinned HomeTiles example above, retaining
its Apache-2.0 license, notices, mode tables, and provenance. This avoids the
unmerged OV02C10 `esp_video` dependency. CSI and ISP use the ESP-IDF components
already included in the stock framework. ESPHome registers their dependencies
with `include_builtin_idf_component`; no generated build files are edited.

The driver attaches sensor address `0x36` to ESPControl's existing I2C bus using
`i2c_master_get_bus_handle`. It does not recreate the bus, change touch pins,
or acquire another board abstraction. The existing display package supplies
LDO3 at 2.5 V. The HomeTiles window override produces 1280x720 RAW10, one CSI
lane at 400 Mbit/s, with ISP conversion to RGB565. Two aligned PSRAM buffers
consume 3,686,400 bytes, plus a 14,400-byte 160x90 luminance grid.

Capture begins 30 seconds after startup. The sensor runs at its mode-table
rate; only the grid sampling is limited to 2 Hz. Each grid pixel centre-samples
an 8x8 block. This establishes capture feasibility; spatial averaging and
noise handling belong to the later experiments. Exposure and gain remain at
the fixed mode-table settings. There is no automatic exposure, JPEG encoding,
image preview, network frame transport, motion score, brightness entity, or
classification. The optional configuration disables automatic P4 and C6
firmware updates so a stock release cannot silently replace this experiment
during the controlled comparison. Manual update controls remain available.

The ISR freezes a completed buffer during sampling and supplies the other
buffer for subsequent DMA frames. Cache synchronization occurs before CPU
reads. Initialization failure or a five-second frame timeout stops capture
and marks this component failed. The normal display/network components remain
in place. Health logs every ten seconds report cumulative CSI frames and
sampled grids, incomplete frames, a checksum, conversion time, loop gaps,
internal free/largest heap block, and PSRAM free space. Conversion time is
not total CPU utilization, and loop gaps are not a measurement of physical
touch latency.

Compile with the pinned image and a Linux Git snapshot at `/config`:

```sh
esphome compile /config/devices/guition-esp32-p4-jc4880p443/camera-capture.yaml
```

The native RGB565 conversion check can run with:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -I. \
  tests/firmware/jc4880_capture_grid_test.cpp -o /tmp/jc4880-grid-test
/tmp/jc4880-grid-test
```

### Local recovery and stock hardware baseline

The connected display is COM13, P4 revision 1.3, 16 MB flash. Secure boot and
flash encryption are disabled. Before writing firmware, the full flash was
read to ignored local file `.cache/camera-stage2/vendor-p4-16mb.bin`, exactly
16,777,216 bytes, SHA256
`f6e67898c4543aeaa709da4a9ecca34ebfd4e6cddcddd8198fdccf11cabe3fee`.
`verify_flash` compared the full backup with the device and reported
`verify OK (digest matched)`. This backup may contain vendor configuration;
keep it local and outside Git.

Esptool 5.3.1 stopped repeatedly near address `0x491000`. ESP-IDF's esptool
4.12.0 read that region, completed the full backup, and verified its digest.
Use the verified working 4.12.0 environment for this physical panel. To restore
the P4 vendor firmware from the repository root, use this explicit command:

```powershell
& .cache/camera-stage2/venv-idf/Scripts/python.exe -m esptool `
  --chip esp32p4 --port COM13 write_flash 0 `
  .cache/camera-stage2/vendor-p4-16mb.bin
```

This restores the P4 flash only; it does not restore a separately changed C6.
No C6 firmware update was deliberately performed for the experiment.

The unchanged stock factory target rebuilt successfully with the Stage 1
versions and size figures. Its factory image was uploaded at offset zero;
the upload hash verified. A captured stock boot reported 32 MB PSRAM, GT911
at `0x5D`, the MIPI display, an initialized setup AP, and C6 firmware 2.3.2.
It subsequently reported a successful boot. Physical rendering/touch response,
normal Wi-Fi provisioning, and Home Assistant operation require user testing;
successful initialization logs do not establish those results.

### Automated validation

The unchanged stock and opt-in capture configurations compile successfully
with ESPHome 2026.9.1 / ESP-IDF 5.5.5. Capture uses 5,695,172 application
bytes (78.3% of the application partition) and 220,896 static RAM bytes
(38.3%): increases of 24,176 flash bytes and 16,432 static RAM bytes over
stock. Runtime DMA buffers are additional PSRAM allocations, not part of
these static RAM figures.

All 73 native firmware tests pass, including known RGB565 colors, a monotonic
gray ramp, the frame-clock timing regression, and millisecond rollover.
Device profiles and the device matrix pass. The two imported sensor-register
arrays match their pinned-source hashes. Documentation builds and passes
the site's internal link/anchor checks.

`npm run prepare:ci` was also attempted in the Linux snapshot. It does not
entirely pass: untouched baseline checks report a missing web-asset firmware
version declaration, a stored web-bundle SHA mismatch, and stale card-runtime
coverage fixtures. These were reproduced after restoring temporary generated
outputs; no related source was changed. The native firmware suite passes when
the image's bundled CMake is on PATH. The installer static check passes, but
its browser check is unavailable in this build image because Chromium's shared
libraries are missing; Windows also lacks the matching browser executable.
Do not describe the whole CI suite as passing or merge on this evidence alone.

### Physical capture measurements

A ten-minute USB observation of the corrected capture code reached uptime
593 seconds, 16,890 CSI frames, and 1,118 sampled grids. Capture starts after
the initial 30-second stock interval; the steady health-sample comparison
covers uptime 43 to 593 seconds (550 seconds of capture).

| Measurement | Observed result |
| --- | --- |
| Sampled-grid rate | 1.996 grids/s, targeting 2 Hz |
| CSI frame rate | 30.165 frames/s; sensor capture remains continuous |
| Incomplete frames / capture errors | 0 / 0 |
| Grid conversion wall-time fraction | 0.712%; includes cache sync, sampling and checksum, excludes total CSI/ISP/ISR cost |
| Maximum grid conversion duration | 3,935 microseconds |
| Largest loop gap after the first startup reporting window | 30 ms |
| Internal free heap at the final sample | 354,512 bytes |
| Largest free internal block | 270,336 bytes |
| Free PSRAM during the health samples | 20,669,012 to 20,669,044 bytes |
| PSRAM before camera initialization | 24,355,876 bytes |
| Restarts during observation | None after the intentional observation reset |

The first frame produced a 160x90 grid with checksum `0db85d5a`, value range
10 to 16, and 3,708 microseconds conversion time. Subsequent checksums changed;
that alone does not distinguish scene detail from sensor noise. Image content
and orientation have not been visually verified. These measurements were
collected with the idle onboarding UI/setup AP, not configured Home Assistant
cards, artwork, or audio load. They demonstrate sustained capture and bounded
memory use; they do not establish useful motion detection or light measurement.

The same capture code is used in the final configuration, which additionally
disables P4 automatic updates. A separate post-upload startup check confirms
that configuration. A 24-hour soak, physical temperature measurement, visible
display/touch responsiveness, normal Wi-Fi/Home Assistant behavior, and visual
frame verification remain pending. Keep this experiment out of `main` until
the user confirms the physical tests.
External temperature/humidity, lux and mmWave sensors, CAD and enclosure work
remain out of scope. No upstream/community-facing action was performed.
