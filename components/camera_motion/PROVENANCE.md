# Camera motion experiment provenance

Adapted from infamy/espcontrol, commit
`ffc2886c79c133d2d6e64135e2087c5cf4da789c`, the head of
[EspControl PR #2034](https://github.com/jtenniswood/espcontrol/pull/2034).
The contributor reported testing on JC1060P470 V2; our JC4880P443 adaptation
requires separate physical validation. The upstream PR was closed unmerged.

The sensor tables retain their Espressif notices and Apache-2.0 licence in
`LICENSE.md`. Other component code follows the EspControl repository licence.
Local changes explicitly include the ESP-IDF ISP dependency and identify the
fork maintainer rather than claiming upstream ownership. The opt-in profile
selects the existing one-lane 1288x728 mode and uses the shared touch I2C bus and
display LDO. The first image-verification build left motion wake disabled;
the subsequent build wires opt-in wake into the existing screensaver policy.
Native web changes preserve newer HA camera-screen controls, expose motion
capability only through the private profile generator, and add sensitivity to
settings backups. Newer schedule-boundary behavior treats Camera Motion like
Timer mode. Test fixtures were adapted to the current backup contract.

The fork subsequently adds a motion-refreshed occupancy timer and an experimental
exposure/gain-compensated relative scene-light estimate. These extensions are not
claimed as contributor-validated features. The opt-in profile keeps capture
running to support occupancy independently of screen wake. Local brightness
interpolation is disabled by default pending room calibration; common display
policies use stock no-op defaults and opt-in expressions for sleep prevention
and active-screen brightness. Web/HA controls share persisted ESPHome entities.

This component is separate from the earlier `jc4880_camera_capture` experiment;
the two must never be loaded together because both own the CSI/ISP pipeline.
