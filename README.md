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

## Usage

1. Connect the Flydigi Vader 4 Pro controller (USB or dongle)
2. Run `main.exe` (or `start /min main.exe`)
3. The driver automatically detects the controller and applies the active config
4. Edit settings in real time with the config editor: `python ConfigEditor.py`

## Configuration

- `default.json` — base config applied to all applications
- `master_config.json` — maps executable names to per-app config files in `configs/`
- Configs support comments (`//`) for documentation

## Dependencies

- [hidapi](https://github.com/libusb/hidapi) — USB HID communication
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing
- [PySide6](https://pypi.org/project/PySide6/) — config editor GUI (`pip install PySide6`)

## License

MIT
