#include "haptic_pipe.h"

#include <windows.h>
#include <stdio.h>

// Low-level rumble packet sender used by both the runtime and pipe bridge.
void SendRumble(SDL_hid_device* h, uint8_t left, uint8_t right, uint8_t lT, uint8_t rT) {
    if (!h) return;

    unsigned char buf[12] = {0x05, 0x0F, left, right, lT, rT, 0, 0, 0, 0, 0, 0};
    SDL_hid_write(h, buf, sizeof(buf));
}

void HapticThread(SDL_hid_device* h,
                  std::atomic<bool>& haptic_running,
                  std::atomic<bool>& device_connected) {
    // Bridges a small named-pipe protocol to controller rumble packets.
    const char* pipeName = "\\\\.\\pipe\\VaderHaptic";
    haptic_running = true;
    printf("[HAPTIC] Pipe thread started\n");

    ULONGLONG rumble_end_time = 0;
    bool is_rumbling = false;

    while (haptic_running) {
        HANDLE hPipe = CreateNamedPipeA(pipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                        PIPE_UNLIMITED_INSTANCES, 64, 64, 0, NULL);
        if (hPipe == INVALID_HANDLE_VALUE) {
            if (!haptic_running) break;
            Sleep(1000);
            continue;
        }

        OVERLAPPED ov = {0};
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        bool connected = false;

        if (ConnectNamedPipe(hPipe, &ov)) {
            connected = true;
        } else {
            DWORD connectErr = GetLastError();
            if (connectErr == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (connectErr == ERROR_IO_PENDING) {
                while (haptic_running && device_connected) {
                    if (WaitForSingleObject(ov.hEvent, 500) == WAIT_OBJECT_0) {
                        connected = true;
                        break;
                    }
                    if (is_rumbling && GetTickCount64() >= rumble_end_time) {
                        SendRumble(h, 0, 0);
                        is_rumbling = false;
                    }
                }
            } else {
                CloseHandle(ov.hEvent);
                CloseHandle(hPipe);
                if (!haptic_running) break;
                Sleep(50);
                continue;
            }
        }

        if (!connected) {
            CancelIo(hPipe);
            CloseHandle(ov.hEvent);
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        if (!haptic_running || !device_connected) {
            CloseHandle(ov.hEvent);
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            break;
        }

        unsigned char cmd[6];
        OVERLAPPED readOv = {0};
        readOv.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        while (haptic_running && device_connected) {
            ResetEvent(readOv.hEvent);
            DWORD bytesRead = 0;
            BOOL readOk = ReadFile(hPipe, cmd, 6, &bytesRead, &readOv);

            if (!readOk) {
                DWORD readErr = GetLastError();
                if (readErr == ERROR_IO_PENDING) {
                    while (haptic_running && device_connected) {
                        DWORD res = WaitForSingleObject(readOv.hEvent, 50);
                        if (is_rumbling && GetTickCount64() >= rumble_end_time) {
                            SendRumble(h, 0, 0);
                            is_rumbling = false;
                        }
                        if (res == WAIT_OBJECT_0) {
                            if (!GetOverlappedResult(hPipe, &readOv, &bytesRead, FALSE)) {
                                readErr = GetLastError();
                                bytesRead = 0;
                            }
                            break;
                        }
                    }
                }
                if (readErr == ERROR_BROKEN_PIPE || readErr == ERROR_NO_DATA) break;
                if (bytesRead == 0 && readErr != ERROR_IO_PENDING) break;
            }

            if (bytesRead == 6) {
                SendRumble(h, cmd[0], cmd[1], cmd[2], cmd[3]);
                uint16_t duration = cmd[4] | (cmd[5] << 8);
                if (duration > 0) {
                    rumble_end_time = GetTickCount64() + duration;
                    is_rumbling = true;
                } else {
                    is_rumbling = false;
                }
            } else if (bytesRead == 0) {
                break;
            }
        }

        CancelIo(hPipe);
        CloseHandle(readOv.hEvent);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    if (is_rumbling) SendRumble(h, 0, 0);

    haptic_running = false;
    printf("[HAPTIC] Pipe thread stopped\n");
}
