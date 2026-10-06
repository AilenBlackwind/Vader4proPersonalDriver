#pragma once

#include <atomic>
#include <cstdint>

#include <SDL3/SDL_hidapi.h>

// Sends one low-level rumble packet to the controller.
void SendRumble(SDL_hid_device* h, uint8_t left, uint8_t right, uint8_t lT = 0, uint8_t rT = 0);

// Named-pipe worker that accepts simple haptic commands from external tools.
// The caller sets haptic_running before launch; the worker never re-enables it.
void HapticThread(SDL_hid_device* h,
                  std::atomic<bool>& haptic_running,
                  std::atomic<bool>& device_connected);
