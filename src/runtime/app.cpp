#include "app.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
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
    state.motion.Reset();
    state.accX = 0;
    state.accY = 0;
    state.tiltY = 0;
    state.tiltP = 0;
    state.lostY = 0;
    state.lostP = 0;
    state.smooth_factor = 1.0;
    state.trigger_slowdown_timer = 0;
    state.trigger_slowdown_prev_active = false;
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
    std::thread haptic_thr(HapticThread, hid_out, std::ref(runtime.haptic_running),
                           std::ref(runtime.device_connected));

    unsigned char buf[64] = {0};
    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    GyroState state;
    Config conf;
    {
        std::lock_guard<std::mutex> l(runtime.config_mutex);
        conf = runtime.active_config;
    }

    {
        auto calMode = static_cast<GamepadMotionHelpers::CalibrationMode>(conf.calibration_mode);
        state.motion.SetCalibrationMode(calMode);
        state.motion.Settings.MinStillnessCorrectionTime = (float)conf.calibration_correction_time;
    }

    bool disconnected = false;

    while (!disconnected) {
        // Poll SDL events for disconnect detection.
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED && e.gdevice.which == gamepad_id) {
                disconnected = true;
            }
        }
        if (disconnected) break;

        // Non-blocking read of raw HID for button/trigger state.
        // Stale buf is fine when no report arrives — DetermineMode and
        // trigger slowdown only need recent values on state transitions.
        SDL_hid_read_timeout(hid_in, buf, 64, 0);

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

            auto calMode = static_cast<GamepadMotionHelpers::CalibrationMode>(conf.calibration_mode);
            state.motion.SetCalibrationMode(calMode);
            state.motion.Settings.MinStillnessCorrectionTime = (float)conf.calibration_correction_time;
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
        // Gyro: rad/s → °/s, then bias-corrected by EstimateBias.
        // Accel: m/s² → G (/ 9.80665).
        float rY = gyro[1] * 57.2958f;
        float rP = gyro[0] * 57.2958f;
        float rR = gyro[2] * 57.2958f;
        float aX = accel[0] / 9.80665f;
        float aY = accel[1] / 9.80665f;
        float aZ = (accel[2] / 9.80665f) + conf.accel_z_offset;

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
            int trig_val = buf[p->trigger_slowdown_byte];
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
        EstimateBias(rY, rP, rR, dt, state, mode);
        state.motion.ProcessMotion(rY, rP, rR, aX, aY, aZ, (float)dt);

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

    const char* best_path = nullptr;
    int best_bytes = -1;
    for (auto cur = devs; cur; cur = cur->next) {
        SDL_hid_device* test = SDL_hid_open_path(cur->path);
        if (!test) continue;
        SDL_hid_set_nonblocking(test, 1);
        unsigned char tmp[64];
        int res = SDL_hid_read_timeout(test, tmp, 64, 10);
        if (res > best_bytes) {
            best_bytes = res;
            best_path = cur->path;
        }
        SDL_hid_close(test);
    }
    SDL_hid_free_enumeration(devs);

    if (!best_path) return nullptr;
    return SDL_hid_open_path(best_path);
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

        // Wait for the Vader 4 Pro to appear.
        while (!gamepad && runtime.app_running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                    gamepad = SDL_OpenGamepad(e.gdevice.which);
                    break;
                }
            }
            if (!gamepad) SDL_Delay(100);
        }
        if (!gamepad) break;

        SDL_JoystickID gamepad_id = SDL_GetGamepadID(gamepad);

        // Enable sensors on the gamepad.
        SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true);
        SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_ACCEL, true);

        // Open raw HID for button/trigger parsing and LED/rumble output.
        SDL_hid_device* hid_in = OpenHidInput();
        SDL_hid_device* hid_out = SDL_hid_open(0x04B4, 0x2412, nullptr);
        if (!hid_in || !hid_out) {
            printf("Failed to open raw HID\n");
            SDL_CloseGamepad(gamepad);
            if (hid_in) SDL_hid_close(hid_in);
            if (hid_out) SDL_hid_close(hid_out);
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
