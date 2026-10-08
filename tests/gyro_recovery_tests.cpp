#include <iostream>
#include <cstdlib>
#include "../src/gyro/gyro_processing.cpp"

void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
bool Near(double a, double b) { return fabs(a - b) < 1e-8; }

int main() {
    // An uncapped trace must conserve angle and agree across sampling rates.
    for (int hz : {100, 250, 1000}) {
        double debt = 0, outputAngle = 0, dt = 1.0 / hz;
        for (int i = 0; i < hz; ++i)
            outputAngle += RecoverPrecision(1, 0.5, 10, dt, true, 2, debt) * dt;
        Check(Near(outputAngle + debt, 1), "Angle was created or lost before the safety cap");
        Check(Near(debt, 0.25 * (1 - exp(-2.0))), "Slow recovery depends on sample rate");
        Check(outputAngle > 0.5, "No recovery during slow tracking");
        for (int i = 0; i < hz * 10; ++i)
            outputAngle += (RecoverPrecision(2, 2, 10, dt, true, 2, debt) - 2) * dt;
        Check(Near(outputAngle, 1), "Stored angle was not returned");
        std::cout << hz << " Hz: recovered trace angle=" << outputAngle << '\n';
    }
    double debt = 0;
    for (int i = 0; i < 100000; ++i) {
        const double out = RecoverPrecision(0.5, 0.01, 1, 0.001, true, 2, debt);
        Check(out >= 0.01 && out <= 0.020000001 && debt <= 0.1,
              "Slow motion built unbounded debt or released a burst");
    }
    Check(Near(RecoverPrecision(0, 0, 1, 0.001, true, 2, debt), 0) && debt == 0,
          "Stop replayed old motion");
    debt = 0.1;
    Check(Near(RecoverPrecision(-2, -2, 1, 0.001, true, 2, debt), -2) && debt == 0,
          "Reversal returned debt in the old direction");
    for (int mode = 0; mode < 3; ++mode) {
        debt = 0.1;
        RecoverPrecision(1, 0.5, mode == 2 ? 0 : 1, 0.001, mode != 0,
                         mode == 1 ? 0 : 2, debt);
        Check(debt == 0, "Disabled recovery retained debt");
    }
    double fast = 0.1, slow = 0.1;
    Check(RecoverPrecision(2, 2, 1, 0.001, true, 4, fast) >
          RecoverPrecision(2, 2, 1, 0.001, true, 0.1, slow), "Recovery rate has no effect");

    // Run actual shaping with independent yaw/pitch/roll histories.
    GyroProfile p;
    p.precision_threshold = p.roll_precision_threshold = 1;
    p.precision_power = 2;
    p.roll_precision_power = 3; // Roll shapes speed directly; yaw shapes a multiplier.
    p.roll_deadzone = 0;
    p.recovery_rate = 2;
    GyroState s;
    double y = 0, pitch = 0, roll = 0;
    for (int i = 0; i < 20; ++i)
        ApplyDirectGyroMode(0.5, 0, 0.5, 0.001, s, p, y, pitch, roll);
    Check(y > 0.125 && Near(y, roll) && s.lostY > 0 && s.lostR > 0 && s.lostP == 0,
          "Roll recovery missing or histories coupled");
    s.trigRollSuppressTimer = 0.05f;
    ApplyDirectGyroMode(0.5, 0, 0.5, 0.001, s, p, y, pitch, roll);
    Check(s.lostR == 0 && Near(roll, 0.125), "Suppressed roll was saved for later replay");
    s.trigRollSuppressTimer = 0;
    p.smoothing_enabled = true;
    ApplyDirectGyroMode(0.5, 0, 0.5, 0.001, s, p, y, pitch, roll);
    Check(s.lostY == 0 && s.lostP == 0 && s.lostR > 0,
          "Smoothing did not isolate roll recovery");
    p.roll_deadzone = 0.6;
    ApplyDirectGyroMode(0.5, 0, 0.5, 0.001, s, p, y, pitch, roll);
    Check(roll == 0 && s.lostR == 0, "Roll deadzone was recovered");
    s.lostY = s.lostP = s.lostR = 0.1;
    ApplyGyroMovement(0, 0, 0, 0.001, s, p, 0);
    Check(s.lostY == 0 && s.lostP == 0 && s.lostR == 0, "Gyro-off left stale recovery");
    std::cout << "Recovery regression tests passed\n";
}
