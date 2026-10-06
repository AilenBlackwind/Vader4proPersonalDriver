#include <SDL3/SDL.h>
#include <cstring>
#include <iostream>
#include <string>

namespace {
char enumerated_path[64] = "test-vader-path";
SDL_hid_device_info device_info = {};
bool enumeration_released = false;
std::string reopened_path;

SDL_hid_device_info* TestEnumerate(unsigned short, unsigned short) {
    device_info.path = enumerated_path;
    return &device_info;
}
SDL_hid_device* TestOpenPath(const char* path) {
    if (enumeration_released) reopened_path = path;
    return reinterpret_cast<SDL_hid_device*>(1);
}
int TestSetNonblocking(SDL_hid_device*, int) { return 0; }
int TestRead(SDL_hid_device*, unsigned char*, size_t, int) { return 64; }
void TestClose(SDL_hid_device*) {}
void TestFreeEnumeration(SDL_hid_device_info*) {
    // Poison the enumeration's storage to simulate losing its owned path.
    // Keep the storage allocated so the failing regression is deterministic.
    std::strcpy(enumerated_path, "released-enumeration");
    enumeration_released = true;
}
}

#define SDL_hid_enumerate TestEnumerate
#define SDL_hid_open_path TestOpenPath
#define SDL_hid_set_nonblocking TestSetNonblocking
#define SDL_hid_read_timeout TestRead
#define SDL_hid_close TestClose
#define SDL_hid_free_enumeration TestFreeEnumeration
// Exercise the actual internal connection helper without controller hardware.
#include "../src/runtime/app.cpp"

int main() {
    if (!OpenHidInput() || !enumeration_released || reopened_path != "test-vader-path") {
        std::cerr << "HID path was not preserved after enumeration release\n";
        return 1;
    }
    std::cout << "HID path lifetime regression test passed\n";
    return 0;
}
