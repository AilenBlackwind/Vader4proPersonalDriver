#include "profile_logic.h"

#include <math.h>

// Sends the current RGB color to the controller light bar.
void SendLED(SDL_hid_device* h, uint8_t r, uint8_t g, uint8_t b) {
    if (!h) return;

    unsigned char buf[12] = {0x05, 0xE0, r, g, b, 0, 0, 0, 0, 0, 0, 0};
    SDL_hid_write(h, buf, sizeof(buf));
}

int DetermineMode(const unsigned char* buf, const Config& conf, GyroState& state) {
    // Toggle mode depends on previous button state, so this function is stateful.
    int mode = 0;
    if (conf.mode_source == "trigger") {
        int val = buf[conf.mode_trigger_byte];
        mode = (val >= conf.trigger_threshold_high) ? 2 : (val >= conf.trigger_threshold_low ? 1 : 0);
    } else if (conf.mode_source == "button") {
        bool pressed = (buf[conf.mode_button_byte] & conf.mode_button_mask) != 0;
        if (conf.mode_toggle) {
            if (pressed && !state.prev_toggle_button) state.toggle_state = !state.toggle_state;
            state.prev_toggle_button = pressed;
            mode = state.toggle_state ? 1 : 0;
        } else {
            mode = pressed ? 1 : 0;
        }
    }

    if (mode == 0 && !conf.mode_alt_buttons.empty()) {
        for (auto& alt : conf.mode_alt_buttons) {
            if (buf[alt.byte_index] & alt.mask) {
                mode = alt.mode;
                break;
            }
        }
    }

    return mode;
}

void SetProfileLED(SDL_hid_device* h, const GyroProfile& p, int& cr, int& cg, int& cb) {
    // Avoid redundant HID writes when the visible color has not changed.
    int r = (int)fmax(0, fmin(255, p.led_r * p.led_brightness));
    int g = (int)fmax(0, fmin(255, p.led_g * p.led_brightness));
    int b = (int)fmax(0, fmin(255, p.led_b * p.led_brightness));
    if (r == cr && g == cg && b == cb) return;

    cr = r;
    cg = g;
    cb = b;
    SendLED(h, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
