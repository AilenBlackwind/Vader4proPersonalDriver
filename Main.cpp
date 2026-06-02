#include <windows.h>

#include "hidapi.h"
#include "src/runtime/app.h"
#include "src/runtime/runtime_state.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Psapi.lib")

int main() {
    // Main is intentionally thin: it only sets process-level options and then
    // hands control to the application runtime in src/runtime/app.cpp.
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    if (hid_init()) return 1;

    AppRuntime runtime;
    int exit_code = RunApp(runtime);
    hid_exit();
    return exit_code;
}
