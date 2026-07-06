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

    inputY = s.fY.filter(inputY, s.absT);
    inputP = s.fP.filter(inputP, s.absT);
    inputR = s.fR.filter(inputR, s.absT);

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

void HandleCalibration(float rY,
                       float rP,
                       float rR,
                       float aX,
                       float aY,
                       float aZ,
                       double dt,
                       GyroState& s,
                       const Config& c,
                       int mode) {
    // Keeps gyro bias stable while the controller is considered "still".
    if (!s.initialized) {
        s.bY = rY;
        s.bP = rP;
        s.bR = rR;
        s.base_aX = aX;
        s.base_aY = aY;
        s.base_aZ = aZ;
        s.initialized = true;
    }

    float g_diff = s.bias_ready
        ? sqrtf(powf(rY - (float)s.bY, 2) + powf(rP - (float)s.bP, 2) + powf(rR - (float)s.bR, 2))
        : sqrtf(rY * rY + rP * rP + rR * rR);
    float a_diff = sqrtf(powf(aX - s.base_aX, 2) + powf(aY - s.base_aY, 2) + powf(aZ - s.base_aZ, 2));
    bool gyro_still = g_diff <= c.autocalib_tolerance;
    bool accel_still = a_diff <= c.accel_still_threshold;

    if (!gyro_still || !accel_still) {
        s.stillT = 0;
        s.stillSamples = 0;
        s.stillSumY = s.stillSumP = s.stillSumR = 0;
        s.base_aX = aX;
        s.base_aY = aY;
        s.base_aZ = aZ;
        return;
    }

    s.stillT += dt;
    s.stillSumY += rY;
    s.stillSumP += rP;
    s.stillSumR += rR;
    s.stillSamples++;

    s.base_aX += (aX - s.base_aX) * 0.05f;
    s.base_aY += (aY - s.base_aY) * 0.05f;
    s.base_aZ += (aZ - s.base_aZ) * 0.05f;

    if (s.stillSamples <= 0) return;

    double avgY = s.stillSumY / s.stillSamples;
    double avgP = s.stillSumP / s.stillSamples;
    double avgR = s.stillSumR / s.stillSamples;

    if (s.stillT >= c.autocalib_time) {
        if (!s.bias_ready) {
            s.bY = avgY;
            s.bP = avgP;
            s.bR = avgR;
            s.bias_ready = true;
        } else {
            double blend = 1.0 - exp(-dt * 1.5);
            s.bY += (avgY - s.bY) * blend;
            s.bP += (avgP - s.bP) * blend;
            s.bR += (avgR - s.bR) * blend;
        }

        s.stillSumY = avgY;
        s.stillSumP = avgP;
        s.stillSumR = avgR;
        s.stillSamples = 1;
        s.stillT = c.autocalib_time;
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
        s.fY.filter(0, s.absT);
        s.fP.filter(0, s.absT);
        s.fR.filter(0, s.absT);
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

    double rawY = rY - s.bY;
    double rawP = rP - s.bP;
    double rawR = rR - s.bR;
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
