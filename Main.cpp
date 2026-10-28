#include <windows.h>
#include <cstdio>
#include <cstring>

#include "src/runtime/app.h"
#include "src/runtime/runtime_state.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Psapi.lib")

int main(int argc, char** argv) {
    // Main is intentionally thin: it only sets process-level options and then
    // hands control to the application runtime in src/runtime/app.cpp.
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    AppRuntime runtime;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gyro-log") == 0) runtime.gyro_trace = true;
        else { std::fprintf(stderr, "Usage: %s [--gyro-log]\n", argv[0]); return 2; }
    }
    int exit_code = RunApp(runtime);
    return exit_code;
}
