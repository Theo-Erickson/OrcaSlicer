#!/usr/bin/env python3
import os

GIF_DIR = os.path.join(os.path.dirname(__file__), "gifs")
OUTPUT  = os.path.join(os.path.dirname(__file__), "src", "PrintStatusIconGIF.hpp")

STATE_MAP = {
    "status_idle":            "PrintState::IDLE",
    "status_slicing":         "PrintState::SLICING",
    "status_sliced":          "PrintState::SLICED",
    "status_sending":         "PrintState::SENDING",
    "status_prepare":         "PrintState::PREPARE",
    "status_running":         "PrintState::RUNNING",
    "status_pause":           "PrintState::PAUSE",
    "status_filament_change": "PrintState::FILAMENT_CHANGE",
    "status_calibrating":     "PrintState::CALIBRATING",
    "status_finish":          "PrintState::FINISH",
    "status_failed":          "PrintState::FAILED",
    "status_offline":         "PrintState::OFFLINE",
}

lines = [
    "// PrintStatusIconGIF.hpp — AUTO-GENERATED",
    "#pragma once",
    '#include "PrintStatusIcon.hpp"',
    "#include <cstdint>",
    "#include <cstddef>",
    "",
    "namespace Slic3r { namespace GUI { namespace PrintStatusIconGIF {",
    "",
]

entries = []
for filename in sorted(os.listdir(GIF_DIR)):
    if not filename.endswith(".gif"): continue
    stem = filename[:-4]
    if stem not in STATE_MAP:
        print(f"  SKIP {filename}"); continue
    data = open(os.path.join(GIF_DIR, filename), "rb").read()
    var  = stem.upper()
    lines.append(f"// {filename} ({len(data)} bytes)")
    lines.append(f"static const uint8_t k_{var}[{len(data)}] = {{")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    entries.append((STATE_MAP[stem], var, len(data)))
    print(f"  {filename}  {len(data)} bytes")

lines += [
    "inline const uint8_t* GetData(PrintState state, size_t& out_len) {",
    "    switch (state) {",
]
for ev, var, sz in entries:
    lines += [f"    case {ev}: out_len={sz}; return k_{var};"]
lines += [
    "    default: out_len=0; return nullptr;",
    "    }",
    "}",
    "",
    "} } }  // Slic3r::GUI::PrintStatusIconGIF",
]

open(OUTPUT, "w").write("\n".join(lines) + "\n")
print(f"\nWrote {OUTPUT}")
