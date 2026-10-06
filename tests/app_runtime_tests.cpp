#include <SDL3/SDL.h>
#include "../src/runtime/app.h"
#include "../src/haptics/haptic_pipe.h"
#include "../src/gyro/profile_logic.h"
#include <iostream>
#include <string>

namespace {
SDL_JoystickID ids[] = {1, 2, 3, 4};
SDL_hid_device_info hid_info = {};
char hid_path[] = "vader-input";
int rejected_sensor = 0;
int rejected_enable = 0;
bool fail_gamepad_open = false;
bool invalid_device_opened = false;
int candidate_closes = 0;
int scans = 0;
int output_attempts = 0;
int retry_delays = 0;
AppRuntime* test_runtime = nullptr;

SDL_JoystickID* TestGamepads(int* count) { ++scans; *count = 4; return ids; }
void TestFree(void*) {}
Uint16 TestVendor(SDL_JoystickID id) { return id == 1 ? 0x1234 : 0x04B4; }
Uint16 TestProduct(SDL_JoystickID id) { return id == 2 ? 0x5678 : 0x2412; }
SDL_Gamepad* TestOpenGamepad(SDL_JoystickID id) {
    if (id < 3) invalid_device_opened = true;
    if (id == 4 && fail_gamepad_open) { fail_gamepad_open = false; return nullptr; }
    return reinterpret_cast<SDL_Gamepad*>(static_cast<uintptr_t>(id));
}
void TestCloseGamepad(SDL_Gamepad*) { ++candidate_closes; }
bool TestHasSensor(SDL_Gamepad* pad, SDL_SensorType type) {
    return pad != reinterpret_cast<SDL_Gamepad*>(3) || type != rejected_sensor;
}
bool TestEnableSensor(SDL_Gamepad* pad, SDL_SensorType type, bool) {
    return pad != reinterpret_cast<SDL_Gamepad*>(3) || type != rejected_enable;
}
SDL_JoystickID TestGamepadID(SDL_Gamepad*) { return 4; }
bool TestConnected(SDL_Gamepad*) { test_runtime->app_running = false; return false; }
bool TestPoll(SDL_Event*) { return false; } // No GAMEPAD_ADDED events at all.
bool TestInit(SDL_InitFlags) { return true; }
void TestQuit() {}
void TestDelay(Uint32 ms) { if (ms == 100) ++retry_delays; }
SDL_hid_device_info* TestEnumerate(unsigned short, unsigned short) {
    hid_info.path = hid_path;
    return &hid_info;
}
void TestFreeEnumeration(SDL_hid_device_info*) {}
SDL_hid_device* TestOpenPath(const char*) { return reinterpret_cast<SDL_hid_device*>(1); }
SDL_hid_device* TestOpenOutput(unsigned short, unsigned short, const wchar_t*) {
    ++output_attempts;
    return output_attempts == 1 ? nullptr : reinterpret_cast<SDL_hid_device*>(2);
}
void TestCloseHid(SDL_hid_device*) {}
int TestSetNonblocking(SDL_hid_device*, int) { return 0; }
int TestRead(SDL_hid_device*, unsigned char*, size_t, int) { return 64; }
int TestWrite(SDL_hid_device*, const unsigned char*, size_t size) { return static_cast<int>(size); }
void TestHapticThread(SDL_hid_device*, std::atomic<bool>&, std::atomic<bool>&) {}
void TestSendLED(SDL_hid_device*, uint8_t, uint8_t, uint8_t) {}
}

#define SDL_GetGamepads TestGamepads
#define SDL_free TestFree
#define SDL_GetGamepadVendorForID TestVendor
#define SDL_GetGamepadProductForID TestProduct
#define SDL_OpenGamepad TestOpenGamepad
#define SDL_CloseGamepad TestCloseGamepad
#define SDL_GamepadHasSensor TestHasSensor
#define SDL_SetGamepadSensorEnabled TestEnableSensor
#define SDL_GetGamepadID TestGamepadID
#define SDL_GamepadConnected TestConnected
#define SDL_PollEvent TestPoll
#define SDL_Init TestInit
#define SDL_Quit TestQuit
#define SDL_Delay TestDelay
#define SDL_hid_enumerate TestEnumerate
#define SDL_hid_free_enumeration TestFreeEnumeration
#define SDL_hid_open_path TestOpenPath
#define SDL_hid_open TestOpenOutput
#define SDL_hid_close TestCloseHid
#define SDL_hid_set_nonblocking TestSetNonblocking
#define SDL_hid_read_timeout TestRead
#define SDL_hid_write TestWrite
#define HapticThread TestHapticThread
#define SendLED TestSendLED
#include "../src/runtime/app.cpp"

int main() {
    // Controller 3 is the right model, but lacks a usable sensor. Controller 4
    // is ready. Check both missing sensors and both enable failures.
    for (int test = 0; test < 4; ++test) {
        rejected_sensor = test < 2 ? (test == 0 ? SDL_SENSOR_GYRO : SDL_SENSOR_ACCEL) : 0;
        rejected_enable = test >= 2 ? (test == 2 ? SDL_SENSOR_GYRO : SDL_SENSOR_ACCEL) : 0;
        const int closed_before = candidate_closes;
        if (OpenVaderGamepad() != reinterpret_cast<SDL_Gamepad*>(4) ||
            candidate_closes != closed_before + 1 || invalid_device_opened) {
            std::cerr << "Controller selection/capability check failed\n";
            return 1;
        }
    }
    rejected_sensor = SDL_SENSOR_ACCEL;
    rejected_enable = 0;
    fail_gamepad_open = true;
    scans = 0;
    AppRuntime runtime;
    test_runtime = &runtime;
    if (RunApp(runtime) != 0 || scans != 3 || output_attempts != 2 ||
        retry_delays != 2 || invalid_device_opened) {
        std::cerr << "Existing device was not retried after gamepad/HID open failures\n";
        return 1;
    }
    std::cout << "Controller selection and reconnect regression tests passed\n";
    return 0;
}
