#include "app.h"

#include <windows.h>

#include <thread>

#include "../../hidapi.h"
#include "../config/config.h"
#include "../gyro/gyro_processing.h"
#include "../gyro/profile_logic.h"
#include "../haptics/haptic_pipe.h"

namespace {

short read_le(unsigned char lo, unsigned char hi) {
    return (short)(lo | (hi << 8));
}

// Applies filter and LED state when the active profile changes.
void RefreshModeState(hid_device* h, GyroState& state, GyroProfile& profile, int mode,
                      int& cur_r, int& cur_g, int& cur_b) {
    state.fY.updateParams(profile.euro_min_cutoff, profile.euro_beta);
    state.fP.updateParams(profile.euro_min_cutoff, profile.euro_beta);
    state.fR.updateParams(profile.roll_euro_min_cutoff, profile.roll_euro_beta);
    state.joyFY.updateParams(profile.joy_filter_cutoff, profile.joy_filter_beta);
    state.joyFP.updateParams(profile.joy_filter_cutoff, profile.joy_filter_beta);
    state.fY.reset();
    state.fP.reset();
    state.fR.reset();
    state.joyFY.reset();
    state.joyFP.reset();
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
        SendLED(h, 0, 0, 0);
        cur_r = cur_g = cur_b = 0;
    } else {
        SetProfileLED(h, profile, cur_r, cur_g, cur_b);
    }
}

// Runs one connected-device session until the controller disconnects.
void RunConnectedDeviceLoop(hid_device* h, AppRuntime& runtime) {
    int cur_r = -1;
    int cur_g = -1;
    int cur_b = -1;
    const float accScl = 0.03801141343622691f;

    SendLED(h, 0, 0, 0);
    runtime.device_connected = true;
    std::thread haptic_thr(HapticThread, h, std::ref(runtime.haptic_running),
                           std::ref(runtime.device_connected));

    unsigned char buf[64];
    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    GyroState state;
    Config conf;
    {
        std::lock_guard<std::mutex> l(runtime.config_mutex);
        conf = runtime.active_config;
    }

    while (true) {
        int res = hid_read(h, buf, 64);
        if (res < 0) {
            printf("[DISCONNECTED]\n");
            runtime.haptic_running = false;
            runtime.device_connected = false;
            haptic_thr.join();
            hid_close(h);
            break;
        }
        if (res == 0) continue;

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

        float aX = read_le(buf[11], buf[12]) * accScl;
        float aY = read_le(buf[15], buf[16]) * accScl;
        float aZ = (read_le(buf[13], buf[14]) + conf.accel_z_offset) * accScl;

        // Vader 4 Pro uses a non-standard gyro byte layout in its HID report.
        // These indices were verified against working values; do not shift them.
        float rY = (read_le(buf[18], buf[20]) / 512.0f) * 2000.0f;
        float rP = (read_le(buf[26], buf[27]) / 512.0f) * 2000.0f;
        float rR =
            (float)((((double)read_le(buf[29], buf[30]) + 32768.0) / 65535.0 * 2.0) - 1.0) *
            2000.0f;

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
            RefreshModeState(h, state, *p, mode, cur_r, cur_g, cur_b);
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

        HandleCalibration(rY, rP, rR, aX, aY, aZ, dt, state, conf, mode);
        ApplyGyroMovement(rY, rP, rR, dt, state, *p, mode, slowdown);
    }
}

}  // namespace

int RunApp(AppRuntime& runtime) {
    LoadConfig(ResolveActiveConfigPath(), runtime.active_config);
    std::thread watcher_thr(ConfigWatcherThread, std::ref(runtime.config_mutex),
                            std::ref(runtime.active_config), std::ref(runtime.config_changed),
                            std::ref(runtime.app_running));

    while (runtime.app_running) {
        hid_device* h = hid_open(0x04B4, 0x2412, NULL);
        if (!h) {
            Sleep(1000);
            continue;
        }

        hid_set_nonblocking(h, 0);
        printf("\n[CONNECTED] Vader 4 Pro\n");
        RunConnectedDeviceLoop(h, runtime);
    }

    runtime.app_running = false;
    watcher_thr.join();
    return 0;
}
