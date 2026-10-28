#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include "../src/gyro/gyro_processing.h"

namespace {
long sentX = 0, sentY = 0;
UINT TestSendInput(UINT count, LPINPUT inputs, int) {
    for (UINT i = 0; i < count; ++i) {
        sentX += inputs[i].mi.dx;
        sentY += inputs[i].mi.dy;
    }
    return count;
}
}

// Exercise the production pipeline without moving the user's mouse.
#define SendInput TestSendInput
#include "../src/gyro/gyro_processing.cpp"
#undef SendInput

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
bool Near(double a, double b) { return std::abs(a - b) < 1e-5; }
GyroProfile Profile() {
    GyroProfile p;
    p.tilt_axis_angle = 0;
    p.invert_roll = false;
    p.roll_deadzone = 0;
    p.smooth_max_strength = 1;
    p.recovery_enabled = false;
    p.bounce_back_strength = 0;
    p.accel_threshold = 4;
    p.accel_rate = 0.1;
    p.accel_limit = 3;
    p.real_world_sens = 100;
    p.dots_per_360 = 360;
    return p;
}
void Step(GyroState& s, const GyroProfile& p, float yaw, float pitch, float roll,
          float slowdown = 1) {
    constexpr double dt = 0.01;
    s.absT += dt;
    s.motion.SetCalibrationMode(GamepadMotionHelpers::CalibrationMode::Manual);
    s.motion.ProcessMotion(-pitch, -yaw, roll, 0, 0, 1, float(dt));
    ApplyGyroMovement(0, 0, 0, dt, s, p, 1, slowdown);
}
}

int main() {
    auto p = Profile();
    // Disabled roll cannot amplify a simultaneous deliberate pitch movement.
    p.roll_weight = 0;
    GyroState clean, noisy;
    sentX = sentY = 0;
    Step(clean, p, 0, 3, 0);
    const long cleanY = sentY;
    sentX = sentY = 0;
    Step(noisy, p, 0, 3, 100);
    Check(Near(noisy.debugAccel, 1) && sentY == cleanY && sentX == 0,
          "Disabled roll amplified mouse output");

    // Tilt mixing still makes roll relevant when its direct weight is zero.
    p.tilt_axis_angle = 90;
    GyroState tilt;
    Step(tilt, p, 0, 0, 10);
    Check(Near(tilt.debugAccel, 1.6), "Tilt roll contribution was ignored");

    p = Profile();
    p.invert_roll = true;
    GyroState cancelled;
    Step(cancelled, p, 10, 0, 10);
    Check(Near(cancelled.debugAccelSpeed, 0) && Near(cancelled.debugAccel, 1),
          "Cancelled yaw and roll still triggered acceleration");

    p = Profile();
    GyroState suppressed;
    suppressed.trigRollSuppressTotal = 0.05f;
    suppressed.trigRollSuppressTimer = 0.04f; // Inside the zero plateau.
    Step(suppressed, p, 0, 3, 100);
    Check(Near(suppressed.debugAccel, 1), "Suppressed trigger impulse boosted pitch");

    // Alternating roll noise is filtered while steady pitch stays responsive.
    GyroState filtered;
    filtered.gyroFR.updateParams(0.1, 0);
    Step(filtered, p, 0, 3, 0);
    for (int i = 0; i < 100; ++i) {
        Step(filtered, p, 0, 3, i % 2 ? -20.f : 20.f);
        Check(Near(filtered.debugAccel, 1), "Roll noise bypassed One Euro");
        Check(filtered.debugAccelSpeed >= 3, "Roll filter interfered with pitch");
    }

    // Known curve points, including exact threshold and saturation.
    for (const auto speed : {4.f, 14.f, 30.f}) {
        GyroState s;
        Step(s, p, speed, 0, 0);
        const double expected = speed == 4 ? 1 : speed == 14 ? 2 : 3;
        Check(Near(s.debugAccel, expected), "Acceleration curve changed");
    }
    GyroState sensitivity;
    p.real_world_sens = 1;
    p.dots_per_360 = 8000;
    Step(sensitivity, p, 14, 0, 0, 0.5f);
    Check(Near(sensitivity.debugAccel, 2), "Sensitivity/slowdown moved threshold");

    p = Profile();
    p.vertical_scale = 0;
    GyroState disabledPitch;
    Step(disabledPitch, p, 3, 100, 0);
    Check(Near(disabledPitch.debugAccel, 1), "Disabled pitch boosted horizontal");
    p.vertical_scale = 2;
    GyroState vertical;
    Step(vertical, p, 0, 7, 0);
    Check(Near(vertical.debugAccel, 2), "Vertical scale was ignored");

    p = Profile();
    p.roll_weight = 2;
    GyroState weightedRoll;
    Step(weightedRoll, p, 0, 0, 7);
    Check(Near(weightedRoll.debugAccel, 2), "Deliberate weighted roll failed to accelerate");

    p = Profile();
    p.accel_rate = 0;
    GyroState disabled;
    Step(disabled, p, 100, 0, 0);
    Check(Near(disabled.debugAccel, 1), "Disabled acceleration changed output");
    p = Profile();
    p.joystick_mode = true;
    p.accel_threshold = 0;
    p.accel_rate = 100;
    GyroState joystick;
    Step(joystick, p, 100, 0, 0);
    Check(Near(joystick.debugAccel, 1), "Pure joystick gained mouse acceleration");
    p.joy_overlay_enabled = true;
    p.joy_roll_weight = 0;
    GyroState overlayClean, overlayNoisy;
    p.roll_weight = 0;
    Step(overlayClean, p, 10, 0, 0);
    Step(overlayNoisy, p, 10, 0, 100);
    Check(overlayClean.debugAccel > 1 &&
          Near(overlayClean.debugAccelSpeed, overlayNoisy.debugAccelSpeed),
          "Overlay lost acceleration or unused roll leaked into it");
    ApplyGyroMovement(0, 0, 0, 0.01, filtered, p, 0);
    Check(Near(filtered.debugAccelSpeed, 0) && Near(filtered.debugAccel, 1),
          "Gyro off retained stale acceleration diagnostics");
    std::cout << "Gyro acceleration tests passed\n";
}
