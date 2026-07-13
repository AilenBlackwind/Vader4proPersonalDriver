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
    if (p.smoothing_enabled && p.smoothing_threshold > 0.0) {
        double x = fmin(rawMagYP / p.smoothing_threshold, 1.0);
        double g = (1.0 - x) * (1.0 - x);
        double effT = p.smoothing_time * g;
        double alpha = (effT <= 1e-6) ? 1.0 : (1.0 - exp(-dt / effT));
        double tY = rawY * g, tP = rawP * g;
        s.smoothDY += alpha * (tY - s.smoothDY);
        s.smoothDP += alpha * (tP - s.smoothDP);
        inputY = s.smoothDY + rawY * (1.0 - g);
        inputP = s.smoothDP + rawP * (1.0 - g);
    } else {
        double f = 1.0;
        if (rawMagYP > 0.0 && rawMagYP < p.precision_threshold) {
            f = pow(rawMagYP / p.precision_threshold, p.precision_power);
        }
        inputY = rawY * f;
        inputP = rawP * f;

        if (p.recovery_enabled && rawMagYP > 0.0) {
            double rr = p.precision_power * 0.5;
            if (f < 1.0) {
                s.lostY += (rawY - inputY) * dt * rr;
                s.lostP += (rawP - inputP) * dt * rr;
            } else {
                double decay = 1.0 - exp(-dt * rr);
                double rY = fmin(fabs(s.lostY), fabs(inputY) * decay) * (s.lostY > 0 ? 1 : -1);
                double rP = fmin(fabs(s.lostP), fabs(inputP) * decay) * (s.lostP > 0 ? 1 : -1);
                inputY += rY;
                inputP += rP;
                s.lostY -= rY;
                s.lostP -= rP;
            }
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
        s.smoothDY = 0;
        s.smoothDP = 0;
        s.smooth_factor = 1.0;
        s.decelHistoryCount = 0;
        s.decelHistoryHead = 0;
        s.decelBrakeEngagement = 0.f;
        s.decelBrakeOmegaRaw = 0.f;
        s.gyroFX.filter(0, s.absT);
        s.gyroFP.filter(0, s.absT);
        s.gyroFR.filter(0, s.absT);
        return;
    }

    float rawY, rawP, rawR;
    s.motion.GetCalibratedGyro(rawY, rawP, rawR);
    // SDL3 gyro Y (pitch) and X (roll) are negated relative to the old HID
    // convention that users' profiles were tuned for.
    rawY = -rawY;
    rawP = -rawP;

    // Capture pre-curve magnitude for deceleration brake.
    s.decelBrakeOmegaRaw = sqrtf(rawY * rawY + rawP * rawP);
    {
        auto& h = s.decelHistory[s.decelHistoryHead];
        h.ts = s.absT;
        h.mag = s.decelBrakeOmegaRaw;
        s.decelHistoryHead = (s.decelHistoryHead + 1) % 64;
        if (s.decelHistoryCount < 64) s.decelHistoryCount++;
    }

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

    if (!pureJoystick) {
        inputY = s.gyroFX.filter(inputY, s.absT);
        inputP = s.gyroFP.filter(inputP, s.absT);
        inputR = s.gyroFR.filter(inputR, s.absT);

        if (p.angle_snap_degrees > 0.f && (inputY != 0.0 || inputP != 0.0)) {
            double snapRad = p.angle_snap_degrees * M_PI / 180.0;
            double refAngle = (inputY == 0.0) ? M_PI_2 : atan(fabs(inputP / inputY));
            double mag = sqrt(inputY * inputY + inputP * inputP);
            if (p.angle_snap_ease) {
                auto smoothstep = [](double x) -> double {
                    x = fmin(fmax(x, 0.0), 1.0);
                    return x * x * (3.0 - 2.0 * x);
                };
                if (refAngle > M_PI_2 - snapRad) {
                    double t = 1.0 - ((M_PI_2 - refAngle) / snapRad);
                    double snap = smoothstep(t);
                    inputY *= (1.0 - snap);
                    inputP = copysign(mag, inputP);
                } else if (refAngle < snapRad) {
                    double t = 1.0 - (refAngle / snapRad);
                    double snap = smoothstep(t);
                    inputP *= (1.0 - snap);
                    inputY = copysign(mag, inputY);
                }
            } else {
                if (refAngle > M_PI_2 - snapRad) {
                    inputY = 0.0;
                    inputP = copysign(mag, inputP);
                } else if (refAngle < snapRad) {
                    inputP = 0.0;
                    inputY = copysign(mag, inputY);
                }
            }
        }
    }

    if (p.invert_roll) inputR = -inputR;
    double rad = p.tilt_axis_angle * M_PI / 180.0;
    double hor = ((inputY * cos(rad)) + (inputR * sin(rad))) * p.yaw_weight +
                 ((inputR * cos(rad)) - (inputY * sin(rad))) * p.roll_weight;
    double sens = (p.real_world_sens * p.dots_per_360 / 360.0) * accel * slowdown;

    // Deceleration brake: detects rapid gyro deceleration and dampens it
    // to reduce overshoot when stopping.
    float decelMul = 1.f;
    if (p.decel_brake_strength > 0.f && s.decelHistoryCount >= 10) {
        constexpr float kWindowSec = 0.010f;
        constexpr float kSpeedMin = 2.f;
        constexpr float kSpeedMax = 60.f;
        constexpr float kEngageTime = 0.010f;
        constexpr float kReleaseTime = 0.006f;
        constexpr float kFullBrakeFactor = 3.5f;

        float omega = s.decelBrakeOmegaRaw;
        float sOld = omega;
        double targetTs = s.absT - kWindowSec;
        int idx = s.decelHistoryHead;
        for (int i = 0; i < s.decelHistoryCount; i++) {
            idx = (idx - 1 + 64) % 64;
            if (s.decelHistory[idx].ts <= targetTs) {
                sOld = s.decelHistory[idx].mag;
                break;
            }
        }

        float slope = (omega - sOld) / kWindowSec;
        float dNeg = fmaxf(0.f, -slope);
        float d0 = p.decel_brake_threshold / kWindowSec;
        float d1 = d0 * kFullBrakeFactor;
        float denom = (d1 > d0) ? (d1 - d0) : 1e-6f;

        float bInst = 0.f;
        bool speedGate = (omega >= kSpeedMin && omega <= kSpeedMax);
        if (speedGate) {
            bInst = fminf(1.f, fmaxf(0.f, (dNeg - d0) / denom));
        }

        if (speedGate && bInst > 0.f) {
            s.decelBrakeEngagement += (float)(dt / kEngageTime) * bInst;
        } else {
            s.decelBrakeEngagement -= (float)(dt / kReleaseTime);
        }
        s.decelBrakeEngagement = fminf(1.f, fmaxf(0.f, s.decelBrakeEngagement));

        if (s.decelBrakeEngagement > 0.f) {
            decelMul = 1.f - p.decel_brake_strength * s.decelBrakeEngagement;
        }
    }

    hor *= decelMul;
    inputP *= decelMul;

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
