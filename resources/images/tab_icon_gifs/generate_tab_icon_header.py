#!/usr/bin/env python3
"""Embeds tab GIF bytes into Notebook.cpp-ready header."""
import os

GIF_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tab_icon_gifs")
OUTPUT  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "TabIconGIF_data.hpp")
os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)

# Maps GIF stem → the icon name string used in AddPage() calls
ICON_MAP = {
    "tab_3d":          "tab_3d_active",
    "tab_auxiliary":   "tab_auxiliary_active",
    "tab_calibration": "tab_calibration_active",
    "tab_home":        "tab_home_active",
    "tab_monitor":     "tab_monitor_active",
    "tab_multi":       "tab_multi_active",
    "tab_presets":     "tab_presets_active",
    "tab_preview":     "tab_preview_active",
}

lines = [
    "// TabIconGIF_data.hpp — AUTO-GENERATED.  Do not edit.",
    "// Include ONLY from Notebook.cpp.",
    "#pragma once",
    "#include <cstdint>",
    "#include <cstddef>",
    "#include <string>",
    "",
]

entries = []  # (icon_name_string, variable_name, byte_len)

for filename in sorted(os.listdir(GIF_DIR)):
    if not filename.endswith(".gif"):
        continue
    stem = filename[:-4]
    if stem not in ICON_MAP:
        print(f"  SKIP {filename}"); continue
    data = open(os.path.join(GIF_DIR, filename), "rb").read()
    var  = stem.upper().replace("-","_")  # TAB_3D etc.
    lines.append(f"// {filename}  ({len(data)} bytes)")
    lines.append(f"static const uint8_t k_{var}[{len(data)}] = {{")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    entries.append((ICON_MAP[stem], var, len(data)))
    print(f"  {filename}  {len(data):,} bytes")

# GetData() function
lines += [
    "// Returns GIF bytes for the given AddPage icon name.",
    "// Returns nullptr (out_len=0) when no animation is registered.",
    "static const uint8_t* TabGIF_GetData(const std::string& icon_name, size_t& out_len) {",
]
for name, var, sz in entries:
    lines.append(f'    if (icon_name == "{name}") {{ out_len = {sz}; return k_{var}; }}')
lines += [
    "    out_len = 0; return nullptr;",
    "}",
]

open(OUTPUT, "w").write("\n".join(lines) + "\n")
print(f"\nWrote {OUTPUT}  ({len(lines)} lines)")
