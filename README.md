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

## Dependencies

- [hidapi](https://github.com/libusb/hidapi) — USB HID communication
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing
- [PySide6](https://pypi.org/project/PySide6/) — config editor GUI (`pip install PySide6`)

## License

MIT
