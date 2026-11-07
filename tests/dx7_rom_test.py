#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the checked-in DX7 bulk ROMs and their FM6 factory conversion."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_fm6_patches as fm6

voices = fm6.dx7_factory_voices()
assert len(voices) == 64, len(voices)
assert voices[0][0].startswith("R1-01 ")
assert voices[31][0].startswith("R1-32 ")
assert voices[32][0].startswith("R2-01 ")
assert voices[63][0].startswith("R2-32 ")
for name, record in voices:
    assert name and len(name) <= 10, (name, len(name))
    assert len(record) == 128, (name, len(record))
    assert all(0 <= value <= 127 for value in record), name

factory = fm6.factory_voices()
assert len(factory) == 72, len(factory)
assert factory[:8][0][0] == "F1 TINE EP"
assert factory[8][0].startswith("R1-01 ")
assert factory[-1][0].startswith("R2-32 ")
print("DX7 ROMs: 2 banks, 64 voices verified; FM6 factory total: 72")
