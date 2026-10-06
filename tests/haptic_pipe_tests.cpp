#include <windows.h>
#include "../src/haptics/haptic_pipe.h"
#include <iostream>
#include <string>
#include <thread>

namespace {
std::string test_pipe;
std::atomic<int> received_commands{0};
int TestHidWrite(SDL_hid_device*, const unsigned char*, size_t size) {
    ++received_commands;
    return static_cast<int>(size);
}
HANDLE TestCreateNamedPipe(LPCSTR, DWORD mode, DWORD type, DWORD instances,
                          DWORD output_size, DWORD input_size, DWORD timeout,
                          LPSECURITY_ATTRIBUTES attributes) {
    return CreateNamedPipeA(test_pipe.c_str(), mode, type, instances,
                            output_size, input_size, timeout, attributes);
}
HANDLE ConnectClient() {
    for (int i = 0; i < 300; ++i) {
        HANDLE client = CreateFileA(test_pipe.c_str(), GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
        if (client != INVALID_HANDLE_VALUE) return client;
        Sleep(10);
    }
    std::cerr << "Cannot connect test client: " << GetLastError() << '\n';
    return INVALID_HANDLE_VALUE;
}
}

#define CreateNamedPipeA TestCreateNamedPipe
#define SDL_hid_write TestHidWrite
#include "../src/haptics/haptic_pipe.cpp"

int main() {
    // A private pipe prevents the tests from contacting a running driver.
    test_pipe = "\\\\.\\pipe\\VaderDriverHapticTest-" + std::to_string(GetCurrentProcessId());
    std::atomic<bool> running{false}, connected{false};
    HapticThread(nullptr, running, connected);
    if (running) return 1; // A stopped worker must not resurrect itself.

    DWORD before = 0, after = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &before)) return 1;
    running = true;
    connected = true;
    std::thread worker(HapticThread, reinterpret_cast<SDL_hid_device*>(1),
                       std::ref(running), std::ref(connected));
    bool ok = true;
    for (int i = 0; i < 100; ++i) {
        HANDLE client = ConnectClient();
        if (client == INVALID_HANDLE_VALUE) { ok = false; break; }
        unsigned char command[6] = {};
        DWORD written = 0;
        ok = WriteFile(client, command, sizeof(command), &written, nullptr) &&
             written == sizeof(command) && ok;
        for (int attempt = 0; attempt < 300 && received_commands <= i; ++attempt) Sleep(10);
        if (received_commands <= i) {
            std::cerr << "Command not received for client " << i << '\n';
            ok = false;
        }
        CloseHandle(client);
        if (!ok) break;
    }
    // Stop with a connected client and a pending read.
    HANDLE idle_client = ConnectClient();
    ok = idle_client != INVALID_HANDLE_VALUE && ok;
    Sleep(20);
    running = false;
    connected = false;
    worker.join();
    if (idle_client != INVALID_HANDLE_VALUE) CloseHandle(idle_client);

    // Also stop while ConnectNamedPipe is pending, with no client.
    running = true;
    connected = true;
    std::thread waiting(HapticThread, nullptr, std::ref(running), std::ref(connected));
    Sleep(20);
    running = false;
    connected = false;
    waiting.join();
    if (!GetProcessHandleCount(GetCurrentProcess(), &after)) return 1;
    if (!ok || running || after > before + 2) {
        std::cerr << "Haptic pipe lifecycle failed; handles before=" << before
                  << ", after=" << after << '\n';
        return 1;
    }
    std::cout << "Haptic pipe regression tests passed (100 clients; handles "
              << before << " -> " << after << ")\n";
    return 0;
}
