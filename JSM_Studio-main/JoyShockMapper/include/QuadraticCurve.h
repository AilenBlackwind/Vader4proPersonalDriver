#pragma once

// Quadratic curve with a hard cap at vCap:
// For omega < vCap:
//   S = sMin + (sMax - sMin) * (omega / vCap)^2
// For omega >= vCap:
//   S = sMax
float QuadraticSensitivity(float omega, float sMin, float sMax, float vCap);
