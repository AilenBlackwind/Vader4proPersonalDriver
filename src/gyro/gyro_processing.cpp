#include "gyro_processing.h"

#include <windows.h>

#include <cmath>

#include "joystick_mode.h"

namespace {

// Debt is an angle (degrees), output is angular speed (degrees/second).
// Only retain a short history: recovery is not a promise to replay every
// suppressed movement after the user's intent has changed.
double RecoverPrecision(double raw, double shaped, double threshold, double dt,
                        bool enabled, double rate, double& debt) {
    if (!enabled || !(rate > 0) || !std::isfinite(rate) || !(dt > 0) ||
        !(threshold > 0) || raw == 0) {
        debt = 0;
        return shaped;
    }
    if (debt * raw < 0) debt = 0;
    const double direction = raw > 0 ? 1.0 : -1.0;
    const double missing = fmax(0.0, fabs(raw) - fabs(shaped));
    const double old = fabs(debt);
    const double fraction = -expm1(-rate * dt);
    // Exact integration for constant input over this sample, including losses
    // generated during the sample. This also recovers during slow tracking.
    double returned = old * fraction + missing * fmax(0.0, dt - fraction / rate);
    // Never add more than the current shaped motion: tiny movements must not
    // release a large old debt. Cap stored loss at 100 ms of threshold motion.
    returned = fmin(returned, fabs(shaped) * dt);
    debt = direction * fmin(threshold * 0.1, fmax(0.0, old + missing * dt - returned));
    return shaped + direction * returned / dt;
}

void ApplyDirectGyroMode(double rawY,
                         double rawP,
                         double rawR,
                         double dt,
                         GyroState& s,
                         const GyroProfile& p,
                         double& inputY,
                         double& inputP,
                         double& inputR) {
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
    inputR = RecoverPrecision(absR <= p.roll_deadzone ? 0.0 : rawR, inputR,
                              rollPrecisionThreshold > p.roll_deadzone ? rollPrecisionThreshold : 0.0,
                              dt, p.recovery_enabled && s.trigRollSuppressTimer <= 0,
                              p.recovery_rate, s.lostR);

    double rawMagYP = sqrt(rawY * rawY + rawP * rawP);
    if (p.smoothing_enabled && p.smoothing_threshold > 0.0) {
        s.lostY = s.lostP = 0;
        double x = fmin(rawMagYP / p.smoothing_threshold, 1.0);
        // Strength < 1 routes a fraction of the signal around the filter;
        // strength > 1 keeps full routing but stretches the filter time.
        double strength = fmax(p.smoothing_strength, 0.0);
        double g = fmin(strength, 1.0) * (1.0 - x) * (1.0 - x);
        double effT = p.smoothing_time * g * fmax(1.0, strength);
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

        inputY = RecoverPrecision(rawY, inputY, p.precision_threshold, dt,
                                  p.recovery_enabled, p.recovery_rate, s.lostY);
        inputP = RecoverPrecision(rawP, inputP, p.precision_threshold, dt,
                                  p.recovery_enabled, p.recovery_rate, s.lostP);
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

void ProcessGyroSensors(float rY, float rP, float rR,
                        float aX, float aY, float aZ, double dt,
                        GyroState& s, int mode, const Config& config) {
    // One calibration authority: feeding a separately bias-corrected signal
    // into a second bias estimator lets its learned offset become stale as
    // the first estimator changes. Manual and automatic calibration now share
    // the library's offset, in native sensor coordinates.
    s.motion.Settings.MinStillnessCorrectionTime = (float)config.calibration_correction_time;
    s.motion.Settings.MinStillnessCollectionTime = (float)config.calibration_stillness_time;
    s.motion.Settings.StillnessMeanTimeConstant = (float)fmax(0.1,
        fmax(config.calibration_correction_time, config.calibration_stillness_time));
    s.motion.Settings.StillnessGyroDelta = (float)config.calibration_gyro_delta;
    s.motion.Settings.StillnessAccelDelta = (float)config.calibration_accel_delta;
    // Variation alone cannot distinguish steady yaw from rest. Bound the
    // ABSOLUTE incoming speed, not deviation from an already learned bias:
    // otherwise an estimator can follow a slow ramp arbitrarily far.
    const double speed = sqrt(double(rY) * rY + double(rP) * rP + double(rR) * rR);
    s.debugAutoCalibrationAllowed = (mode == 0 || config.calibration_while_active) &&
        std::isfinite(speed) && speed <= config.calibration_max_gyro_speed;
    s.motion.SetCalibrationMode(s.debugAutoCalibrationAllowed
        ? static_cast<GamepadMotionHelpers::CalibrationMode>(config.calibration_mode)
        : GamepadMotionHelpers::CalibrationMode::Manual);
    // Gyro and acceleration must use the same basis for sensor fusion.
    s.motion.ProcessMotion(rP, rY, rR, aX, aY, aZ, (float)dt);
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
        s.debugMx = s.debugMy = 0;
        s.joyFY.filter(0, s.absT);
        s.joyFP.filter(0, s.absT);
        s.accX = 0;
        s.accY = 0;
        s.tiltY = 0;
        s.tiltP = 0;
        s.lostY = 0;
        s.lostP = 0;
        s.lostR = 0;
        s.smoothDY = 0;
        s.smoothDP = 0;
        s.smooth_factor = 1.0;
        s.trigRollSuppressTimer = 0.0f;
        s.debugAccelSpeed = 0.0;
        s.debugAccel = 1.0;
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
    s.motion.GetCalibratedGyro(rawP, rawY, rawR);
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
        s.lostY = s.lostP = s.lostR = 0;
        ApplyJoystickMode(rawY, rawP, rawR, dt, s, p, inputY, inputP, true);
    } else {
        ApplyDirectGyroMode(rawY, rawP, rawR, dt, s, p, inputY, inputP, inputR);

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
        // Trigger-coupled roll suppression: Tukey window (bath-shaped) on rawR
        // BEFORE OneEuro filter.  5ms ramp-down → 15ms zero plateau → ramp-up
        // to end.  The wide zero floor absorbs timing jitter in the mechanical
        // jolt (which arrives 8–12ms after the trigger edge).
        if (s.trigRollSuppressTimer > 0.0f) {
            double total = s.trigRollSuppressTotal;
            if (total > 0.0) {
                double elapsed = total - s.trigRollSuppressTimer;
                double roll_mult = 1.0;
                if (elapsed < 0.005) {
                    double t = elapsed / 0.005;
                    roll_mult = 0.5 + 0.5 * cos(M_PI * t);
                } else if (elapsed <= 0.020) {
                    roll_mult = 0.0;
                } else {
                    double rampUp = total - 0.020;
                    double t = fmin(fmax((elapsed - 0.020) / rampUp, 0.0), 1.0);
                    roll_mult = 0.5 - 0.5 * cos(M_PI * t);
                }
                inputR *= roll_mult;
            }
            s.trigRollSuppressTimer -= (float)dt;
            if (s.trigRollSuppressTimer < 0.0f) s.trigRollSuppressTimer = 0.0f;
        }

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
    // Tilt correction: pure rotation with yaw_weight
    double tilt_hor = (inputY * cos(rad) + inputR * sin(rad)) * p.yaw_weight;
    // Roll mixing: roll contributes directly to horizontal, independent of tilt
    double roll_hor = inputR * p.roll_weight;
    double hor = tilt_hor + roll_hor;
    // Use the motion that actually contributes to the mouse, after suppression,
    // per-axis filtering and axis mixing. Unused/cancelled roll must not boost
    // the other axes. Stay in mapped degrees/second, before base sensitivity,
    // temporary slowdown and the downstream stopping/bounce-back effects.
    double accelSpeed = hypot(hor, inputP * p.vertical_scale);
    if (!pureJoystick && p.accel_rate > 0 && accelSpeed > p.accel_threshold) {
        accel = fmin(p.accel_limit, 1.0 + (accelSpeed - p.accel_threshold) * p.accel_rate);
    }
    s.debugAccelSpeed = accelSpeed;
    s.debugAccel = accel;
    double sens = (p.real_world_sens * p.dots_per_360 / 360.0) * accel * slowdown;

    // Deceleration brake: detects rapid gyro deceleration and dampens it
    // to reduce overshoot when stopping.
    float decelMul = 1.f;
    if (p.decel_brake_strength > 0.f && s.decelHistoryCount >= 10) {
        constexpr float kWindowSec = 0.010f;
        constexpr float kSpeedMin = 2.f;
        constexpr float kEngageTime = 0.003f;
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
        bool speedGate = (omega >= kSpeedMin);
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

    // Bounce-back: suppress small reversal after fast flick
    if (p.bounce_back_strength > 0.f && p.bounce_back_threshold > 0.f) {
        s.bouncePeakY = fmaxf(s.bouncePeakY * 0.9999f, fabs(hor));
        s.bouncePeakP = fmaxf(s.bouncePeakP * 0.9999f, fabs(inputP));
        if (s.bouncePeakY > p.bounce_back_peak_threshold) s.bounceArmedY = true;
        if (s.bouncePeakP > p.bounce_back_peak_threshold) s.bounceArmedP = true;

        if (s.bounceTimerY > 0.f) {
            hor *= (1.f - p.bounce_back_strength);
            s.bounceTimerY -= dt;
        } else if (s.bounceDelayY > 0.f) {
            s.bounceDelayY -= dt;
            if (s.bounceDelayY <= 0.f) s.bounceTimerY = p.bounce_back_time;
        } else if (s.bounceArmedY && fabs(hor) < p.bounce_back_threshold && s.prevHor * hor < 0 && hor != 0) {
            s.bounceDelayY = p.bounce_back_delay;
        }
        if (s.bounceTimerP > 0.f) {
            inputP *= (1.f - p.bounce_back_strength);
            s.bounceTimerP -= dt;
        } else if (s.bounceDelayP > 0.f) {
            s.bounceDelayP -= dt;
            if (s.bounceDelayP <= 0.f) s.bounceTimerP = p.bounce_back_time;
        } else if (s.bounceArmedP && fabs(inputP) < p.bounce_back_threshold && s.prevInputP * inputP < 0 && inputP != 0) {
            s.bounceDelayP = p.bounce_back_delay;
        }

        if (s.bounceTimerY <= 0.f && s.bounceDelayY <= 0.f && fabs(hor) < p.bounce_back_threshold * 0.5f) {
            s.bounceArmedY = false;
            s.bouncePeakY = 0.f;
        }
        if (s.bounceTimerP <= 0.f && s.bounceDelayP <= 0.f && fabs(inputP) < p.bounce_back_threshold * 0.5f) {
            s.bounceArmedP = false;
            s.bouncePeakP = 0.f;
        }
    }
    s.prevHor = hor;
    s.prevInputP = inputP;

    s.accX += (hor * dt) * sens;
    s.accY += (inputP * dt) * sens * p.vertical_scale;

    int mx = (int)std::round(s.accX);
    int my = (int)std::round(s.accY);

    s.debugRawR = rawR;
    s.debugEuroR = inputR;
    s.debugHor = hor;
    s.debugMx = mx;
    s.debugMy = my;

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
