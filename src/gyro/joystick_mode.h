#pragma once

#include "gyro_processing.h"

// Applies virtual-stick steering using gyro input and writes shaped Y/P outputs.
void ApplyJoystickMode(double rawY,
                       double rawP,
                       double dt,
                       GyroState& state,
                       const GyroProfile& profile,
                       double& inputY,
                       double& inputP);
