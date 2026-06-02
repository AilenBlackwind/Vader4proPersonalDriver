#pragma once

#include "runtime_state.h"

// High-level application module that owns the runtime lifecycle.
// Runs the full controller runtime: config hot-reload, reconnect loop,
// HID polling, profile switching, and gyro-to-mouse conversion.
int RunApp(AppRuntime& runtime);
