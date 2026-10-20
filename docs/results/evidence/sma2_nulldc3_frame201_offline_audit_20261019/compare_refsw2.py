#!/usr/bin/env python3
"""Offline comparison of frame-201 guest RGB565 captures under documented nullDC3 refsw2 conversion."""
from pathlib import Path
from hashlib import sha256
from struct import unpack

root = Path(__file__).resolve().parent / "pcroot"
software_path = root / "sma2_gameplay_frame201_software.rgb565"
pvr_path = root / "sma2_gameplay_frame201_pvr.rgb565"
software_bytes = software_path.read_bytes()
pvr_bytes = pvr_path.read_bytes()
assert len(software_bytes) == len(pvr_bytes) == 240 * 160 * 2
software = unpack("<38400H", software_bytes)
pvr = unpack("<38400H", pvr_bytes)

def refsw2(rgb565):
    r = (((rgb565 >> 11) & 31) << 3) * 31 // 255
    g = (((rgb565 >> 5) & 63) << 2) * 63 // 255
    b = ((rgb565 & 31) << 3) * 31 // 255
    return (r << 11) | (g << 5) | b

def native_palette_refsw2(rgb565):
    """Model this frame's 5-bit GBA palette -> ARGB1555 -> refsw2 path."""
    r5 = (rgb565 >> 11) & 31
    g5 = ((rgb565 >> 5) & 63) >> 1
    b5 = rgb565 & 31
    r = (r5 << 3) * 31 // 255
    g = (g5 << 3) * 63 // 255
    b = (b5 << 3) * 31 // 255
    return (r << 11) | (g << 5) | b

raw = [(i, a, b) for i, (a, b) in enumerate(zip(software, pvr)) if a != b]
adjusted = [(i, refsw2(a), b) for i, (a, b) in enumerate(zip(software, pvr)) if refsw2(a) != b]
native_adjusted = [(i, native_palette_refsw2(a), b)
                   for i, (a, b) in enumerate(zip(software, pvr))
                   if native_palette_refsw2(a) != b]
for label, path, data in (("software", software_path, software_bytes), ("pvr", pvr_path, pvr_bytes)):
    print(f"{label}_sha256={sha256(data).hexdigest()} bytes={len(data)} path={path}")
for label, diffs in (("raw", raw), ("rgb565_fallback_refsw2_model", adjusted),
                     ("argb1555_native_palette_refsw2_model", native_adjusted)):
    print(f"{label}_mismatches={len(diffs)}/38400")
    if diffs:
        i, expected, actual = diffs[0]
        print(f"{label}_first_x={i % 240} first_y={i // 240} expected=0x{expected:04x} actual=0x{actual:04x}")
