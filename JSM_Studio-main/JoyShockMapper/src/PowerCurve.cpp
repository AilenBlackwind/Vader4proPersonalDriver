#include "PowerCurve.h"
#include <cmath>

float PowerSensitivity(float omega,
                       float sMin,
                       float sMax,
                       float vRef,
                       float exponent)
{
    if (vRef <= 0.0f) {
        return sMax;
    }
    if (exponent <= 0.0f) {
        return sMin;
    }
    if (omega <= 0.0f) {
        return sMin;
    }

    const float x = omega / vRef;
    const float u = std::pow(x, exponent);
    float t = 1.0f - std::exp(-u);

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    return sMin + (sMax - sMin) * t;
}