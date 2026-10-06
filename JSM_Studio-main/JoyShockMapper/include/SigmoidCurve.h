#pragma once

// Sigmoid (S-shaped) curve:
// S = sMin + (sMax - sMin) * sigma((omega - vMid) / width)
// where sigma(z) = 1 / (1 + e^-z).
// omega: input speed
// width: controls steepness; larger = gentler
float SigmoidSensitivity(float omega, float sMin, float sMax, float vMid, float width);
