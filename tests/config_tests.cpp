#include "../src/config/config.h"

#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Write(const char* path, const std::string& content) {
    std::ofstream f(path);
    f << content;
    Check(f.good(), "Cannot write test fixture");
}

void TestIndices() {
    const char* invalid[] = {"-1", "64", "999999", "4294967296",
                             "18446744073709551615", "1.5", "null", "\"23\""};
    for (const char* index : invalid) {
        const std::string value(index);
        const std::string configs[] = {
            "{\"mode_trigger_byte\":" + value + "}",
            "{\"mode_button_byte\":" + value + "}",
            "{\"mode_alt_buttons\":[{\"byte\":" + value + "}]}",
            "{\"low_profile\":{\"trigger_slowdown_byte\":" + value + "}}",
            "{\"high_profile\":{\"trigger_slowdown_byte\":" + value + "}}"
        };
        for (const auto& config : configs) {
            Write("test.json", config);
            Config c;
            c.trigger_threshold_low = 42;
            c.low_profile.real_world_sens = 7;
            Check(!LoadConfig("test.json", c), "Invalid index accepted");
            Check(c.trigger_threshold_low == 42 && c.low_profile.real_world_sens == 7,
                  "Rejected config changed current settings");
        }
    }
    Write("test.json", R"({"mode_trigger_byte":0,"mode_button_byte":63,
        "mode_alt_buttons":[{"byte":63}],"low_profile":{"trigger_slowdown_byte":0},
        "high_profile":{"trigger_slowdown_byte":63}})");
    Config c;
    Check(LoadConfig("test.json", c), "Valid boundary indices rejected");
    Check(c.mode_trigger_byte == 0 && c.mode_button_byte == 63 &&
          c.mode_alt_buttons[0].byte_index == 63 &&
          c.low_profile.trigger_slowdown_byte == 0 &&
          c.high_profile.trigger_slowdown_byte == 63, "Boundary indices not preserved");

    Write("test.json", R"({"trigger_threshold_low":99,"high_profile":{"led_r":"bad"}})");
    c.trigger_threshold_low = 42;
    Check(!LoadConfig("test.json", c) && c.trigger_threshold_low == 42,
          "Type error partially applied configuration");
}

bool WaitForChange(std::atomic<bool>& changed) {
    for (int i = 0; i < 300; ++i) {
        if (changed.load()) return true;
        Sleep(10);
    }
    return false;
}

void TestWatcherStartup(const char* master) {
    DeleteFileA("master_config.json");
    if (master) Write("master_config.json", master);
    Write("default.json", R"({"trigger_threshold_low":37})");
    Check(ResolveActiveConfigPath() == "default.json", "Startup fallback path failed");
    Config c;
    std::mutex mutex;
    std::atomic<bool> changed{false}, running{true};
    std::thread watcher(ConfigWatcherThread, std::ref(mutex), std::ref(c),
                        std::ref(changed), std::ref(running));
    const bool loaded = WaitForChange(changed);
    running = false;
    watcher.join();
    Check(loaded && c.trigger_threshold_low == 37, "Watcher did not load fallback config");
}

void TestWatcherRecovery() {
    Write("master_config.json", R"({"default":"custom.json"})");
    Write("custom.json", R"({"trigger_threshold_low":41})");
    Config c;
    std::mutex mutex;
    std::atomic<bool> changed{false}, running{true};
    std::thread watcher(ConfigWatcherThread, std::ref(mutex), std::ref(c),
                        std::ref(changed), std::ref(running));
    bool ok = WaitForChange(changed);
    changed = false;
    Write("master_config.json", R"({"default":123})");
    Write("custom.json", R"({"trigger_threshold_low":42})");
    ok = WaitForChange(changed) && ok;
    {
        std::lock_guard<std::mutex> lock(mutex);
        ok = ok && c.trigger_threshold_low == 42;
    }
    changed = false;
    Write("custom.json", R"({"trigger_threshold_low":99,"mode_trigger_byte":64})");
    Sleep(1200);
    {
        std::lock_guard<std::mutex> lock(mutex);
        ok = ok && c.trigger_threshold_low == 42 && !changed;
    }
    Write("custom.json", R"({"trigger_threshold_low":43})");
    ok = WaitForChange(changed) && ok;
    running = false;
    watcher.join();
    Check(ok && c.trigger_threshold_low == 43,
          "Hot reload did not preserve/recover the last valid configuration");
}
} // namespace

// Run in an isolated temporary directory: this creates config fixtures.
int main() {
    try {
        TestIndices();
        const char* masters[] = {nullptr, "{", "null", "[]", "{\"default\":123}",
                                "{\"apps\":[]}", "{\"apps\":{\"test.exe\":123}}"};
        for (const char* master : masters) {
            TestWatcherStartup(master);
        }
        TestWatcherRecovery();
        std::cout << "Config regression tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
