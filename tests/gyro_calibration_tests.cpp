#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include "../src/gyro/gyro_processing.h"

namespace {
long sentX = 0, sentY = 0;
UINT CaptureMouse(UINT count, LPINPUT inputs, int) {
    for (UINT i = 0; i < count; ++i) {
        sentX += inputs[i].mi.dx;
        sentY += inputs[i].mi.dy;
    }
    return count;
}
}
#define SendInput CaptureMouse
#include "../src/gyro/gyro_processing.cpp"
#undef SendInput

namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
struct Output { long dx, dy; float calibrated; };
void Configure(GyroState& s, const Config& c) {
    s.motion.Settings.MinStillnessCorrectionTime = float(c.calibration_correction_time);
    s.motion.Settings.StillnessGyroDelta = float(c.calibration_gyro_delta);
    s.motion.Settings.StillnessAccelDelta = float(c.calibration_accel_delta);
}
Output Step(GyroState& s, const Config& c, float speed, int axis,
            int mode, double dt, bool legacy = false) {
    s.absT += dt;
    const auto& p = mode == 2 ? c.high_profile : c.low_profile;
    if (s.prevMode != mode) {
        s.gyroFX.updateParams(p.one_euro_min_cutoff, p.one_euro_beta);
        s.gyroFP.updateParams(p.one_euro_min_cutoff, p.one_euro_beta);
        s.gyroFR.updateParams(p.roll_euro_min_cutoff, p.roll_euro_beta);
        s.gyroFX.reset(); s.gyroFP.reset(); s.gyroFR.reset();
        s.motion.ResetMotion();
        s.accX = s.accY = s.lostY = s.lostP = s.lostR = 0;
        s.prevMode = mode;
    }
    float y = axis == 0 ? speed : 0, pch = axis == 1 ? speed : 0, r = axis == 2 ? speed : 0;
    if (legacy) {
        // Reproduce the old library policy and axis order. Exact zero during
        // warmup makes the former first estimator contribute zero bias here.
        s.motion.SetCalibrationMode(static_cast<GamepadMotionHelpers::CalibrationMode>(c.calibration_mode));
        s.motion.ProcessMotion(y, pch, r, 0, 0, 1 + c.accel_z_offset, float(dt));
    } else {
        ProcessGyroSensors(y, pch, r, 0, 0, 1 + c.accel_z_offset, dt, s, mode, c);
    }
    float cy, cp, cr;
    if (legacy) s.motion.GetCalibratedGyro(cy, cp, cr);
    else s.motion.GetCalibratedGyro(cp, cy, cr);
    sentX = sentY = 0;
    ApplyGyroMovement(0, 0, 0, dt, s, p, mode);
    return {sentX, sentY, axis == 0 ? cy : axis == 1 ? cp : cr};
}
void RegressionTests() {
    for (int hz : {100, 250, 1000}) {
        GamepadMotionHelpers::SensorMinMaxWindow bounded, cumulative;
        const float dt = 1.f / hz;
        for (int i = 0; i < 110 * hz; ++i) {
            GamepadMotionHelpers::Vec gyro(0, 0, i < 100 * hz ? 0.2f : 0.f);
            GamepadMotionHelpers::Vec accel(0, 0, 1);
            bounded.AddSample(gyro, accel, dt, 2.f);
            cumulative.AddSample(gyro, accel, dt);
        }
        Check(std::abs(bounded.MeanGyro.z - 0.2f * exp(-5.f)) < 0.00003f &&
              cumulative.MeanGyro.z > 0.18f,
              "Long stillness history retained old bias or response depends on sampling rate");
    }
    // Physically consistent gravity during X/Y rotations. The former swapped
    // gyro/native accel basis invents offsets even with perfect sensor data.
    for (int axis : {0, 1}) {
        Config c; c.calibration_mode = 2; c.calibration_max_gyro_speed = 20;
        GyroState s; GamepadMotion old;
        old.SetCalibrationMode(GamepadMotionHelpers::CalibrationMode::SensorFusion);
        for (int i = 0; i < 1200; ++i) {
            const double angle = i * 0.02 * 12 * M_PI / 180;
            const float ax = axis == 1 ? float(-sin(angle)) : 0;
            const float ay = axis == 0 ? float(sin(angle)) : 0;
            const float az = float(cos(angle));
            const float rY = axis == 1 ? 12.f : 0, rP = axis == 0 ? 12.f : 0;
            ProcessGyroSensors(rY, rP, 0, ax, ay, az, 0.02, s, 1, c);
            old.ProcessMotion(rY, rP, 0, ax, ay, az, 0.02f);
        }
        float x, y, z, ox, oy, oz;
        s.motion.GetCalibrationOffset(x, y, z);
        old.GetCalibrationOffset(ox, oy, oz);
        std::cout << "Sensor fusion axis " << axis << ": new bias=" << hypot(x, y)
                  << ", swapped basis bias=" << hypot(ox, oy) << '\n';
        Check(hypot(x, y) < 0.35 && hypot(ox, oy) > 5,
              "Sensor fusion gyro/accel coordinates are inconsistent");
    }
    {
        Config c; GyroState s; Configure(s, c);
        for (int i = 0; i < 4000; ++i) Step(s, c, 0, 2, 0, 0.001);
        for (int i = 0; i < 8000; ++i) Step(s, c, 8, 2, 0, 0.001);
        Output out{};
        for (int i = 0; i < 10000; ++i) out = Step(s, c, 0, 2, 1, 0.001);
        std::cout << "Off-mode turn, then active rest for 10s: calibrated="
                  << out.calibrated << ", mouse dx=" << out.dx << '\n';
        Check(std::abs(out.calibrated) < 0.01,
              "Off-mode turn poisoned calibration and caused persistent active drift");
    }
    int cases = 0;
    for (int hz : {100, 250, 1000}) for (int axis : {0, 1, 2})
    for (int sign : {-1, 1}) for (int mode : {1, 2}) {
        Config c;
        GyroState s; Configure(s, c);
        double dt = 1.0 / hz;
        for (int i = 0; i < 4 * hz; ++i) Step(s, c, 0, axis, 0, dt);
        for (float speed : {8.f, 3.f, 0.f}) {
            for (int i = 0; i < 6 * hz; ++i) {
                auto out = Step(s, c, sign * speed, axis, mode, dt);
                Check(std::abs(out.calibrated - sign * speed) < 1e-4,
                      "Automatic calibration absorbed active motion");
                if (speed == 0 && i > hz) Check(out.dx == 0 && out.dy == 0,
                      "Sustained mouse motion after stop");
            }
        }
        ++cases;
    }
    // Active movement above the speed limit must neither become startup bias
    // nor continue a collection window begun at low speed.
    for (int mode : {1, 2}) {
        Config c; GyroState s; Configure(s, c);
        for (int i = 0; i < 3000; ++i) {
            auto out = Step(s, c, 2, 2, mode, 0.001);
            Check(std::abs(out.calibrated - 2) < 1e-4, "Startup motion became zero bias");
            Check(!s.debugAutoCalibrationAllowed, "Fast motion allowed automatic calibration");
        }
        GyroState pending; Configure(pending, c);
        for (int i = 0; i < 200; ++i) Step(pending, c, 0.2f, 2, 0, 0.001);
        float bx, by, biasBefore;
        pending.motion.GetCalibrationOffset(bx, by, biasBefore);
        for (int i = 0; i < 3000; ++i) Step(pending, c, 2, 2, mode, 0.001);
        float biasAfter;
        pending.motion.GetCalibrationOffset(bx, by, biasAfter);
        Check(biasAfter == biasBefore && !pending.debugAutoCalibrationAllowed,
              "Fast activation continued unfinished calibration");
    }
    // Idle startup ignores a fast first sample and learns the small rest bias.
    // Manual calibration remains usable above the automatic speed limit.
    Config c; GyroState s; Configure(s, c);
    Step(s, c, 2, 2, 0, 0.001);
    for (int i = 0; i < 6000; ++i) Step(s, c, 0.2f, 2, 0, 0.001);
    float bx, by, bz;
    s.motion.GetCalibrationOffset(bx, by, bz);
    Check(std::abs(bz - 0.2f) < 0.01f,
          "Idle startup no longer converges");
    for (bool enabled : {false, true}) {
        Config activeConfig;
        activeConfig.calibration_while_active = enabled;
        GyroState active; Configure(active, activeConfig);
        Output out{};
        for (int i = 0; i < 10000; ++i) out = Step(active, activeConfig, 0.1f, 2, 1, 0.001);
        Check(enabled ? std::abs(out.calibrated) < 0.01 : std::abs(out.calibrated - 0.1f) < 0.001,
              "Active calibration option did not control bias learning");
    }
    {
        Config always; GyroState continuous; Configure(continuous, always);
        for (int i = 0; i < 10000; ++i) Step(continuous, always, 0.2f, 2, 1, 0.001);
        Output out{};
        for (int i = 0; i < 10000; ++i) out = Step(continuous, always, 0, 2, 1, 0.001);
        std::cout << "Continuous rest bias 0.2 -> 0: residual=" << out.calibrated << '\n';
        Check(std::abs(out.calibrated) < 0.01,
              "Changed incoming rest bias caused permanent drift in continuous mode");
    }
    GyroState manual; Configure(manual, c);
    manual.motion.StartContinuousCalibration();
    for (int i = 0; i < 1000; ++i) Step(manual, c, 1, 2, 1, 0.001);
    manual.motion.PauseContinuousCalibration();
    float x, y, z; manual.motion.GetCalibrationOffset(x, y, z);
    Check(std::abs(z - 1) < 1e-4, "Manual calibration was disabled");
    std::cout << "Calibration regressions passed: " << cases
              << " rate/axis/direction/mode combinations, startup, idle settling, manual key\n";
}
void Report(const char* config, const char* path) {
    Config c;
    Check(LoadConfig(config, c), "Cannot load report config");
    Check(!c.low_profile.joystick_mode, "Report scenario requires a direct-mouse low profile");
    GyroState fixed, legacy; Configure(fixed, c); Configure(legacy, c);
    std::ofstream html(path);
    Check(bool(html), "Cannot write report");
    html << R"HTML(<!doctype html><meta charset="utf-8"><title>Gyro replay</title>
<style>body{background:#111827;color:#eee;font:16px system-ui;max-width:1100px;margin:30px auto;padding:16px}canvas{width:100%;background:#1f2937;border-radius:8px}input{width:100%}button{padding:8px 20px}small{color:#adb9cd}</style>
<h1>Gyro replay: steady turn, slower turn, stop</h1>
<p>Artificial sensor input through production calibration, filters, mapping and pixel rounding. Mouse commands are captured; the desktop cursor is never moved.</p>
<p>Blue: corrected driver. Orange: previous automatic library calibration policy. Input: 4s rest, 8s at 8 deg/s, 2s at 3 deg/s, 2s rest. Constant gravity models yaw.</p>
<button id="play">Play / pause</button><input id="seek" type="range" min="0" value="0"><p id="readout"></p>
<canvas id="plot" width="1060" height="280"></canvas><p>Mouse path (independently scaled to fit): blue corrected, orange previous policy. Dots show the selected instant.</p>
<canvas id="path" width="1060" height="200"></canvas><p><small>This tests sensor-to-mouse processing. It does not emulate firmware, SDL transport, a game, OS pointer acceleration, trigger edges or profile hot reload.</small></p>
<script>const rows=[
)HTML";
    long fx = 0, fy = 0, lx = 0, ly = 0, fixedTail = 0, oldTail = 0;
    int fixedReverse = 0, oldReverse = 0;
    for (int i = 0; i < 16000; ++i) {
        float speed = i < 4000 ? 0 : i < 12000 ? 8 : i < 14000 ? 3 : 0;
        int mode = i < 4000 ? 0 : 1;
        auto f = Step(fixed, c, speed, 2, mode, 0.001);
        auto l = Step(legacy, c, speed, 2, mode, 0.001, true);
        fx += f.dx; fy += f.dy; lx += l.dx; ly += l.dy;
        if (i >= 12000 && i < 14000) { fixedReverse += f.calibrated < 0; oldReverse += l.calibrated < 0; }
        if (i >= 14250) { fixedTail += f.dx; oldTail += l.dx; }
        if (i % 10 == 0) html << '[' << i * 0.001 << ',' << speed << ',' << f.calibrated << ','
            << l.calibrated << ',' << fx << ',' << fy << ',' << lx << ',' << ly << ",],\n";
    }
    html << R"HTML(];
const seek=document.querySelector('#seek'),plot=document.querySelector('#plot'),path=document.querySelector('#path');seek.max=rows.length-1;
let running=false,last=0;
function draw(){const k=+seek.value,r=rows[k];document.querySelector('#readout').textContent=`Time ${r[0].toFixed(2)}s | input ${r[1]} deg/s | corrected ${r[2].toFixed(3)} | previous ${r[3].toFixed(3)} | mouse X ${r[4]} / ${r[6]} px`;
const ctx=plot.getContext('2d');ctx.clearRect(0,0,1060,280);const Y=v=>140-v*12;
ctx.strokeStyle='#64748b';ctx.beginPath();ctx.moveTo(30,Y(0));ctx.lineTo(1030,Y(0));ctx.stroke();
for(const [col,color] of [[1,'#cbd5e1'],[3,'#fb923c'],[2,'#60a5fa']]){ctx.strokeStyle=color;ctx.beginPath();rows.forEach((a,i)=>{let x=30+a[0]/16*1000,y=Y(a[col]);i?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke();}
ctx.strokeStyle='#fff';ctx.beginPath();ctx.moveTo(30+r[0]/16*1000,10);ctx.lineTo(30+r[0]/16*1000,270);ctx.stroke();ctx.fillStyle='#fff';ctx.fillText('Input / calibrated signal (deg/s); time 0 to 16 seconds',30,20);
const p=path.getContext('2d');p.clearRect(0,0,1060,200);
for(const [col,color,cy] of [[4,'#60a5fa',60],[6,'#fb923c',140]]){const xs=rows.map(a=>a[col]),lo=Math.min(...xs),hi=Math.max(...xs),X=v=>30+(v-lo)/Math.max(1,hi-lo)*1000;p.strokeStyle=color;p.beginPath();rows.forEach((a,i)=>{i?p.lineTo(X(a[col]),cy):p.moveTo(X(a[col]),cy)});p.stroke();p.fillStyle=color;p.beginPath();p.arc(X(r[col]),cy,8,0,Math.PI*2);p.fill();}}
seek.oninput=draw;document.querySelector('#play').onclick=()=>{running=!running;if(+seek.value===rows.length-1)seek.value=0;};function tick(t){if(running&&t-last>=10){seek.value=Math.min(rows.length-1,+seek.value+Math.max(1,Math.floor((t-last)/10)));draw();if(+seek.value===rows.length-1)running=false;}if(t-last>=10)last=t;requestAnimationFrame(tick)}draw();requestAnimationFrame(tick);
</script>)HTML";
    Check(bool(html), "Report write failed");
    std::cout << "Report: " << path << "\nReverse calibrated samples during slower same-direction motion: corrected="
              << fixedReverse << ", previous=" << oldReverse
              << "\nStopped X displacement after 250ms settling: corrected=" << fixedTail << ", previous=" << oldTail << " px\n";
    Check(fixedReverse == 0 && fixedTail == 0, "Report found a regression in corrected output");
}
}
int main(int argc, char** argv) {
    RegressionTests();
    if (argc == 4 && std::string(argv[1]) == "--report") Report(argv[2], argv[3]);
    else if (argc != 1) { std::cerr << "Usage: gyro-calibration-tests [--report config.json report.html]\n"; return 2; }
}
