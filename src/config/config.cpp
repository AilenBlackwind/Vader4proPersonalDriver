#include "config.h"

#include <windows.h>

#include <fstream>
#include <psapi.h>
#include <tlhelp32.h>

#include "../../json.hpp"

using json = nlohmann::json;

namespace {

// Uses the foreground window to pick a per-app config, if one exists.
std::string GetActiveProcessName() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "Unknown";

    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    // Use CreateToolhelp32Snapshot instead of OpenProcess.
    // OpenProcess may fail for protected processes (anti-cheat, etc).
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return "Unknown";

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    std::string result = "Unknown";

    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                int len = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, NULL, 0, NULL, NULL);
                if (len > 0) {
                    result.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, &result[0], len, NULL, NULL);
                }
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return result;
}

FILETIME GetFileWriteTime(const std::string& path) {
    FILETIME ft = {0};
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) {
        ft = data.ftLastWriteTime;
    }
    return ft;
}

void LoadProfile(const json& j, GyroProfile& p) {
    p.real_world_sens = j.value("real_world_sens", p.real_world_sens);
    p.dots_per_360 = j.value("dots_per_360", p.dots_per_360);
    p.tilt_axis_angle = j.value("tilt_axis_angle", p.tilt_axis_angle);
    p.vertical_scale = j.value("vertical_scale", p.vertical_scale);
    p.roll_weight = j.value("roll_weight", p.roll_weight);
    p.yaw_weight = j.value("yaw_weight", p.yaw_weight);
    p.invert_roll = j.value("invert_roll", p.invert_roll);
    p.precision_threshold = j.value("precision_threshold", p.precision_threshold);
    p.precision_power = j.value("precision_power", p.precision_power);
    p.roll_deadzone = j.value("roll_deadzone", p.roll_deadzone);
    p.gyro_deadzone = j.value("gyro_deadzone", p.gyro_deadzone);
    p.roll_precision_threshold = j.value("roll_precision_threshold", p.roll_precision_threshold);
    p.roll_precision_power = j.value("roll_precision_power", p.roll_precision_power);
    p.accel_rate = j.value("accel_rate", p.accel_rate);
    p.accel_threshold = j.value("accel_threshold", p.accel_threshold);
    p.accel_limit = j.value("accel_limit", p.accel_limit);
    p.joystick_mode = j.value("joystick_mode", p.joystick_mode);
    p.joy_overlay_enabled = j.value("joy_overlay_enabled", p.joy_overlay_enabled);
    p.joy_deadzone = j.value("joy_deadzone", p.joy_deadzone);
    p.joy_exponent = j.value("joy_exponent", p.joy_exponent);
    p.joy_max_speed = j.value("joy_max_speed", p.joy_max_speed);
    p.joy_input_deadzone = j.value("joy_input_deadzone", p.joy_input_deadzone);
    p.joy_input_rate = j.value("joy_input_rate", p.joy_input_rate);
    p.joy_centering = j.value("joy_centering", p.joy_centering);
    p.joy_tilt_limit = j.value("joy_tilt_limit", p.joy_tilt_limit);
    p.joy_vertical_scale = j.value("joy_vertical_scale", p.joy_vertical_scale);
    p.joy_filter_cutoff = j.value("joy_filter_cutoff", p.joy_filter_cutoff);
    p.joy_filter_beta = j.value("joy_filter_beta", p.joy_filter_beta);
    p.joy_auto_recenter = j.value("joy_auto_recenter", p.joy_auto_recenter);
    p.joy_roll_weight = j.value("joy_roll_weight", p.joy_roll_weight);
    p.smooth_speed_threshold = j.value("smooth_speed_threshold", p.smooth_speed_threshold);
    p.smooth_max_strength = j.value("smooth_max_strength", p.smooth_max_strength);
    p.smooth_lerp_speed = j.value("smooth_lerp_speed", p.smooth_lerp_speed);
    p.recovery_enabled = j.value("recovery_enabled", p.recovery_enabled);
    p.smoothing_enabled = j.value("smoothing_enabled", p.smoothing_enabled);
    p.smoothing_time = j.value("smoothing_time", p.smoothing_time);
    p.smoothing_threshold = j.value("smoothing_threshold", p.smoothing_threshold);
    p.angle_snap_degrees = j.value("angle_snap_degrees", p.angle_snap_degrees);
    p.angle_snap_ease = j.value("angle_snap_ease", p.angle_snap_ease);
    p.decel_brake_strength = j.value("decel_brake_strength", p.decel_brake_strength);
    p.decel_brake_threshold = j.value("decel_brake_threshold", p.decel_brake_threshold);
    p.one_euro_min_cutoff = j.value("one_euro_min_cutoff", p.one_euro_min_cutoff);
    p.one_euro_beta = j.value("one_euro_beta", p.one_euro_beta);
    p.roll_euro_min_cutoff = j.value("roll_euro_min_cutoff", p.roll_euro_min_cutoff);
    p.roll_euro_beta = j.value("roll_euro_beta", p.roll_euro_beta);
    p.trigger_slowdown_enabled = j.value("trigger_slowdown_enabled", p.trigger_slowdown_enabled);
    p.trigger_slowdown_amount = j.value("trigger_slowdown_amount", p.trigger_slowdown_amount);
    p.trigger_slowdown_threshold = j.value("trigger_slowdown_threshold", p.trigger_slowdown_threshold);
    p.trigger_slowdown_byte = j.value("trigger_slowdown_byte", p.trigger_slowdown_byte);
    p.trigger_slowdown_duration = j.value("trigger_slowdown_duration", p.trigger_slowdown_duration);
    p.led_r = j.value("led_r", p.led_r);
    p.led_g = j.value("led_g", p.led_g);
    p.led_b = j.value("led_b", p.led_b);
    p.led_brightness = j.value("led_brightness", p.led_brightness);
}

}  // namespace

std::string ResolveActiveConfigPath() {
    std::string exe = GetActiveProcessName();
    std::ifstream f("master_config.json");
    if (!f.is_open()) return "default.json";
    try {
        json master = json::parse(f, nullptr, true, true);
        std::string default_path = master.value("default", "default.json");
        if (master.contains("apps") && master["apps"].contains(exe)) {
            return master["apps"][exe].get<std::string>();
        }
        return default_path;
    } catch (...) {
        return "default.json";
    }
}

bool LoadConfig(const std::string& path, Config& c) {
    // Configs are loaded wholesale so callers can swap them in atomically.
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        json j = json::parse(f, nullptr, true, true);
        c.accel_z_offset = j.value("accel_z_offset", c.accel_z_offset);
        c.trigger_threshold_low = j.value("trigger_threshold_low", c.trigger_threshold_low);
        c.trigger_threshold_high = j.value("trigger_threshold_high", c.trigger_threshold_high);
        c.test_keys_enabled = j.value("test_keys_enabled", c.test_keys_enabled);
        c.key_360 = j.value("key_360", c.key_360);
        c.key_180 = j.value("key_180", c.key_180);
        c.manual_mode_keys_enabled = j.value("manual_mode_keys_enabled", c.manual_mode_keys_enabled);
        c.key_mode_0 = j.value("key_mode_0", c.key_mode_0);
        c.key_mode_1 = j.value("key_mode_1", c.key_mode_1);
        c.key_mode_2 = j.value("key_mode_2", c.key_mode_2);
        c.calibration_mode = j.value("calibration_mode", c.calibration_mode);
        c.calibration_correction_time = j.value("calibration_correction_time", c.calibration_correction_time);
        c.calibration_key = j.value("calibration_key", c.calibration_key);
        c.mode_source = j.value("mode_source", c.mode_source);
        c.mode_trigger_byte = j.value("mode_trigger_byte", c.mode_trigger_byte);
        c.mode_button_byte = j.value("mode_button_byte", c.mode_button_byte);
        c.mode_button_mask = j.value("mode_button_mask", c.mode_button_mask);
        c.mode_toggle = j.value("mode_toggle", c.mode_toggle);
        c.mode_alt_buttons.clear();
        if (j.contains("mode_alt_buttons")) {
            for (auto& ab : j["mode_alt_buttons"]) {
                c.mode_alt_buttons.push_back(
                    {ab.value("byte", 0), ab.value("mask", 0), ab.value("mode", 1)});
            }
        }
        if (j.contains("low_profile")) LoadProfile(j["low_profile"], c.low_profile);
        if (j.contains("high_profile")) LoadProfile(j["high_profile"], c.high_profile);
        return true;
    } catch (...) {
        return false;
    }
}

void ConfigWatcherThread(std::mutex& config_mutex,
                         Config& active_config,
                         std::atomic<bool>& config_changed,
                         std::atomic<bool>& app_running) {
    // Polling is intentionally simple here: it keeps hot-reload predictable
    // without introducing another Windows-specific watcher dependency.
    std::string lastFile;
    FILETIME lastWT = {0};
    FILETIME lastMWT = {0};
    json master;
    bool first_run = true;

    while (app_running) {
        if (!first_run) Sleep(1000);
        first_run = false;

        FILETIME cmFT = GetFileWriteTime("master_config.json");
        if (CompareFileTime(&cmFT, &lastMWT) != 0) {
            std::ifstream f("master_config.json");
            if (f.is_open()) {
                try {
                    master = json::parse(f, nullptr, true, true);
                    lastMWT = cmFT;
                } catch (...) {
                }
            }
        }

        std::string exe = GetActiveProcessName();
        std::string default_path = master.value("default", "default.json");
        std::string target =
            (master.contains("apps") && master["apps"].contains(exe))
                ? master["apps"][exe].get<std::string>()
                : default_path;

        FILETIME cFT = GetFileWriteTime(target);
        bool file_changed = CompareFileTime(&cFT, &lastWT) != 0;
        if (target != lastFile || file_changed) {
            Config tmp;
            if (LoadConfig(target, tmp)) {
                std::lock_guard<std::mutex> lock(config_mutex);
                active_config = tmp;
                lastFile = target;
                lastWT = cFT;
                config_changed = true;
            }
        }
    }
}
