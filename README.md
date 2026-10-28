# VaderDriver

Windows userspace driver for the **Flydigi Vader 4 Pro** controller — gyro-to-mouse, per-app profiles, and haptic feedback.

## Features

- **Gyro-to-mouse** — advanced gyro processing with OneEuro filtering, precision shaping, acceleration curve, and tilt-axis mixing
- **Virtual joystick mode** — gyro-controlled virtual stick with configurable deadzone, centering, and tilt shaping
- **Per-application profiles** — automatically switch configs based on the foreground window (e.g., different sensitivity for each game)
- **Profile switching** — switch between low/high sensitivity profiles via trigger depth, button combos, or keyboard hotkeys
- **Hot-reload** — config files are watched and reloaded at runtime — no restart needed
- **Haptic pipe** — external apps can send rumble commands via `\\.\pipe\VaderHaptic`
- **LED control** — per-profile RGB colors
- **Config editor** — PySide6 GUI for editing all parameters, managing app overrides, and testing LED/rumble directly via HID

## Building

### Requirements

- Visual Studio 2019 or 2022 (with "Desktop development with C++" workload)
- CMake (optional, `build.bat` uses MSVC directly)

### Build

Run `build.bat` from a Visual Studio Developer Command Prompt, or open the project in Visual Studio.

```
build.bat
```

The output is `main.exe`.
To keep a running build intact, use `build.bat main-roll-filtered.exe`.
Exit the previous driver before starting the experimental executable from this folder.

## Usage

1. Connect the Flydigi Vader 4 Pro controller (USB or dongle)
2. Run `main.exe` (or `start /min main.exe`)
3. The driver automatically detects the controller and applies the active config
4. Edit settings in real time with the config editor: `python ConfigEditor.py`

## Configuration

- `default.json` — base config applied to all applications
- `master_config.json` — maps executable names to per-app config files in `configs/`
- Configs support comments (`//`) for documentation

### Filtered acceleration experiment

Mouse acceleration now uses the combined horizontal and vertical motion after
trigger-roll suppression, per-axis One Euro filters, angle snapping and axis
mixing. Roll with no mapped contribution cannot accelerate the other axes.
The threshold is in mapped degrees/second: yaw/roll weights, tilt mixing and
`vertical_scale` affect it; base sensitivity, dots per turn and temporary
slowdown do not. Opposing yaw/roll contributions cancel before this measurement.
Stopping and bounce-back effects remain downstream of acceleration.

The multiplier stays at 1 through `accel_threshold`, then grows by
`accel_rate` per degree/second above it, capped at `accel_limit`. Pure joystick
mode still bypasses mouse acceleration; joystick overlay mode uses the combined,
filtered mouse motion including the overlay. Existing profile values are kept,
but thresholds may need retuning because they previously used unweighted motion
before One Euro filtering. Roll's existing independent filter settings are unchanged.

For a controller check, compare holding aim, slow tracking, trigger presses and
wide roll turns followed by a stop with the same profile. Try acceleration off
and on, and check combined yaw/roll turns for delay. Automated pipeline checks
run via `tests\run_tests.bat` and intercept mouse output.

### Precision recovery

Recovery now returns suppressed angular displacement during continued motion,
including below the precision threshold, independently for yaw, pitch and roll.
`recovery_rate` is read from profiles and exposed in the editor (inverse seconds;
default 2, zero disables recovery). Existing values such as 0.1 now take effect:
0.1 is a slow, approximately 10-second exponential time constant before limits.

Each axis stores at most 0.1 seconds of motion at its precision threshold.
Returned speed cannot exceed the current shaped speed, so the total at this
stage is at most twice the shaped input. Excess history is discarded; this is
bounded recovery, not guaranteed replay of every suppressed degree. A stopped
or reversed axis drops old debt. Deadzone motion is never recovered. Recovery
does not undo downstream filters, acceleration or pixel rounding.

Yaw/pitch smoothing clears their precision debt; roll recovery remains available
because roll retains its separate precision curve. Trigger-roll suppression
clears and suspends roll recovery to prevent delayed replay of button jolts.
Turning gyro off, changing profile/mode or entering pure joystick mode clears
the corresponding histories. This is an experimental policy, not a verified
replica of Steam Input.

## Offline gyro calibration check

Run `tests\run_calibration.bat` from any working directory. It builds a temporary
test executable, runs calibration regressions, and writes `gyro-replay.html`
beside the driver. Open that report in a browser and use Play or the time slider
to inspect the artificial input, calibrated angular speed, and captured mouse
path. The desktop cursor is never moved. The report reads the current
`default.json` low profile, including its accel offset and filters. Its scenario
requires a direct mouse profile (not joystick mode).

The comparison retains the previous library calibration policy as a diagnostic
baseline: 4 seconds at rest, 8 seconds of constant yaw at 8 degrees/second,
2 seconds in the same direction at 3 degrees/second, then 2 seconds at rest.
Constant gravity is appropriate for rotation about the gravity axis. Automatic
stillness calibration can absorb the constant turn and then report a negative
speed even while the slower turn continues. This demonstrates a host-side bug
without any firmware behavior. It does not prove the cause of every physical
controller incident.

The v2 sensor path uses one library calibration offset rather than two
successive bias estimators. Gyro and acceleration enter fusion in native SDL
X/Y/Z coordinates; historical mouse axis ordering is applied at the output.
Previously gyro X/Y were swapped before fusion while acceleration was not.
An ideal synthetic X or Y rotation produced about 17 degrees/second of false
bias in that mismatched basis, versus about 0.011 with matching axes.
The local library extension `StillnessMeanTimeConstant` also limits the mean's
memory to the larger of the collection/correction times (at least 0.1 seconds).
Upstream averaging is cumulative: a small bias change inside the accepted
stillness range can otherwise retain historical drift for an arbitrarily long
time. Other callers retain upstream behavior unless they enable this extension.

Automatic calibration while gyro is active is a separate option,
`calibration_while_active` (default `true`). Set it to `false` to learn only in
mode 0. In **all** modes, `calibration_max_gyro_speed` (default 0.3 degrees/second)
bounds the absolute incoming three-axis gyro magnitude eligible for learning.
This prevents steady turns above the threshold from being treated as rest,
including turns while the activation button is released. Manual key calibration
bypasses this automatic gate and still requires the controller to be still.

Recommended starting parameters for continuous gyro, pending device measurement:

```json
{
  "calibration_mode": 3,
  "calibration_while_active": true,
  "calibration_max_gyro_speed": 0.3,
  "calibration_correction_time": 2.0,
  "calibration_stillness_time": 0.75,
  "calibration_gyro_delta": 0.15,
  "calibration_accel_delta": 0.015,
  "accel_z_offset": 0.0
}
```

These are conservative starting points, not hardware-validated optimal values.
`calibration_gyro_delta` measures variation; it cannot replace the absolute speed
limit. Uniform rotation below that limit remains ambiguous, especially around
gravity. Noise/bias above the limit prevents automatic learning: use explicit
manual calibration or tune the limit using a stationary trace. Zero permits
automatic learning only at exactly zero speed. A Z offset of 15 adds **15 g**
to SDL acceleration and distorts the gravity direction; use zero unless a
measured sensor offset requires compensation. Existing user profiles are not
rewritten. Apply these root settings to the active per-app config as well.

`tests\run_tests.bat` includes the new regressions across three sample rates,
three axes, both signs and both active modes, plus startup, idle convergence,
continuous-mode learning on/off, off-mode turns, coherent gravity rotations
and manual calibration. Tests use the same sensor-processing entry point and
mouse conversion as the driver, intercepting `SendInput`. The replay bypasses
SDL/device transport, trigger events, config hot reload and the operating
system's pointer processing; physical device and game checks remain separate.

Build `build.bat main-calibration-v2.exe` for a separate experimental driver.
Exit the running driver before launching this file. The regular `main.exe`
is not replaced by that command.

For a physical reproduction, start `main-calibration-v2.exe --gyro-log`.
The console prints a new `gyro-trace-*.csv` filename in the working directory.
It records native sensor inputs, the learned offset, calibrated gyro, active
mode, learning gate and emitted mouse deltas. It does not record keystrokes or
foreground application names. Logging is opt-in, buffered once per second and
stops after ten minutes per connection; the last second can be lost if forcibly
closed. The file can be read while the driver runs. Reproduce a turn, stop,
then release activation and rest. Comparing raw input with calibration offset
separates an incoming opposite signal from a host-created correction. The trace
is diagnostic data; the current HTML scenario does not yet import real traces.

## Dependencies

- [hidapi](https://github.com/libusb/hidapi) — USB HID communication
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing
- [PySide6](https://pypi.org/project/PySide6/) — config editor GUI (`pip install PySide6`)

## License

MIT
