#pragma once

#include "../config/config.h"
#include "filters.h"

// Runtime-only gyro state: bias, filters, accumulated motion, and mode memory.
struct GyroState {
    double bY = 0;
    double bP = 0;
    double bR = 0;
    float base_aX = 0;
    float base_aY = 0;
    float base_aZ = 0;
    double accX = 0;
    double accY = 0;
    double stillT = 0;
    double absT = 0;
    double tiltY = 0;
    double tiltP = 0;
    OneEuroFilter fY;
    OneEuroFilter fP;
    OneEuroFilter fR;
    int prevMode = -1;
    bool k360_down = false;
    bool k180_down = false;
    bool initialized = false;
    double lostY = 0;
    double lostP = 0;
    double smooth_factor = 1.0;
    bool toggle_state = false;
    bool prev_toggle_button = false;
    bool bias_ready = false;
    double stillSumY = 0;
    double stillSumP = 0;
    double stillSumR = 0;
    int stillSamples = 0;
    float trigger_slowdown_timer = 0.0f;
    bool trigger_slowdown_prev_active = false;
};

// Manual test helper for fixed-angle turns.
void PerformRotation(double deg, const GyroProfile& p);

// Updates gyro bias while the controller is considered stationary.
void HandleCalibration(float rY,
                       float rP,
                       float rR,
                       float aX,
                       float aY,
                       float aZ,
                       double dt,
                       GyroState& s,
                       const Config& c,
                       int mode);

// Converts calibrated gyro input into mouse motion for the active profile.
void ApplyGyroMovement(float rY,
                       float rP,
                       float rR,
                       double dt,
                       GyroState& s,
                       const GyroProfile& p,
                       int mode,
                       float slowdown = 1.0f);
