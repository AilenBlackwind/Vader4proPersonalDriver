#include "QuadraticCurve.h"

float QuadraticSensitivity(float omega, float sMin, float sMax, float vCap)
{
    // Avoid division by zero or negative caps; treat as always at max.
    if (vCap <= 0.0f)
    {
        return sMax;
    }

    if (omega >= vCap)
    {
        return sMax;
    }

    const float t = omega / vCap;
    return sMin + (sMax - sMin) * t * t;
}
