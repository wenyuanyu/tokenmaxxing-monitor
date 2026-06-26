#!/usr/bin/env python3
import csv
import struct
import sys

RECORD = struct.Struct("<HBBIIHBBhH")
MAGIC = 0xB17E
VERSION = 1


def iter_records(path):
    with open(path, "rb") as f:
        offset = 0
        while True:
            chunk = f.read(RECORD.size)
            if len(chunk) < RECORD.size:
                return
            if all(b == 0xFF for b in chunk):
                return
            magic, version, flags, seq, uptime_s, mv, raw_pct, shown_pct, temp_x10, hum_x10 = RECORD.unpack(chunk)
            if magic == MAGIC and version == VERSION:
                yield {
                    "offset": offset,
                    "seq": seq,
                    "uptime_s": uptime_s,
                    "millivolts": mv,
                    "raw_percent": raw_pct,
                    "shown_percent": shown_pct,
                    "usb_connected": flags & 1,
                    "temp_c": "" if temp_x10 == -32768 else f"{temp_x10 / 10:.1f}",
                    "humidity": "" if hum_x10 == 65535 else f"{hum_x10 / 10:.1f}",
                }
            offset += RECORD.size


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} batlog.bin", file=sys.stderr)
        return 2
    fields = [
        "offset",
        "seq",
        "uptime_s",
        "millivolts",
        "raw_percent",
        "shown_percent",
        "usb_connected",
        "temp_c",
        "humidity",
    ]
    writer = csv.DictWriter(sys.stdout, fieldnames=fields)
    writer.writeheader()
    writer.writerows(iter_records(sys.argv[1]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
