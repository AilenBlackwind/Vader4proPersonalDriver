#pragma once

// Uncomment to enable the trigger-history dump (prints ±40ms of gyro/accel/mouse
// data around each analog trigger rising edge).  When disabled, TriggerDebugger
// compiles to a zero-overhead empty struct.
//
// To enable: uncomment the line below, or add /DENABLE_TRIGGER_DEBUG to your
// build flags (e.g. in build.bat: cl.exe ... /DENABLE_TRIGGER_DEBUG ... ).

// #define ENABLE_TRIGGER_DEBUG

#ifdef ENABLE_TRIGGER_DEBUG

#include <cmath>
#include <cstdio>

struct TriggerDebugger {
    struct Sample {
        double t;
        float y, p, r;
        float ax, ay, az;
        float euroR, hor, accX;
        int mx;
    };

    Sample history[128] = {};
    int head = 0;
    int count = 0;

    bool pending = false;
    double eventT = 0.0;
    float timer = 0.0f;
    int trigVal = 0;
    int hidVal = 0;

    void push(double t, float y, float p, float r, float ax, float ay, float az) {
        history[head] = {t, y, p, r, ax, ay, az};
        head = (head + 1) % 128;
        if (count < 128) count++;
    }

    void updateMouse(float euroR, float hor, float accX, int mx) {
        int lastIdx = (head - 1 + 128) % 128;
        history[lastIdx].euroR = euroR;
        history[lastIdx].hor = hor;
        history[lastIdx].accX = accX;
        history[lastIdx].mx = mx;
    }

    void arm(double t, int trig, int hid) {
        pending = true;
        eventT = t;
        timer = 0.030f;
        trigVal = trig;
        hidVal = hid;
        printf("[ANALOG] rising edge analog=%d hid=%d\n", trig, hid);
    }

    void tick(double dt) {
        if (!pending) return;
        timer -= (float)dt;
        if (timer > 0.0f) return;
        pending = false;

        double evT = eventT;
        int centerIdx = 0;
        double bestDist = 1e9;
        int start = (count == 128) ? head : 0;
        for (int i = 0; i < count; i++) {
            int idx = (start + i) % 128;
            double d = fabs(history[idx].t - evT);
            if (d < bestDist) { bestDist = d; centerIdx = idx; }
        }

        printf("[TRIG] analog=%d hid=%d eventT=%.4f\n", trigVal, hidVal, evT);
        printf("  offset    yaw(pitch)   pitch(roll)   roll(yaw)    accX     accY     accZ\n");
        for (int i = 0; i < count; i++) {
            int idx = (start + i) % 128;
            double dT_ms = (history[idx].t - evT) * 1000.0;
            if (dT_ms < -40.0 || dT_ms > 40.0) continue;
            char marker = (idx == centerIdx) ? '<' : ' ';
            printf("%c %+7.2fms  %+9.2f  %+9.2f  %+9.2f   %+7.3f %+7.3f %+7.3f\n",
                   marker, dT_ms,
                   history[idx].y, history[idx].p, history[idx].r,
                   history[idx].ax, history[idx].ay, history[idx].az);
        }
        {
            int li = (head - 1 + 128) % 128;
            printf("[CUR]  rawR=%6.2f  euro=%6.2f  hor=%6.2f  acc=%6.2f  mx=%d\n",
                   history[li].r, history[li].euroR,
                   history[li].hor, history[li].accX, history[li].mx);
        }
        fflush(stdout);
    }
};

#else  // ENABLE_TRIGGER_DEBUG not defined — zero-overhead stub

struct TriggerDebugger {
    void push(double, float, float, float, float, float, float) {}
    void updateMouse(float, float, float, int) {}
    void arm(double, int, int) {}
    void tick(double) {}
};

#endif
