#!/usr/bin/env python3
"""InEquator RA Tracker TCP CLI.

Speaks the `#`-terminated text protocol on port 4030 (docs/PROTOCOL.md).
Stdlib only.

Usage examples:
    python tracker_cli.py status
    python tracker_cli.py track on
    python tracker_cli.py rate 80000
    python tracker_cli.py rate-steps 400
    python tracker_cli.py deg -0.5
    python tracker_cli.py arcsec 30
    python tracker_cli.py jog cw
    python tracker_cli.py halt
    python tracker_cli.py move 1000
    python tracker_cli.py sync 0
    python tracker_cli.py reverse on
    python tracker_cli.py hold off
    python tracker_cli.py ppm 15
    python tracker_cli.py raw "G"
    python tracker_cli.py --host 192.168.1.50 info
"""

import argparse
import json
import socket
import sys

DEFAULT_HOST = "192.168.4.1"
DEFAULT_PORT = 4030


class TrackerClient:
    def __init__(self, host, port, timeout=3.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), self.timeout)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def request(self, command):
        """Send `command` (without '#') and return the response without '#'."""
        if not self.sock:
            self.connect()
        self.sock.sendall((command + "#").encode("ascii"))
        data = b""
        while not data.endswith(b"#"):
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("connection closed by device")
            data += chunk
        return data[:-1].decode("ascii", "replace")

    def identify(self):
        return self.request("")

    def version(self):
        return int(self.request("V").split()[1])

    def status(self):
        response = self.request("G")
        fields = {}
        for part in response.split(";"):
            key, _, value = part.partition(" ")
            fields[key] = value
        return fields

    def json_status(self):
        return json.loads(self.request("I"))


def main(argv=None):
    parser = argparse.ArgumentParser(description="InEquator RA Tracker CLI")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"device host (default {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"TCP port (default {DEFAULT_PORT})")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="short status (G)")
    sub.add_parser("info", help="full JSON status (I)")
    p = sub.add_parser("track", help="tracking on/off")
    p.add_argument("value", choices=["on", "off"])
    p = sub.add_parser("rate", help="set jog rate, x10000 of sidereal (80000 = 8x)")
    p.add_argument("value", type=int)
    p = sub.add_parser("rate-steps", help="set explicit jog rate in steps/s (1..10000)")
    p.add_argument("value", type=int)
    p = sub.add_parser("deg", help="move by signed degrees (e.g. -0.5)")
    p.add_argument("value", type=float)
    p = sub.add_parser("arcsec", help="move by signed arcseconds (e.g. 30)")
    p.add_argument("value", type=float)
    p = sub.add_parser("jog", help="continuous jog")
    p.add_argument("direction", choices=["cw", "ccw"])
    sub.add_parser("halt", help="stop jog")
    p = sub.add_parser("move", help="relative move N steps at current jog rate")
    p.add_argument("steps", type=int)
    p = sub.add_parser("sync", help="set current position")
    p.add_argument("steps", type=int)
    p = sub.add_parser("reverse", help="direction inversion on/off")
    p.add_argument("value", choices=["on", "off"])
    p = sub.add_parser("hold", help="hold torque on/off")
    p.add_argument("value", choices=["on", "off"])
    p = sub.add_parser("ppm", help="tracking rate PPM correction (-10000..10000)")
    p.add_argument("value", type=int)
    p = sub.add_parser("raw", help="send any raw command")
    p.add_argument("command")

    args = parser.parse_args(argv)
    client = TrackerClient(args.host, args.port)
    try:
        if args.command == "status":
            fields = client.status()
            print("Position :", fields.get("P", "?"))
            print("Tracking :", fields.get("T", "?"))
            print("Jog rate :", fields.get("Q", "?"), "(x10000) /", fields.get("Y", "?"), "steps/s")
            print("Moving   :", fields.get("M", "?"))
        elif args.command == "info":
            print(json.dumps(client.json_status(), indent=2, ensure_ascii=False))
        elif args.command == "track":
            print(client.request(f"B {'1' if args.value == 'on' else '0'}"))
        elif args.command == "rate":
            print(client.request(f"Q {args.value}"))
        elif args.command == "rate-steps":
            print(client.request(f"Y {args.value}"))
        elif args.command == "deg":
            print(client.request(f"MD {round(args.value * 1000)}"))
        elif args.command == "arcsec":
            print(client.request(f"MA {round(args.value)}"))
        elif args.command == "jog":
            print(client.request("M+" if args.direction == "cw" else "M-"))
        elif args.command == "halt":
            print(client.request("S"))
        elif args.command == "move":
            print(client.request(f"M {args.steps}"))
        elif args.command == "sync":
            print(client.request(f"P {args.steps}"))
        elif args.command == "reverse":
            print(client.request(f"R {'1' if args.value == 'on' else '0'}"))
        elif args.command == "hold":
            print(client.request(f"C {'1' if args.value == 'on' else '0'}"))
        elif args.command == "ppm":
            print(client.request(f"D {args.value}"))
        elif args.command == "raw":
            print(client.request(args.command))
        return 0
    except (OSError, ConnectionError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
