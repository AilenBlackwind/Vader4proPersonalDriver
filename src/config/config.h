#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Alternate button mapping used when mode selection is not driven by the trigger.
struct AltButton {
    int byte_index = 0;
    int mask = 0;
    int mode = 1;
};

// Per-profile gyro tuning, movement shaping, and LED color settings.
struct GyroProfile {
    double real_world_sens = 1.0;
    double dots_per_360 = 4000.0;
    float tilt_axis_angle = 15.0f;
    float vertical_scale = 1.0f;
    float roll_weight = 1.0f;
    float yaw_weight = 1.0f;
    bool invert_roll = true;
    double precision_threshold = 0.0;
    double precision_power = 2.0;
    double roll_deadzone = 0.008;
    double gyro_deadzone = 0.0;
    double roll_precision_threshold = 0.0;
    double roll_precision_power = 2.5;
    double accel_rate = 0.0;
    double accel_threshold = 0.5;
    double accel_limit = 2.0;
    bool joystick_mode = false;
    bool joy_overlay_enabled = false;
    double joy_deadzone = 1.5;
    double joy_exponent = 2.0;
    double joy_max_speed = 1.0;
    double joy_input_deadzone = 1.0;
    double joy_input_rate = 3.0;
    double joy_centering = 2.0;
    double joy_tilt_limit = 1.0;
    double joy_vertical_scale = 0.5;
    double joy_filter_cutoff = 1.5;
    double joy_filter_beta = 0.05;
    double joy_auto_recenter = 0.5;
    double joy_roll_weight = 0.0;
    double smooth_speed_threshold = 50.0;
    double smooth_max_strength = 0.3;
    double smooth_lerp_speed = 5.0;
    bool recovery_enabled = true;
    bool smoothing_enabled = false;
    double smoothing_time = 0.05;
    double smoothing_threshold = 10.0;
    double smoothing_strength = 1.0;
    float angle_snap_degrees = 0.0f;
    bool angle_snap_ease = false;
    float decel_brake_strength = 0.0f;
    float decel_brake_threshold = 5.0f;
    float bounce_back_threshold = 2.0f;
    float bounce_back_peak_threshold = 60.0f;
    float bounce_back_strength = 0.8f;
    float bounce_back_time = 0.006f;
    float bounce_back_delay = 0.010f;
    double one_euro_min_cutoff = 6.0;
    double one_euro_beta = 0.3;
    double roll_euro_min_cutoff = 2.0;
    double roll_euro_beta = 0.5;
    bool trigger_slowdown_enabled = false;
    float trigger_slowdown_amount = 0.5f;
    int trigger_slowdown_threshold = 200;
    int trigger_slowdown_byte = 24;
    float trigger_slowdown_duration = 0.0f;
    float trigger_roll_suppression_ms = 0.0f;
    int led_r = 0;
    int led_g = 0;
    int led_b = 0;
    double led_brightness = 1.0;
};

// Top-level runtime config loaded from JSON and hot-swapped by the watcher thread.
struct Config {
    float accel_z_offset = 0.0f;
    int trigger_threshold_low = 10;
    int trigger_threshold_high = 200;
    bool test_keys_enabled = false;
    int key_360 = 0x78;
    int key_180 = 0x79;
    bool manual_mode_keys_enabled = false;
    int key_mode_0 = 0;
    int key_mode_1 = 0;
    int key_mode_2 = 0;
    int calibration_mode = 3;
    double calibration_correction_time = 2.0;
    double calibration_stillness_time = 0.5;
    double calibration_gyro_delta = 2.5;
    double calibration_accel_delta = 0.04;
    bool mode2_rumble_enabled = true;
    float mode2_rumble_interval = 0.5f;
    float mode2_rumble_duration = 0.01f;
    int mode2_rumble_intensity = 10;
    int calibration_key = 0;
    std::string mode_source = "trigger";
    int mode_trigger_byte = 23;
    int mode_button_byte = 9;
    int mode_button_mask = 16;
    bool mode_toggle = false;
    std::vector<AltButton> mode_alt_buttons;
    GyroProfile low_profile;
    GyroProfile high_profile;
};

// Resolves the active config path from master_config.json and foreground window.
std::string ResolveActiveConfigPath();

// Loads one JSON config file into a full Config object.
bool LoadConfig(const std::string& path, Config& c);

// Background poller that reloads master/default/app-specific configs on change.
void ConfigWatcherThread(std::mutex& config_mutex,
                         Config& active_config,
                         std::atomic<bool>& config_changed,
                         std::atomic<bool>& app_running);
