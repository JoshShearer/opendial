#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Adds a 'typer' test tool (CW = a, CCW = Shift+b, click = Return) to a config file."""
import json
import sys

p = sys.argv[1]
c = json.load(open(p))
c['profiles'][0]['tools'].append({'id': 'typer', 'name': 'Typer', 'bindings': {
    'rotate_cw': {'type': 'key', 'keys': 'a'},
    'rotate_ccw': {'type': 'key', 'keys': 'Shift+b'},
    'click': {'type': 'key', 'keys': 'Return'}}})
json.dump(c, open(p, 'w'))
