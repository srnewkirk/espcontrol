# Stage 2 capture experiment

The OV02C10 sensor files are adapted from HomeTiles commit
`a980aa752a8af89e268396e17eb35af726d2c3ff`, directory
`src/video/local_camera/sensors/ov02c10`:
https://github.com/GalusPeres/HomeTiles/tree/a980aa752a8af89e268396e17eb35af726d2c3ff/src/video/local_camera/sensors/ov02c10

Their Apache-2.0 license and original notices are retained. Adaptations replace
HomeTiles include paths and compilation guards with ESPHome equivalents.
See UPSTREAM_PROVENANCE.md for the original manufacturer register-table sources.
No HomeTiles display, board, network, JPEG, or application layer is imported.

The capture lifecycle follows the same project's CSI/ISP integration using
ESP-IDF APIs already included in the existing ESPControl framework. This
experiment uses the existing I2C bus and powered MIPI PHY, two 1280x720 RGB565
buffers, and one 160x90 luminance grid sampled at 2 Hz. It uses the sensor's
fixed mode-table exposure/gain; it does not implement automatic exposure,
brightness classification, motion detection, JPEG, or network image streaming.
