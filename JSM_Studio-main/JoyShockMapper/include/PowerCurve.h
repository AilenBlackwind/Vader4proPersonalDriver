#pragma once

// Computes the "power" acceleration sensitivity curve.
// omega: input speed (deg/sec or equivalent)
// sMin: minimum sensitivity
// sMax: maximum sensitivity
// vRef: reference speed that sets where the power curve begins to increase more aggressively
// exponent: power applied to the scaled input
float PowerSensitivity(float omega, float sMin, float sMax, float vRef, float exponent);