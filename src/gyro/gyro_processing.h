#pragma once

#include "../config/config.h"
#include "../../GamepadMotionHelpers-main/GamepadMotion.hpp"
#include "filters.h"

// Runtime-only gyro state: accumulated motion, motion helpers, and mode memory.
struct GyroState {
    double accX = 0;
    double accY = 0;
    double absT = 0;
    double tiltY = 0;
    double tiltP = 0;
    OneEuroFilter joyFY;
    OneEuroFilter joyFP;
    OneEuroFilter gyroFX;  // main gyro output (pitch/horizontal)
    OneEuroFilter gyroFP;  // main gyro output (roll/vertical)
    OneEuroFilter gyroFR;  // roll-axis gyro output
    int prevMode = -1;
    bool k360_down = false;
    bool k180_down = false;
    bool cal_key_down = false;
    double lostY = 0;
    double lostP = 0;
    double smoothDY = 0;
    double smoothDP = 0;
    double smooth_factor = 1.0;
    bool toggle_state = false;
    bool prev_toggle_button = false;
    float trigger_slowdown_timer = 0.0f;
    bool trigger_slowdown_prev_active = false;
    GamepadMotion motion;

    // Bias estimator (replicates original HandleCalibration).
    bool bias_initialized = false;
    bool bias_ready = false;
    float stillT = 0.f;
    int stillSamples = 0;
    double stillSumY = 0.0, stillSumP = 0.0, stillSumR = 0.0;
    float gyroBiasY = 0.f, gyroBiasP = 0.f, gyroBiasR = 0.f;
    float biasTolerance = 0.5f;     // °/s — max gyro deviation from bias (used post-ready)
    float biasRawTolerance = 5.f;   // °/s — max raw gyro magnitude before bias_ready
    float biasCollectionTime = 0.5f; // seconds of stillness before updating

    // Deceleration brake state
    struct DecelSample { double ts; float mag; };
    DecelSample decelHistory[64];
    int decelHistoryHead = 0;
    int decelHistoryCount = 0;
    float decelBrakeEngagement = 0.f;
    float decelBrakeOmegaRaw = 0.f;
};

// Manual test helper for fixed-angle turns.
void PerformRotation(double deg, const GyroProfile& p);

// Running-average bias estimator (replicates original HandleCalibration).
// Subtracts estimated bias in place. Only refines bias during mode 0 (gyro off).
void EstimateBias(float& rY, float& rP, float& rR, double dt, GyroState& s, int mode);

// Converts calibrated gyro input into mouse motion for the active profile.
void ApplyGyroMovement(float rY,
                       float rP,
                       float rR,
                       double dt,
                       GyroState& s,
                       const GyroProfile& p,
                       int mode,
                       float slowdown = 1.0f);
