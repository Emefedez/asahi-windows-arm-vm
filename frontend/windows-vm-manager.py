#!/usr/bin/env python3
"""Compatibility wrapper pointing to QEMU VM Manager."""
import runpy
import sys
from pathlib import Path

target = Path(__file__).resolve().parent / "qemu-vm-manager.py"
runpy.run_path(str(target), run_name="__main__")
