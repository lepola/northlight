#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Geometry stays stable around an orbit without changing coverage/ownership guards."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('validate_world_streaming.py')),run_name='__main__')
