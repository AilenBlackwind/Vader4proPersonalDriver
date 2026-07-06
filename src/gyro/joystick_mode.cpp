#include "joystick_mode.h"

#include <algorithm>
#include <cmath>

namespace {

double MoveTowardZero(double value, double maxStep) {
    if (value > 0.0) return fmax(0.0, value - maxStep);
    if (value < 0.0) return fmin(0.0, value + maxStep);
    return 0.0;
}

void ApplyRadialDeadzone(double x, double y, double deadzone, double& outX, double& outY) {
    double mag = sqrt(x * x + y * y);
    if (mag <= deadzone || mag <= 0.0) {
        outX = 0.0;
        outY = 0.0;
        return;
    }

    double scaledMag = mag - deadzone;
    outX = (x / mag) * scaledMag;
    outY = (y / mag) * scaledMag;
}

void ClampRadial(double& x, double& y, double limit) {
    double mag = sqrt(x * x + y * y);
    if (mag <= limit || mag <= 0.0) return;

    double scale = limit / mag;
    x *= scale;
    y *= scale;
}

void ShapeTiltVector(double tiltX,
                     double tiltY,
                     double deadzone,
                     double limit,
                     double exponent,
                     double maxSpeed,
                     double verticalScale,
                     double& outX,
                     double& outY) {
    deadzone = fmax(0.0, deadzone);
    double mag = sqrt(tiltX * tiltX + tiltY * tiltY);
    if (mag <= deadzone || limit <= deadzone || mag <= 0.0) {
        outX = 0.0;
        outY = 0.0;
        return;
    }

    double normalized = std::clamp((mag - deadzone) / (limit - deadzone), 0.0, 1.0);
    double shapedMag = pow(normalized, exponent) * maxSpeed * 5.0;
    outX = (tiltX / mag) * shapedMag;
    outY = (tiltY / mag) * shapedMag * verticalScale;
}

}  // namespace

void ApplyJoystickMode(double rawY,
                       double rawP,
                       double rawR,
                       double dt,
                       GyroState& state,
                       const GyroProfile& profile,
                       double& inputY,
                       double& inputP,
                       bool freezeNeutral) {
    double outputDeadzone = fmax(0.0, profile.joy_deadzone);
    double inputTiltY = 0.0;
    double inputTiltP = 0.0;
    double inputTiltR = 0.0;
    double dummy = 0.0;
    ApplyRadialDeadzone(rawY, rawP, profile.joy_input_deadzone, inputTiltY, inputTiltP);
    ApplyRadialDeadzone(rawR, 0.0, profile.joy_input_deadzone, inputTiltR, dummy);

    double rollInv = profile.invert_roll ? -1.0 : 1.0;
    double rollContrib = inputTiltR * profile.joy_roll_weight * rollInv;
    state.tiltY += (inputTiltY + rollContrib) * profile.joy_input_rate * dt;
    state.tiltP += inputTiltP * profile.joy_input_rate * dt;

    double tiltLimit = fmax(0.01, profile.joy_tilt_limit);
    ClampRadial(state.tiltY, state.tiltP, tiltLimit);

    bool inputNeutral = inputTiltY == 0.0 && inputTiltP == 0.0 && inputTiltR == 0.0;
    if (inputNeutral && !freezeNeutral) {
        state.tiltY = MoveTowardZero(state.tiltY, (profile.joy_centering + profile.joy_auto_recenter) * dt);
        state.tiltP = MoveTowardZero(state.tiltP, (profile.joy_centering + profile.joy_auto_recenter) * dt);
    }

    ShapeTiltVector(state.tiltY, state.tiltP, outputDeadzone, tiltLimit, profile.joy_exponent,
                    profile.joy_max_speed, profile.joy_vertical_scale, inputY, inputP);

    inputY = state.joyFY.filter(inputY, state.absT);
    inputP = state.joyFP.filter(inputP, state.absT);
}
