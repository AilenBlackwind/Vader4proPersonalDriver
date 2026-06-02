#pragma once

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Small reusable low-pass filter that smooths noisy input while staying responsive.
class OneEuroFilter {
    double mincutoff;
    double beta;
    double dcutoff;
    double x_prev;
    double dx_prev;
    double t_prev;
    bool first_time;

    double alpha(double cutoff, double dt) {
        double tau = 1.0 / (2.0 * M_PI * cutoff);
        return 1.0 / (1.0 + tau / dt);
    }

public:
    OneEuroFilter() : mincutoff(2.0), beta(0.02), dcutoff(1.0), first_time(true) {}

    // Updates smoothing parameters when the active profile or mode changes.
    void updateParams(double mc, double b) {
        mincutoff = mc;
        beta = b;
    }

    // Drops filter history so a new profile does not inherit old motion dynamics.
    void reset() { first_time = true; }

    // Filters one sample using the caller-provided absolute timestamp.
    double filter(double x, double t) {
        if (first_time) {
            first_time = false;
            x_prev = x;
            dx_prev = 0;
            t_prev = t;
            return x;
        }

        double dt = (t - t_prev <= 0) ? 0.001 : t - t_prev;
        double dx = (x - x_prev) / dt;
        double edx = dx_prev + alpha(dcutoff, dt) * (dx - dx_prev);
        dx_prev = edx;
        double result = x_prev + alpha(mincutoff + beta * fabs(edx), dt) * (x - x_prev);
        x_prev = result;
        t_prev = t;
        return result;
    }
};
