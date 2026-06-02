#pragma once

#include <atomic>
#include <mutex>

#include "../config/config.h"

// Shared runtime state touched by the main loop and background threads.
struct AppRuntime {
    // Guards active_config during watcher writes and main-loop reads.
    std::mutex config_mutex;
    Config active_config;
    std::atomic<bool> config_changed{false};
    std::atomic<bool> app_running{true};
    std::atomic<bool> haptic_running{false};
    std::atomic<bool> device_connected{false};
};
