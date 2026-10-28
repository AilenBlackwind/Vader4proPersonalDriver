#include "app.h"
#include "debug_tools.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include <SDL3/SDL.h>

#include "../config/config.h"
#include "../gyro/gyro_processing.h"
#include "../gyro/profile_logic.h"
#include "../haptics/haptic_pipe.h"

namespace {

void RefreshModeState(SDL_hid_device* out, GyroState& state, GyroProfile& profile, int mode,
                      int& cur_r, int& cur_g, int& cur_b) {
    state.joyFY.updateParams(profile.joy_filter_cutoff, profile.joy_filter_beta);
    state.joyFP.updateParams(profile.joy_filter_cutoff, profile.joy_filter_beta);
    state.joyFY.reset();
    state.joyFP.reset();
    state.gyroFX.updateParams(profile.one_euro_min_cutoff, profile.one_euro_beta);
    state.gyroFP.updateParams(profile.one_euro_min_cutoff, profile.one_euro_beta);
    state.gyroFR.updateParams(profile.roll_euro_min_cutoff, profile.roll_euro_beta);
    state.gyroFX.reset();
    state.gyroFP.reset();
    state.gyroFR.reset();
    // Reset orientation/fusion state only — NOT the learned gyro calibration
    // (GamepadMotion::Reset() would wipe GyroCalibration and all Settings on
    // every mode toggle, undoing auto/manual calibration and config thresholds).
    state.motion.ResetMotion();
    state.accX = 0;
    state.accY = 0;
    state.tiltY = 0;
    state.tiltP = 0;
    state.lostY = 0;
    state.lostP = 0;
    state.lostR = 0;
    state.smooth_factor = 1.0;
    state.trigger_slowdown_timer = 0;
    state.trigger_slowdown_prev_active = false;
    state.trigRollSuppressTimer = 0.0f;
    state.trigRollSuppressTotal = 0.0f;
    state.trigRollPrevActive = false;
    // mode2Rumble* NOT reset here — rumble logic's else branch handles cleanup naturally.
    // Resetting mode2RumbleActive early would prevent the else branch from sending the stop command.
    state.debugRawR = 0.0;
    state.debugEuroR = 0.0;
    state.debugHor = 0.0;
    state.debugMx = 0;
    state.prevMode = mode;

    if (mode == 0) {
        SendLED(out, 0, 0, 0);
        cur_r = cur_g = cur_b = 0;
    } else {
        SetProfileLED(out, profile, cur_r, cur_g, cur_b);
    }
}

void RunConnectedDeviceLoop(SDL_Gamepad* gamepad, SDL_JoystickID gamepad_id,
                            SDL_hid_device* hid_in, SDL_hid_device* hid_out,
                            AppRuntime& runtime) {
    int cur_r = -1;
    int cur_g = -1;
    int cur_b = -1;

    SendLED(hid_out, 0, 0, 0);
    runtime.device_connected = true;
    runtime.haptic_running = true;
    std::thread haptic_thr(HapticThread, hid_out, std::ref(runtime.haptic_running),
                           std::ref(runtime.device_connected));

    unsigned char buf[kHidReportSize] = {0};
    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    TriggerDebugger dbg;

    GyroState state;
    GyroTrace trace(runtime.gyro_trace);
    Config conf;
    {
        std::lock_guard<std::mutex> l(runtime.config_mutex);
        conf = runtime.active_config;
    }


    bool disconnected = false;

    while (runtime.app_running && !disconnected) {
        // Poll SDL events for disconnect detection.
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED && e.gdevice.which == gamepad_id) {
                disconnected = true;
            }
        }
        if (disconnected || !SDL_GamepadConnected(gamepad)) break;

        // Non-blocking read of raw HID for button/trigger state.
        // Stale buf is fine when no report arrives — DetermineMode and
        // trigger slowdown only need recent values on state transitions.
        if (SDL_hid_read_timeout(hid_in, buf, sizeof(buf), 0) < 0) break;

        QueryPerformanceCounter(&now);
        double dt = fmin(0.1, (double)(now.QuadPart - prev.QuadPart) / freq.QuadPart);
        prev = now;
        state.absT += dt;

        if (runtime.config_changed) {
            std::lock_guard<std::mutex> lock(runtime.config_mutex);
            conf = runtime.active_config;
            runtime.config_changed = false;
            state.prevMode = -1;
            state.toggle_state = false;
            state.prev_toggle_button = false;

        }

        // Read sensor data from SDL3 gamepad API.
        // Gyro: rad/s. Accel: m/s².
        float gyro[3] = {0}, accel[3] = {0};
        SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_GYRO, gyro, 3);
        SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_ACCEL, accel, 3);

        // Replicate the old code's axis convention:
        //   ProcessMotion was called with (gyroY, gyroX, gyroZ) from raw HID bytes.
        //   SDL3 gyro is (roll, pitch, yaw) = (X, Y, Z).
        //   gyro[1] (pitch, Y) → rY (was gyro Y data)
        //   gyro[0] (roll, X)  → rP (was gyro X data)
        //   gyro[2] (yaw, Z)   → rR (was gyro Z data)
        // Gyro: rad/s → °/s; ProcessGyroSensors restores native axes for fusion.
        // Accel: m/s² → G (/ 9.80665).
        float rY = gyro[1] * 57.2958f;
        float rP = gyro[0] * 57.2958f;
        float rR = gyro[2] * 57.2958f;
        float aX = accel[0] / 9.80665f;
        float aY = accel[1] / 9.80665f;
        float aZ = (accel[2] / 9.80665f) + conf.accel_z_offset;

        // Analog trigger axes from SDL — react earlier than HID digital bytes.
        int analogTrigR = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) * 255 / 32767;
        int analogTrigL = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) * 255 / 32767;

        dbg.push(state.absT, rY, rP, rR, aX, aY, aZ);

        // Mode switching uses the raw HID buffer (same logic as before).
        int mode = DetermineMode(buf, conf, state);

        if (conf.manual_mode_keys_enabled) {
            if (conf.key_mode_0 != 0 && (GetAsyncKeyState(conf.key_mode_0) & 0x8000))
                mode = 0;
            else if (conf.key_mode_1 != 0 && (GetAsyncKeyState(conf.key_mode_1) & 0x8000))
                mode = 1;
            else if (conf.key_mode_2 != 0 && (GetAsyncKeyState(conf.key_mode_2) & 0x8000))
                mode = 2;
        }

        GyroProfile* p = (mode == 2) ? &conf.high_profile : &conf.low_profile;

        if (mode != state.prevMode) {
            RefreshModeState(hid_out, state, *p, mode, cur_r, cur_g, cur_b);
        }
        float slowdown = 1.0f;
        if (p->trigger_slowdown_enabled && mode != 0) {
            int trig_val = (p->trigger_slowdown_byte == 23) ? analogTrigL :
                           (p->trigger_slowdown_byte == 24) ? analogTrigR :
                           buf[p->trigger_slowdown_byte];
            bool trigger_active = trig_val >= p->trigger_slowdown_threshold;

            if (p->trigger_slowdown_duration > 0.0f) {
                if (trigger_active && !state.trigger_slowdown_prev_active) {
                    state.trigger_slowdown_timer = p->trigger_slowdown_duration / 1000.0f;
                }
                if (state.trigger_slowdown_timer > 0) {
                    slowdown = 1.0f - p->trigger_slowdown_amount;
                    state.trigger_slowdown_timer = fmax(0.0f, state.trigger_slowdown_timer - (float)dt);
                }
            } else {
                if (trigger_active) {
                    slowdown = 1.0f - p->trigger_slowdown_amount;
                }
            }
            state.trigger_slowdown_prev_active = trigger_active;
        }

        // Trigger rising-edge detection and coupled roll suppression.
        // Both the slowdown and suppression now read the trigger state
        // from SDL analog axes instead of raw HID bytes.
        if (mode != 0) {
            int trig_val = (p->trigger_slowdown_byte == 23) ? analogTrigL :
                           (p->trigger_slowdown_byte == 24) ? analogTrigR :
                           buf[p->trigger_slowdown_byte];
            bool trig_active = trig_val >= p->trigger_slowdown_threshold;

            // Analog-based early detection for roll suppression.
            constexpr int kAnalogTrigThreshold = 16; // ~2048/32767 raw
            bool analog_active = analogTrigR >= kAnalogTrigThreshold;
            if (analog_active && !state.trigRollPrevActive) {
                dbg.arm(state.absT, analogTrigR, trig_val);
                if (p->trigger_roll_suppression_ms > 0.0f) {
                    float t = p->trigger_roll_suppression_ms / 1000.0f;
                    state.trigRollSuppressTimer = t;
                    state.trigRollSuppressTotal = t;
                    state.gyroFR.reset();
                }
            }
            state.trigRollPrevActive = analog_active;
        } else {
            state.trigRollSuppressTimer = 0.0f;
            state.trigRollSuppressTotal = 0.0f;
            state.trigRollPrevActive = false;
        }

        dbg.tick(dt);

        if (conf.test_keys_enabled) {
            if (GetAsyncKeyState(conf.key_360) & 0x8000) {
                if (!state.k360_down) {
                    PerformRotation(360.0, *p);
                    state.k360_down = true;
                }
            } else {
                state.k360_down = false;
            }

            if (GetAsyncKeyState(conf.key_180) & 0x8000) {
                if (!state.k180_down) {
                    PerformRotation(180.0, *p);
                    state.k180_down = true;
                }
            } else {
                state.k180_down = false;
            }
        }

        // Bias correction, then feed gyro (°/s) and accel (G) to sensor fusion.
        ProcessGyroSensors(rY, rP, rR, aX, aY, aZ, dt,
                           state, mode, conf);

        if (conf.calibration_key != 0) {
            if (GetAsyncKeyState(conf.calibration_key) & 0x8000) {
                if (!state.cal_key_down) {
                    state.motion.StartContinuousCalibration();
                    state.cal_key_down = true;
                }
            } else {
                if (state.cal_key_down) {
                    state.motion.PauseContinuousCalibration();
                    state.cal_key_down = false;
                }
            }
        }

        ApplyGyroMovement(0, 0, 0, dt, state, *p, mode, slowdown);
        trace.record(dt, mode, conf, rP, rY, rR, aX, aY, aZ, state);
        dbg.updateMouse((float)state.debugEuroR, (float)state.debugHor, (float)state.accX, state.debugMx);

        // Mode 2 rumble: short weak left-trigger pulses while high-sensitivity mode is active.
        // Disabled via conf.mode2_rumble_enabled (config hot-reload is respected).
        // Cooldown prevents re-trigger when mode toggles rapidly at the trigger boundary.
        state.mode2RumbleCooldown = fmax(0.0f, state.mode2RumbleCooldown - (float)dt);
        if (mode == 2 && conf.mode2_rumble_enabled) {
            state.mode2RumbleTimer -= (float)dt;
            if (state.mode2RumbleTimer <= 0.0f && !state.mode2RumbleActive
                && state.mode2RumbleCooldown <= 0.0f) {
                SendRumble(hid_out, 0, 0, (uint8_t)conf.mode2_rumble_intensity, 0);
                state.mode2RumbleActive = true;
                state.mode2RumblePulseTimer = conf.mode2_rumble_duration;
                state.mode2RumbleCooldown = 0.1f;
            }
            if (state.mode2RumbleActive) {
                state.mode2RumblePulseTimer -= (float)dt;
                if (state.mode2RumblePulseTimer <= 0.0f) {
                    SendRumble(hid_out, 0, 0, 0, 0);
                    state.mode2RumbleActive = false;
                    state.mode2RumbleTimer = conf.mode2_rumble_interval;
                }
            }
        } else {
            if (state.mode2RumbleActive) {
                SendRumble(hid_out, 0, 0, 0, 0);
                state.mode2RumbleActive = false;
            }
            state.mode2RumbleTimer = 0.0f;
        }

        // Prevent busy-wait: ~1 ms throttle.
        SDL_Delay(1);
    }

    printf("[DISCONNECTED]\n");
    runtime.haptic_running = false;
    runtime.device_connected = false;
    haptic_thr.join();
}

// Opens a raw HID input path for the Vader 4 Pro (best report size).
SDL_hid_device* OpenHidInput() {
    struct SDL_hid_device_info* devs = SDL_hid_enumerate(0x04B4, 0x2412);
    if (!devs) return nullptr;

    std::string best_path;
    int best_bytes = -1;
    for (auto cur = devs; cur; cur = cur->next) {
        SDL_hid_device* test = SDL_hid_open_path(cur->path);
        if (!test) continue;
        SDL_hid_set_nonblocking(test, 1);
        unsigned char tmp[kHidReportSize];
        int res = SDL_hid_read_timeout(test, tmp, sizeof(tmp), 10);
        if (res > best_bytes) {
            best_bytes = res;
            best_path = cur->path;
        }
        SDL_hid_close(test);
    }
    SDL_hid_free_enumeration(devs);

    if (best_path.empty()) return nullptr;
    return SDL_hid_open_path(best_path.c_str());
}

SDL_Gamepad* OpenVaderGamepad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* gamepad = nullptr;
    for (int i = 0; ids && i < count; ++i) {
        // The raw HID protocol below is specific to this VID/PID. Never mix
        // another controller's sensors with Vader's buttons and outputs.
        if (SDL_GetGamepadVendorForID(ids[i]) != 0x04B4 ||
            SDL_GetGamepadProductForID(ids[i]) != 0x2412) continue;
        SDL_Gamepad* candidate = SDL_OpenGamepad(ids[i]);
        if (!candidate) continue;
        if (!SDL_GamepadHasSensor(candidate, SDL_SENSOR_GYRO) ||
            !SDL_GamepadHasSensor(candidate, SDL_SENSOR_ACCEL) ||
            !SDL_SetGamepadSensorEnabled(candidate, SDL_SENSOR_GYRO, true) ||
            !SDL_SetGamepadSensorEnabled(candidate, SDL_SENSOR_ACCEL, true)) {
            SDL_CloseGamepad(candidate);
            continue;
        }
        gamepad = candidate;
        break;
    }
    SDL_free(ids);
    return gamepad;
}

}  // namespace

int RunApp(AppRuntime& runtime) {
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_FLYDIGI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");

    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR)) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    LoadConfig(ResolveActiveConfigPath(), runtime.active_config);
    std::thread watcher_thr(ConfigWatcherThread, std::ref(runtime.config_mutex),
                            std::ref(runtime.active_config), std::ref(runtime.config_changed),
                            std::ref(runtime.app_running));

    while (runtime.app_running) {
        SDL_Gamepad* gamepad = nullptr;

        // Rescan attached devices so a failed open is retried even when no
        // new GAMEPAD_ADDED event arrives.
        while (!gamepad && runtime.app_running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {}
            gamepad = OpenVaderGamepad();
            if (!gamepad) SDL_Delay(100);
        }
        if (!gamepad) break;

        SDL_JoystickID gamepad_id = SDL_GetGamepadID(gamepad);

        // Open raw HID for button/trigger parsing and LED/rumble output.
        SDL_hid_device* hid_in = OpenHidInput();
        SDL_hid_device* hid_out = SDL_hid_open(0x04B4, 0x2412, nullptr);
        if (!hid_in || !hid_out) {
            printf("Failed to open raw HID\n");
            SDL_CloseGamepad(gamepad);
            if (hid_in) SDL_hid_close(hid_in);
            if (hid_out) SDL_hid_close(hid_out);
            SDL_Delay(100);
            continue;
        }

        printf("\n[CONNECTED] Vader 4 Pro\n");
        RunConnectedDeviceLoop(gamepad, gamepad_id, hid_in, hid_out, runtime);

        SDL_hid_close(hid_in);
        SDL_hid_close(hid_out);
        SDL_CloseGamepad(gamepad);
    }

    runtime.app_running = false;
    watcher_thr.join();
    SDL_Quit();
    return 0;
}
