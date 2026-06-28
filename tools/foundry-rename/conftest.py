# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
# Foundry is a fork of Godot Engine 4.6.3-stable (MIT); see NOTICE.
"""Make ``generate_map`` and ``rename`` importable from the tests regardless of
the directory pytest is invoked from."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
