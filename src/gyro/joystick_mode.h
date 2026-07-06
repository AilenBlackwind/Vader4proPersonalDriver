#pragma once

#include "gyro_processing.h"

// Applies virtual-stick steering using gyro input and writes shaped Y/P outputs.
// freezeNeutral: if true, don't center tilt when input is neutral (pure joystick mode).
void ApplyJoystickMode(double rawY,
                       double rawP,
                       double rawR,
                       double dt,
                       GyroState& state,
                       const GyroProfile& profile,
                       double& inputY,
                       double& inputP,
                       bool freezeNeutral = false);
