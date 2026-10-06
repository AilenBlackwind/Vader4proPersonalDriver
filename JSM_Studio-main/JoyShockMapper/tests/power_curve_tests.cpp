#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "PowerCurve.h"

using Catch::Approx;

// Model under test:
//
//   float PowerSensitivity(float omega,
//                          float sMin,
//                          float sMax,
//                          float vRef,
//                          float exponent);
//
// Intended behavior:
//
//   u(ω) = (ω / vRef)^exponent
//   t(ω) = 1 - exp(-u(ω))
//   S(ω) = sMin + (sMax - sMin) * t(ω)
//
// Guards (as implemented):
//   - vRef <= 0  -> return sMax
//   - exponent <= 0 -> return sMin
//   - omega <= 0 -> return sMin
//   - S(ω) is monotone increasing in ω for ω > 0
//   - S(ω) in [sMin, sMax] and tends to sMax as ω → ∞


// ---------------------------------------------------------
// 1. Basic anchor tests
// ---------------------------------------------------------

TEST_CASE("PowerSensitivity at omega = 0 returns sMin") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float vRef = 50.0f;
    float exponent = 1.0f;

    float S = PowerSensitivity(0.0f, sMin, sMax, vRef, exponent);
    REQUIRE(S == Approx(sMin).margin(1e-6f));
}

TEST_CASE("PowerSensitivity tends toward sMax at large omega") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float vRef = 50.0f;
    float exponent = 1.0f;

    float omega = 1e6f;
    float S = PowerSensitivity(omega, sMin, sMax, vRef, exponent);

    REQUIRE(S <= Approx(sMax).margin(1e-6f));
    REQUIRE(S == Approx(sMax).margin(1e-3f));
}


// ---------------------------------------------------------
// 2. Range and monotonicity tests
// ---------------------------------------------------------

TEST_CASE("PowerSensitivity stays within [sMin, sMax] for reasonable omegas") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float vRef = 50.0f;
    float exponent = 1.0f;

    for (float omega = 0.0f; omega <= 200.0f; omega += 2.0f) {
        float S = PowerSensitivity(omega, sMin, sMax, vRef, exponent);
        REQUIRE(S >= Approx(sMin).margin(1e-6f));
        REQUIRE(S <= Approx(sMax).margin(1e-6f));
    }
}

TEST_CASE("PowerSensitivity is monotone non-decreasing in omega") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float vRef = 50.0f;
    float exponent = 1.0f;

    float prev = PowerSensitivity(0.0f, sMin, sMax, vRef, exponent);

    for (float omega = 0.0f; omega <= 200.0f; omega += 2.0f) {
        float cur = PowerSensitivity(omega, sMin, sMax, vRef, exponent);
        REQUIRE(cur >= Approx(prev).margin(1e-6f));
        prev = cur;
    }
}


// ---------------------------------------------------------
// 3. Parameter behavior tests
// ---------------------------------------------------------

TEST_CASE("Increasing vRef delays the rise in sensitivity") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float exponent = 1.0f;

    float vRef_fast = 30.0f;  // rises earlier
    float vRef_slow = 80.0f;  // rises later

    float omega = 40.0f;      // some fixed speed

    float S_fast = PowerSensitivity(omega, sMin, sMax, vRef_fast, exponent);
    float S_slow = PowerSensitivity(omega, sMin, sMax, vRef_slow, exponent);

    // Larger vRef => smaller (omega/vRef) => smaller u => smaller t => smaller S
    REQUIRE(S_slow <= Approx(S_fast).margin(1e-6f));
    REQUIRE(S_fast <= Approx(sMax).margin(1e-6f));
    REQUIRE(S_slow >= Approx(sMin).margin(1e-6f));
}

TEST_CASE("Exponent controls where the curve is aggressive (below vs above vRef)") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float vRef = 50.0f;

    float exp_low  = 0.5f;  // rises earlier
    float exp_high = 2.0f;  // rises later below vRef, more aggressive above

    // Below vRef: omega < vRef
    {
        float omega = 30.0f;
        float S_lowExp  = PowerSensitivity(omega, sMin, sMax, vRef, exp_low);
        float S_highExp = PowerSensitivity(omega, sMin, sMax, vRef, exp_high);

        // For omega < vRef, smaller exponent gives larger u => larger S
        REQUIRE(S_lowExp >= Approx(S_highExp).margin(1e-6f));
    }

    // Above vRef: omega > vRef
    {
        float omega = 100.0f;
        float S_lowExp  = PowerSensitivity(omega, sMin, sMax, vRef, exp_low);
        float S_highExp = PowerSensitivity(omega, sMin, sMax, vRef, exp_high);

        // For omega > vRef, larger exponent gives larger u => larger S
        REQUIRE(S_highExp >= Approx(S_lowExp).margin(1e-6f));
    }
}


// ---------------------------------------------------------
// 4. Edge cases / guards
// ---------------------------------------------------------

TEST_CASE("Non-positive vRef returns sMax (guard behavior)") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float exponent = 1.0f;

    for (float vRef : {0.0f, -10.0f}) {
        float S0   = PowerSensitivity(0.0f,   sMin, sMax, vRef, exponent);
        float Slow = PowerSensitivity(20.0f,  sMin, sMax, vRef, exponent);
        float Shigh= PowerSensitivity(200.0f, sMin, sMax, vRef, exponent);

        REQUIRE(S0    == Approx(sMax).margin(1e-6f));
        REQUIRE(Slow  == Approx(sMax).margin(1e-6f));
        REQUIRE(Shigh == Approx(sMax).margin(1e-6f));
    }
}

TEST_CASE("Non-positive exponent returns sMin (guard behavior)") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float vRef = 50.0f;

    for (float exponent : {0.0f, -1.0f, -5.0f}) {
        float S0   = PowerSensitivity(0.0f,   sMin, sMax, vRef, exponent);
        float Slow = PowerSensitivity(20.0f,  sMin, sMax, vRef, exponent);
        float Shigh= PowerSensitivity(200.0f, sMin, sMax, vRef, exponent);

        REQUIRE(S0    == Approx(sMin).margin(1e-6f));
        REQUIRE(Slow  == Approx(sMin).margin(1e-6f));
        REQUIRE(Shigh == Approx(sMin).margin(1e-6f));
    }
}

TEST_CASE("Negative omega behaves like zero (clamped to sMin)") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float vRef = 50.0f;
    float exponent = 1.0f;

    float Sneg = PowerSensitivity(-10.0f, sMin, sMax, vRef, exponent);
    REQUIRE(Sneg == Approx(sMin).margin(1e-6f));
}


// ---------------------------------------------------------
// 5. Golden sample tests for specific parameters
// ---------------------------------------------------------
//
// Use: sMin=0.5, sMax=1.5, vRef=50, exponent=1
//
// For exponent = 1:
//
//   u(ω)  = ω / vRef
//   t(ω)  = 1 - exp(-u)
//   S(ω)  = 0.5 + 1.0 * t(ω)
//
// Some reference values:
//   ω =   0: u=0,   t=0,                  S = 0.5
//   ω =  50: u=1,   t=1 - e^-1 ≈ 0.63212, S ≈ 1.13212
//   ω = 100: u=2,   t=1 - e^-2 ≈ 0.86466, S ≈ 1.36466
//
TEST_CASE("PowerSensitivity matches golden samples for sMin=0.5, sMax=1.5, vRef=50, exponent=1") {
    float sMin = 0.5f;
    float sMax = 1.5f;
    float vRef = 50.0f;
    float exponent = 1.0f;

    REQUIRE(PowerSensitivity(  0.0f, sMin, sMax, vRef, exponent)
            == Approx(0.5f).margin(1e-6f));

    REQUIRE(PowerSensitivity( 50.0f, sMin, sMax, vRef, exponent)
            == Approx(1.13212f).margin(1e-4f));

    REQUIRE(PowerSensitivity(100.0f, sMin, sMax, vRef, exponent)
            == Approx(1.36466f).margin(1e-4f));
}
