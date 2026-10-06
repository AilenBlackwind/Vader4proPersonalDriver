#!/usr/bin/env python3
"""
Simple UDP listener for JoyShockMapper telemetry.

Usage:
    python script/telemetry_dump.py --port 8974
"""

import argparse
import json
import signal
import socket
import sys
import time
from typing import Optional


stop_requested = False


def _handle_sigint(signum, frame):
    del signum, frame
    global stop_requested
    stop_requested = True


def main() -> None:
    parser = argparse.ArgumentParser(description="Print telemetry packets emitted by JoyShockMapper.")
    parser.add_argument("--port", type=int, default=8974, help="UDP port to listen on (default: 8974).")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(("127.0.0.1", args.port))
    except OSError as exc:
        print(f"Failed to bind to port {args.port}: {exc}", file=sys.stderr)
        sys.exit(1)

    sock.settimeout(0.5)  # Allow Ctrl+C to be detected while waiting for packets.

    signal.signal(signal.SIGINT, _handle_sigint)

    print(f"Listening for telemetry on udp://127.0.0.1:{args.port} ...")
    global stop_requested
    while not stop_requested:
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except KeyboardInterrupt:
            break

        line = data.decode("utf-8", errors="ignore")
        try:
            payload = json.loads(line)
        except json.JSONDecodeError:
            print(f"Malformed packet: {line[:120]}...", file=sys.stderr)
            continue

        omega = payload.get("omega")
        sens_x = payload.get("sensX")
        sens_y = payload.get("sensY")
        normalized = payload.get("t")
        timestamp = payload.get("ts")

        def fmt(value: Optional[float], precision: int = 2) -> str:
            if isinstance(value, (int, float)):
                return f"{value:.{precision}f}"
            return "nan"

        print(
            f"[{time.strftime('%H:%M:%S')}] ts={timestamp} ω={fmt(omega)}°/s "
            f"t={fmt(normalized)} sens=({fmt(sens_x, 3)}, {fmt(sens_y, 3)})"
        )

    print("\nStopping listener.")
    sock.close()


if __name__ == "__main__":
    main()
