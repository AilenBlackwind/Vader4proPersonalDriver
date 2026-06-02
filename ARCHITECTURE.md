# VaderDriver Architecture

## Purpose

`VaderDriver` is a Windows userspace helper for the Flydigi Vader 4 Pro.
The project reads HID reports from the controller, converts gyro input into mouse movement, exposes haptics through a named pipe, and hot-reloads per-app configs.

The code is split into small focused modules plus a separate desktop config editor, so UI work does not get mixed into the low-latency runtime path.

## Current Layout

```text
E:\Codex\VaderDriver
  Main.cpp
  ARCHITECTURE.md
  build.bat
  ConfigEditor.py

  src\
    config\
      config.cpp
      config.h
    gyro\
      filters.h
      gyro_processing.cpp
      gyro_processing.h
      joystick_mode.cpp
      joystick_mode.h
      profile_logic.cpp
      profile_logic.h
    haptics\
      haptic_pipe.cpp
      haptic_pipe.h
    runtime\
      app.cpp
      app.h
      runtime_state.h

  default.json
  master_config.json
  configs\
  RadialSelector.py
  VisualMenu1.py
  hidapi.h
  json.hpp
```

## Module Responsibilities

- `Main.cpp`
  Thin process entry point. Sets process-level options, initializes `hidapi`, creates runtime state, and calls `RunApp(...)`.

- `src/runtime/app.cpp`, `src/runtime/app.h`
  High-level runtime orchestration. Owns the config watcher lifecycle, controller reconnect loop, per-connection HID session loop, and module coordination.

- `src/runtime/runtime_state.h`
  Shared runtime state touched by the main loop and background threads.

- `src/config/config.cpp`, `src/config/config.h`
  Config structures, JSON loading, foreground-process-to-config resolution via `master_config.json`, and polling-based hot reload.

- `src/gyro/filters.h`
  Reusable signal filter primitives. Right now this is the `OneEuroFilter`.

- `src/gyro/gyro_processing.cpp`, `src/gyro/gyro_processing.h`
  Gyro runtime state, calibration, filtering, smoothing, joystick-mode shaping, recovery logic, and gyro-to-mouse conversion.

- `src/gyro/joystick_mode.cpp`, `src/gyro/joystick_mode.h`
  Virtual-stick steering path used by joystick mode, including stick build-up, centering, clamping, and output shaping.

- `src/gyro/profile_logic.cpp`, `src/gyro/profile_logic.h`
  Mode selection from the latest HID report, LED output helpers, and profile LED switching rules.

- `src/haptics/haptic_pipe.cpp`, `src/haptics/haptic_pipe.h`
  Named pipe server for `\\\\.\\pipe\\VaderHaptic` plus low-level rumble packet sending.

- `ConfigEditor.py`
  Desktop config editor for the JSON-driven tuning workflow. Handles grouped profile editing, target selection, app override editing, dirty-state protection, and quick LED/rumble test helpers.

## Runtime Flow

1. `Main.cpp` configures timing and priority, then initializes `hidapi`.
2. `RunApp(runtime)` starts the config watcher thread.
3. The app loop attempts to open the controller until it connects.
4. Once connected, a per-device session starts:
   - LED is reset.
   - The haptic pipe thread starts.
   - HID reports are read in a loop.
   - Active mode and profile are resolved from each report.
   - Gyro data is calibrated and converted into mouse movement.
5. On disconnect, the haptic thread is stopped, the device is closed, and reconnect polling resumes.
6. On shutdown, the config watcher thread is joined and `hidapi` is torn down.

## Shared State

Current shared state is grouped in `AppRuntime`:

```cpp
struct AppRuntime {
    std::mutex config_mutex;
    Config active_config;
    std::atomic<bool> config_changed{false};
    std::atomic<bool> app_running{true};
    std::atomic<bool> haptic_running{false};
    std::atomic<bool> device_connected{false};
};
```

Meaning:

- `config_mutex` protects `active_config` during watcher writes and runtime reads.
- `config_changed` tells the active device loop to refresh its local copy of config.
- `app_running` controls global lifetime.
- `haptic_running` controls the named-pipe worker lifetime.
- `device_connected` lets background workers stop cleanly when the controller disappears.

## Thread Model

Current threads:

- Main/app thread
  Owns reconnect behavior and runs the active HID session loop.

- Config watcher thread
  Polls `master_config.json` and the currently selected config file once per second.

- Haptic pipe thread
  Waits for named-pipe clients and forwards 6-byte rumble commands to the controller.

Rules:

- The active HID handle should have one clear owner: the connected-device loop in `src/runtime/app.cpp`.
- Shared config must be read and written under `config_mutex`.
- Thread coordination should stay small and explicit through `AppRuntime`.

## Important Boundaries

- `runtime` coordinates modules but should avoid absorbing domain logic.
- `config` should not know about HID packet layout or LED or rumble output.
- `gyro` should not know about named pipes or reconnect policy.
- `haptics` should not implement app lifetime or reconnect behavior.
- `ConfigEditor.py` should remain a tool around configs, not a second runtime.
- `Main.cpp` should remain thin.

## Known Sensitive Areas

- Gyro byte extraction in `src/runtime/app.cpp`
  The Vader 4 Pro uses a verified non-standard byte layout. Do not "clean up" indices without revalidation on hardware.

- `ApplyGyroMovement(...)` in `src/gyro/gyro_processing.cpp`
  Small math changes can alter feel, drift, smoothing, acceleration, and recovery behavior.

- `HandleCalibration(...)` in `src/gyro/gyro_processing.cpp`
  Bias logic directly affects stability and long-session drift.

- `HapticThread(...)` in `src/haptics/haptic_pipe.cpp`
  This is a cross-thread, overlapped-I/O path and should be changed cautiously.

- `DetermineMode(...)` in `src/gyro/profile_logic.cpp`
  Toggle behavior depends on previous report state, not only the current packet.

## UI Direction

If local UI keeps growing, prefer keeping it separate from the runtime loop.

Recommended direction:

- keep the controller and gyro runtime as the stable backend process;
- let the UI talk to it through config files or a small IPC layer;
- avoid mixing window code directly into the HID polling loop.

This keeps motion behavior deterministic and reduces the risk of UI work destabilizing device handling.

## Next Sensible Steps

- Add a short README section for config files, `master_config.json`, and the pipe protocol.
- If config editing grows further, extract `ConfigEditor.py` schema and widgets into a small `ui/` package.
- If HID helpers grow, consider a dedicated controller I/O module separate from gyro and haptics logic.
