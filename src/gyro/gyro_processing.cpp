#include "gyro_processing.h"

#include <windows.h>

#include <cmath>

#include "joystick_mode.h"

namespace {

void ApplyDirectGyroMode(double rawY,
                         double rawP,
                         double rawR,
                         double dt,
                         GyroState& s,
                         const GyroProfile& p,
                         double& inputY,
                         double& inputP,
                         double& inputR,
                         double& accel) {
    if (fabs(rawY) < p.gyro_deadzone) rawY = 0;
    if (fabs(rawP) < p.gyro_deadzone) rawP = 0;

    double absR = fabs(rawR);
    double rollPrecisionThreshold = fmax(p.roll_precision_threshold, p.roll_deadzone);
    if (absR < p.roll_deadzone) {
        inputR = 0;
    } else if (rollPrecisionThreshold > p.roll_deadzone && absR < rollPrecisionThreshold) {
        inputR = (rawR > 0 ? 1 : -1) *
                 pow((absR - p.roll_deadzone) / (rollPrecisionThreshold - p.roll_deadzone),
                     p.roll_precision_power) *
                 rollPrecisionThreshold;
    } else {
        inputR = rawR;
    }

    double rawMagYP = sqrt(rawY * rawY + rawP * rawP);
    if (rawMagYP > 0.0 && rawMagYP < p.precision_threshold) {
        double f = pow(rawMagYP / p.precision_threshold, p.precision_power);
        inputY = rawY * f;
        inputP = rawP * f;
        if (p.recovery_enabled) {
            s.lostY += (rawY - inputY) * dt * p.recovery_rate;
            s.lostP += (rawP - inputP) * dt * p.recovery_rate;
        }
    } else {
    inputY = rawY;
    inputP = rawP;
    if (p.recovery_enabled && rawMagYP > 0.0) {
        double rY = fmin(fabs(s.lostY), fabs(inputY)) * (s.lostY > 0 ? 1 : -1);
        double rP = fmin(fabs(s.lostP), fabs(inputP)) * (s.lostP > 0 ? 1 : -1);
        inputY += rY;
        inputP += rP;
        s.lostY -= rY;
        s.lostP -= rP;
    }
}

double fMag = sqrt(inputY * inputY + inputP * inputP + inputR * inputR);
    if (p.accel_rate > 0 && fMag > p.accel_threshold) {
        accel = fmin(p.accel_limit, 1.0 + (fMag - p.accel_threshold) * p.accel_rate);
    }
}

}  // namespace

// Helper for manual testing: applies an instant mouse turn using profile sensitivity.
void PerformRotation(double deg, const GyroProfile& p) {
    int move = (int)std::round(deg * (p.dots_per_360 / 360.0));
    INPUT in = {INPUT_MOUSE};
    in.mi.dx = move;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

void EstimateBias(float& rY, float& rP, float& rR, double dt, GyroState& s, int mode) {
    if (!s.bias_initialized) {
        s.gyroBiasY = rY;
        s.gyroBiasP = rP;
        s.gyroBiasR = rR;
        s.stillT = 0.f;
        s.stillSamples = 0;
        s.stillSumY = s.stillSumP = s.stillSumR = 0.0;
        s.bias_initialized = true;
    }

    float rawY = rY, rawP = rP, rawR = rR;

    // Two-phase stillness detection: before bias_ready use raw gyro magnitude;
    // after bias_ready use deviation from established bias.
    float g_diff;
    if (!s.bias_ready) {
        g_diff = sqrtf(rawY * rawY + rawP * rawP + rawR * rawR);
        s.gyroBiasY = 0.f;
        s.gyroBiasP = 0.f;
        s.gyroBiasR = 0.f;
    } else {
        float dY = rawY - s.gyroBiasY;
        float dP = rawP - s.gyroBiasP;
        float dR = rawR - s.gyroBiasR;
        g_diff = sqrtf(dY * dY + dP * dP + dR * dR);
    }

    // Always subtract current bias.
    rY -= s.gyroBiasY;
    rP -= s.gyroBiasP;
    rR -= s.gyroBiasR;

    float tol = s.bias_ready ? s.biasTolerance : s.biasRawTolerance;
    if (g_diff > tol) {
        s.stillT = 0.f;
        s.stillSamples = 0;
        s.stillSumY = s.stillSumP = s.stillSumR = 0.0;
        return;
    }

    s.stillT += (float)dt;
    s.stillSumY += rawY;
    s.stillSumP += rawP;
    s.stillSumR += rawR;
    s.stillSamples++;

    if (s.stillT >= s.biasCollectionTime && s.stillSamples > 0) {
        double avgY = s.stillSumY / s.stillSamples;
        double avgP = s.stillSumP / s.stillSamples;
        double avgR = s.stillSumR / s.stillSamples;

        if (!s.bias_ready) {
            s.gyroBiasY = (float)avgY;
            s.gyroBiasP = (float)avgP;
            s.gyroBiasR = (float)avgR;
            s.bias_ready = true;
        } else if (mode == 0) {
            // Only refine bias when gyro is off (controller at rest).
            double blend = 1.0 - exp(-dt * 1.5);
            s.gyroBiasY += (float)((avgY - s.gyroBiasY) * blend);
            s.gyroBiasP += (float)((avgP - s.gyroBiasP) * blend);
            s.gyroBiasR += (float)((avgR - s.gyroBiasR) * blend);
        }

        s.stillSumY = avgY;
        s.stillSumP = avgP;
        s.stillSumR = avgR;
        s.stillSamples = 1;
        s.stillT = s.biasCollectionTime;
    }
}

void ApplyGyroMovement(float rY,
                       float rP,
                       float rR,
                       double dt,
                       GyroState& s,
                       const GyroProfile& p,
                       int mode,
                       float slowdown) {
    // Core gyro-to-mouse path. Small changes here can strongly affect feel.
    if (mode == 0) {
        s.joyFY.filter(0, s.absT);
        s.joyFP.filter(0, s.absT);
        s.accX = 0;
        s.accY = 0;
        s.tiltY = 0;
        s.tiltP = 0;
        s.lostY = 0;
        s.lostP = 0;
        s.smooth_factor = 1.0;
        return;
    }

    float rawY, rawP, rawR;
    s.motion.GetCalibratedGyro(rawY, rawP, rawR);
    // SDL3 gyro Y (pitch) and X (roll) are negated relative to the old HID
    // convention that users' profiles were tuned for.
    rawY = -rawY;
    rawP = -rawP;
    double inputY = 0;
    double inputP = 0;
    double inputR = 0;
    double accel = 1.0;

    bool pureJoystick = p.joystick_mode && !p.joy_overlay_enabled;
    bool joystickActive = p.joystick_mode;

    if (pureJoystick) {
        ApplyJoystickMode(rawY, rawP, rawR, dt, s, p, inputY, inputP, true);
    } else {
        ApplyDirectGyroMode(rawY, rawP, rawR, dt, s, p, inputY, inputP, inputR, accel);

        if (p.joystick_mode && p.joy_overlay_enabled) {
            double stickY = 0.0;
            double stickP = 0.0;
            ApplyJoystickMode(rawY, rawP, rawR, dt, s, p, stickY, stickP, false);
            inputY += stickY;
            inputP += stickP;
        }

        if (!joystickActive && p.smooth_max_strength < 1.0) {
            double spd = sqrt(inputY * inputY + inputP * inputP);
            double trg = (spd < p.smooth_speed_threshold && spd > 0)
                             ? p.smooth_max_strength +
                                   (1.0 - p.smooth_max_strength) * (spd / p.smooth_speed_threshold)
                             : 1.0;
            s.smooth_factor += (trg - s.smooth_factor) * (1.0 - exp(-p.smooth_lerp_speed * dt));
            inputY *= s.smooth_factor;
            inputP *= s.smooth_factor;
        }
    }

    if (p.invert_roll) inputR = -inputR;
    double rad = p.tilt_axis_angle * M_PI / 180.0;
    double hor = ((inputY * cos(rad)) + (inputR * sin(rad))) * p.yaw_weight +
                 ((inputR * cos(rad)) - (inputY * sin(rad))) * p.roll_weight;
    double sens = (p.real_world_sens * p.dots_per_360 / 360.0) * accel * slowdown;

    s.accX += (hor * dt) * sens;
    s.accY += (inputP * dt) * sens * p.vertical_scale;

    int mx = (int)std::round(s.accX);
    int my = (int)std::round(s.accY);
    if (mx != 0 || my != 0) {
        INPUT in = {INPUT_MOUSE};
        in.mi.dx = mx;
        in.mi.dy = my;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &in, sizeof(INPUT));
        s.accX -= mx;
        s.accY -= my;
    } else if (!joystickActive) {
        s.accX *= exp(-5.0 * dt);
        s.accY *= exp(-5.0 * dt);
    }
}
