#pragma once

#include <cstdint>

#include "../../hidapi.h"
#include "../config/config.h"
#include "gyro_processing.h"

// Sends the currently requested LED color to the controller.
void SendLED(hid_device* h, uint8_t r, uint8_t g, uint8_t b);

// Resolves the active gyro mode from the latest HID report and toggle state.
int DetermineMode(const unsigned char* buf, const Config& conf, GyroState& state);

// Applies the profile LED color only when it differs from the current device color.
void SetProfileLED(hid_device* h, const GyroProfile& p, int& cr, int& cg, int& cb);
