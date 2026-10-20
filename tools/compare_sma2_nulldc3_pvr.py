#!/usr/bin/env python3
"""Compare SMA2 nullDC3 RGB565 captures using logged scanline ownership.

This models refsw2's color conversion, not physical Dreamcast scanout.
"""

import argparse
from collections import Counter
from hashlib import sha256
from pathlib import Path
import re
import struct
import sys


WIDTH = 240
HEIGHT = 160
PIXELS = WIDTH * HEIGHT
BYTES = PIXELS * 2
LINE_PREFIX = "SMA2_NATIVE_PVR_LINE_CLASS frame="
COUNTER_PREFIX = "SMA2_NATIVE_PVR_CAPTURE_COUNTERS frame="
CAPTURE_PREFIX = "SMA2_NATIVE_PVR_CAPTURE frame="
ROW_RE = re.compile(r"(\d+):(native|fallback-[0-9a-fA-F]{2}|missing)\Z")


def records_for_frame(lines, prefix, frame):
    marker = f"{prefix}{frame} "
    return [line.strip() for line in lines if line.startswith(marker)]


def one_record(lines, prefix, frame):
    matches = records_for_frame(lines, prefix, frame)
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one {prefix.strip()} record for frame {frame}; "
            f"found {len(matches)}"
        )
    return matches[0]


def fields(record):
    return dict(re.findall(r"\b([a-z_][a-z0-9_]*)=([^\s]+)", record))


def require_int(record_fields, name):
    if name not in record_fields:
        raise ValueError(f"capture metadata lacks {name}")
    return int(record_fields[name], 10)


def ownership_for_frame(lines, frame):
    record = one_record(lines, LINE_PREFIX, frame)
    tokens = record.split()[2:]
    if len(tokens) != HEIGHT:
        raise ValueError(f"frame {frame} has {len(tokens)} row entries; expected {HEIGHT}")
    ownership = [None] * HEIGHT
    for token in tokens:
        match = ROW_RE.fullmatch(token)
        if not match:
            raise ValueError(f"malformed ownership token: {token}")
        row = int(match.group(1))
        kind = match.group(2)
        if row >= HEIGHT or ownership[row] is not None:
            raise ValueError(f"duplicate or out-of-range ownership row: {row}")
        if kind == "missing":
            raise ValueError(f"frame {frame} lacks ownership for row {row}")
        ownership[row] = "native" if kind == "native" else "fallback"
    if any(kind is None for kind in ownership):
        raise ValueError(f"frame {frame} ownership does not cover every row")

    counts = Counter(ownership)
    counter_fields = fields(one_record(lines, COUNTER_PREFIX, frame))
    capture_fields = fields(one_record(lines, CAPTURE_PREFIX, frame))
    expected = {
        "rows": HEIGHT,
        "native": counts["native"],
        "fallback": counts["fallback"],
    }
    for name, value in expected.items():
        if require_int(counter_fields, name) != value:
            raise ValueError(f"frame {frame} counter {name} conflicts with row ownership")
        if name != "rows" and require_int(capture_fields, name) != value:
            raise ValueError(f"frame {frame} capture {name} conflicts with row ownership")
    for name in ("captured", "displayed"):
        if require_int(counter_fields, name) != HEIGHT:
            raise ValueError(f"frame {frame} has incomplete {name} rows")
    if require_int(counter_fields, "fallback_rows") != counts["fallback"]:
        raise ValueError(f"frame {frame} backend fallback count conflicts with row ownership")
    if require_int(capture_fields, "pixels") != PIXELS:
        raise ValueError(f"frame {frame} capture pixel count is incomplete")
    if counter_fields.get("result") != "PASS":
        raise ValueError(f"frame {frame} capture counters did not pass")
    for name, value in (("ready", 0), ("render_done", 0),
                        ("vblank", 1), ("stable", 1)):
        if require_int(capture_fields, name) != value:
            raise ValueError(f"frame {frame} capture {name} is not {value}")
    pages = {
        name: int(capture_fields[name], 0)
        for name in ("front", "back", "front_after", "back_after")
        if name in capture_fields
    }
    if len(pages) != 4:
        raise ValueError(f"frame {frame} capture lacks complete front/back page metadata")
    if pages["front"] == pages["back"]:
        raise ValueError(f"frame {frame} front and back pages are not distinct")
    if pages["front"] != pages["front_after"] or \
       pages["back"] != pages["back_after"]:
        raise ValueError(f"frame {frame} front/back page ownership changed during capture")
    return ownership, counts, capture_fields


def fallback_rgb565(pixel):
    """refsw2 RGB565 texture expansion followed by RGB565 framebuffer pack."""
    r5 = (pixel >> 11) & 31
    g6 = (pixel >> 5) & 63
    b5 = pixel & 31
    return (((r5 << 3) * 31 // 255) << 11) | \
           (((g6 << 2) * 63 // 255) << 5) | \
           ((b5 << 3) * 31 // 255)


def native_argb1555(pixel):
    """GBA RGB555 palette through PVR ARGB1555 and refsw2 framebuffer pack."""
    r5 = (pixel >> 11) & 31
    g5 = ((pixel >> 5) & 63) >> 1
    b5 = pixel & 31
    return (((r5 << 3) * 31 // 255) << 11) | \
           (((g5 << 3) * 63 // 255) << 5) | \
           ((b5 << 3) * 31 // 255)


def capture_words(path):
    data = path.read_bytes()
    if len(data) != BYTES:
        raise ValueError(f"{path}: {len(data)} bytes; expected {BYTES}")
    return data, struct.unpack(f"<{PIXELS}H", data)


def capture_fnv(data):
    value = 1469598103934665603
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def describe(name, mismatches):
    print(f"{name}_mismatches={len(mismatches)}/{PIXELS}")
    if mismatches:
        index, expected, actual = mismatches[0]
        print(f"{name}_first_x={index % WIDTH} first_y={index // WIDTH} "
              f"expected=0x{expected:04x} actual=0x{actual:04x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", type=Path, required=True,
                        help="full guest serial log containing LINE_CLASS and capture records")
    parser.add_argument("--frame", type=int, required=True)
    parser.add_argument("--software", type=Path, required=True,
                        help="same-run 240x160 software oracle RGB565 file")
    parser.add_argument("--pvr", type=Path, required=True,
                        help="same-frame captured PVR front buffer RGB565 file")
    args = parser.parse_args()
    if args.frame < 0:
        parser.error("--frame must be nonnegative")

    try:
        lines = args.serial.read_text(errors="replace").splitlines()
        ownership, counts, capture_fields = ownership_for_frame(lines, args.frame)
        software_bytes, software = capture_words(args.software)
        pvr_bytes, pvr = capture_words(args.pvr)
        for name, data in (("software_rgb565_fnv", software_bytes),
                           ("pvr_rgb565_fnv", pvr_bytes)):
            if capture_fields.get(name, "").lower() != capture_fnv(data):
                raise ValueError(f"frame {args.frame} {name} does not match capture file")
    except (OSError, ValueError) as exc:
        print(f"RESULT=ERROR: {exc}", file=sys.stderr)
        return 2

    print(f"frame={args.frame} native_rows={counts['native']} "
          f"fallback_rows={counts['fallback']}")
    print(f"software_sha256={sha256(software_bytes).hexdigest()}")
    print(f"pvr_sha256={sha256(pvr_bytes).hexdigest()}")
    raw = []
    modeled = []
    for index, (source, actual) in enumerate(zip(software, pvr)):
        if source != actual:
            raw.append((index, source, actual))
        model = native_argb1555 if ownership[index // WIDTH] == "native" else fallback_rgb565
        expected = model(source)
        if expected != actual:
            modeled.append((index, expected, actual))
    describe("raw", raw)
    describe("ownership_model", modeled)
    print(f"RESULT={'PASS' if not modeled else 'FAIL'}")
    return 0 if not modeled else 1


if __name__ == "__main__":
    sys.exit(main())
