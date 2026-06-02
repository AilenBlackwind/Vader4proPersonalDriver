#include "joystick_mode.h"

#include <algorithm>
#include <cmath>

namespace {

double ApplyInputDeadzone(double value, double deadzone) {
    double mag = fabs(value);
    if (mag <= deadzone) return 0.0;
    return (value > 0 ? 1.0 : -1.0) * (mag - deadzone);
}

double MoveTowardZero(double value, double maxStep) {
    if (value > 0.0) return fmax(0.0, value - maxStep);
    if (value < 0.0) return fmin(0.0, value + maxStep);
    return 0.0;
}

double ShapeTilt(double tilt, double deadzone, double limit, double exponent, double maxSpeed) {
    double mag = fabs(tilt);
    if (mag <= deadzone || limit <= deadzone) return 0.0;
    double normalized = std::clamp((mag - deadzone) / (limit - deadzone), 0.0, 1.0);
    return (tilt > 0 ? 1.0 : -1.0) * pow(normalized, exponent) * maxSpeed * 5.0;
}

}  // namespace

void ApplyJoystickMode(double rawY,
                       double rawP,
                       double dt,
                       GyroState& state,
                       const GyroProfile& profile,
                       double& inputY,
                       double& inputP) {
    double inputTiltY =
        ApplyInputDeadzone(rawY, profile.joy_input_deadzone) * profile.joy_input_rate * dt;
    double inputTiltP =
        ApplyInputDeadzone(rawP, profile.joy_input_deadzone) * profile.joy_input_rate * dt;

    state.tiltY += inputTiltY;
    state.tiltP += inputTiltP;

    double centeringRate = profile.joy_centering + profile.joy_auto_recenter;
    double centeringStep = centeringRate * dt;
    state.tiltY = MoveTowardZero(state.tiltY, centeringStep);
    state.tiltP = MoveTowardZero(state.tiltP, centeringStep);

    double tiltLimit = fmax(0.01, profile.joy_tilt_limit);
    state.tiltY = std::clamp(state.tiltY, -tiltLimit, tiltLimit);
    state.tiltP = std::clamp(state.tiltP, -tiltLimit, tiltLimit);

    inputY = ShapeTilt(state.tiltY, profile.joy_deadzone, tiltLimit, profile.joy_exponent,
                       profile.joy_max_speed);
    inputP = ShapeTilt(state.tiltP, profile.joy_deadzone, tiltLimit, profile.joy_exponent,
                       profile.joy_max_speed * profile.joy_vertical_scale);

    inputY = state.fY.filter(inputY, state.absT);
    inputP = state.fP.filter(inputP, state.absT);
}
