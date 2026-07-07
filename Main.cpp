#include <windows.h>

#include "src/runtime/app.h"
#include "src/runtime/runtime_state.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Psapi.lib")

int main() {
    // Main is intentionally thin: it only sets process-level options and then
    // hands control to the application runtime in src/runtime/app.cpp.
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    AppRuntime runtime;
    int exit_code = RunApp(runtime);
    return exit_code;
}
