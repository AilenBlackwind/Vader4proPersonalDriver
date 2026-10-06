#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "QuadraticCurve.h" // declares QuadraticSensitivity

using Catch::Approx;

// We assume:
//
// float QuadraticSensitivity(float omega, float sMin, float sMax, float vCap);
// S(omega) = sMin + (sMax - sMin) * (omega / vCap)^2 for 0 <= omega < vCap
// S(omega) = sMax for omega >= vCap
// vCap <= 0 -> sMax


// ---------------------------------------------------------
// 1. Basic shape / anchor tests
// ---------------------------------------------------------

TEST_CASE("QuadraticSensitivity at omega = 0 returns sMin when vCap > 0") {
    float sMin = 0.5f;
    float sMax = 1.0f;
    float vCap = 40.0f;

    float S = QuadraticSensitivity(0.0f, sMin, sMax, vCap);
    REQUIRE(S == Approx(sMin).margin(1e-6f));
}

TEST_CASE("QuadraticSensitivity at vCap returns sMax") {
    float sMin = 0.5f;
    float sMax = 1.0f;
    float vCap = 40.0f;

    float S_at_cap = QuadraticSensitivity(vCap, sMin, sMax, vCap);
    REQUIRE(S_at_cap == Approx(sMax).margin(1e-6f));
}

TEST_CASE("QuadraticSensitivity beyond vCap stays at sMax (hard cap)") {
    float sMin = 0.5f;
    float sMax = 1.0f;
    float vCap = 40.0f;

    for (float omega : {40.0f, 50.0f, 80.0f, 200.0f}) {
        float S = QuadraticSensitivity(omega, sMin, sMax, vCap);
        REQUIRE(S == Approx(sMax).margin(1e-6f));
    }
}


// ---------------------------------------------------------
// 2. Range and monotonicity tests (0 <= omega <= vCap)
// ---------------------------------------------------------

TEST_CASE("QuadraticSensitivity stays within [sMin, sMax] on [0, vCap]") {
    float sMin = 0.5f;
    float sMax = 1.0f;
    float vCap = 40.0f;

    for (float omega = 0.0f; omega <= vCap; omega += 1.0f) {
        float S = QuadraticSensitivity(omega, sMin, sMax, vCap);
        REQUIRE(S >= Approx(sMin).margin(1e-6f));
        REQUIRE(S <= Approx(sMax).margin(1e-6f));
    }
}

TEST_CASE("QuadraticSensitivity is monotone increasing on [0, vCap]") {
    float sMin = 0.5f;
    float sMax = 1.0f;
    float vCap = 40.0f;

    float prev = QuadraticSensitivity(0.0f, sMin, sMax, vCap);

    for (float omega = 0.0f; omega <= vCap; omega += 1.0f) {
        float cur = QuadraticSensitivity(omega, sMin, sMax, vCap);
        REQUIRE(cur >= Approx(prev).margin(1e-6f));
        prev = cur;
    }
}


// ---------------------------------------------------------
// 3. Parameter behavior tests
// ---------------------------------------------------------

TEST_CASE("Increasing sMax never decreases quadratic sensitivity") {
    float sMin  = 0.5f;
    float vCap  = 40.0f;

    float sMax1 = 1.0f;
    float sMax2 = 1.2f; // higher

    for (float omega = 0.0f; omega <= vCap; omega += 4.0f) {
        float s1 = QuadraticSensitivity(omega, sMin, sMax1, vCap);
        float s2 = QuadraticSensitivity(omega, sMin, sMax2, vCap);
        REQUIRE(s2 >= Approx(s1).margin(1e-6f));
    }
}

TEST_CASE("Increasing vCap makes the curve ramp more slowly") {
    float sMin = 0.5f;
    float sMax = 1.0f;

    float vCap_fast = 20.0f; // ramps quickly
    float vCap_slow = 40.0f; // ramps more slowly

    float omega = 10.0f;     // some fixed speed

    float s_fast = QuadraticSensitivity(omega, sMin, sMax, vCap_fast);
    float s_slow = QuadraticSensitivity(omega, sMin, sMax, vCap_slow);

    // At the same omega, the larger vCap should produce lower sensitivity.
    REQUIRE(s_slow <= Approx(s_fast).margin(1e-6f));
}


// ---------------------------------------------------------
// 4. Edge cases
// ---------------------------------------------------------

TEST_CASE("QuadraticSensitivity with non-positive vCap returns sMax") {
    float sMin = 0.5f;
    float sMax = 1.0f;

    for (float vCap : {0.0f, -10.0f, -1.0f}) {
        float S0 = QuadraticSensitivity(0.0f,  sMin, sMax, vCap);
        float S1 = QuadraticSensitivity(10.0f, sMin, sMax, vCap);
        float S2 = QuadraticSensitivity(100.0f, sMin, sMax, vCap);

        REQUIRE(S0 == Approx(sMax).margin(1e-6f));
        REQUIRE(S1 == Approx(sMax).margin(1e-6f));
        REQUIRE(S2 == Approx(sMax).margin(1e-6f));
    }
}


// ---------------------------------------------------------
// 5. Golden sample tests (nice exact-ish values)
// ---------------------------------------------------------
//
// Use sMin=0.5, sMax=1.0, vCap=40:
//   delta = 0.5
//   S(omega) = 0.5 + 0.5 * (omega / 40)^2
//
// Checked points:
//   omega= 0  -> S=0.5
//   omega=10  -> t=0.25,  t^2=0.0625  -> S=0.5 + 0.5*0.0625 = 0.53125
//   omega=20  -> t=0.5,   t^2=0.25    -> S=0.5 + 0.5*0.25   = 0.625
//   omega=30  -> t=0.75,  t^2=0.5625  -> S=0.5 + 0.5*0.5625 = 0.78125
//   omega=40  -> S=1.0 (cap)
//
TEST_CASE("QuadraticSensitivity matches golden samples for sMin=0.5, sMax=1.0, vCap=40") {
    float sMin = 0.5f;
    float sMax = 1.0f;
    float vCap = 40.0f;

    REQUIRE(QuadraticSensitivity( 0.0f, sMin, sMax, vCap) == Approx(0.5f      ).margin(1e-6f));
    REQUIRE(QuadraticSensitivity(10.0f, sMin, sMax, vCap) == Approx(0.53125f  ).margin(1e-6f));
    REQUIRE(QuadraticSensitivity(20.0f, sMin, sMax, vCap) == Approx(0.625f    ).margin(1e-6f));
    REQUIRE(QuadraticSensitivity(30.0f, sMin, sMax, vCap) == Approx(0.78125f  ).margin(1e-6f));
    REQUIRE(QuadraticSensitivity(40.0f, sMin, sMax, vCap) == Approx(1.0f      ).margin(1e-6f));
}
