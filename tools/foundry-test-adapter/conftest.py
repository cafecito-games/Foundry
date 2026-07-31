# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
"""Makes ``foundry_test_adapter`` importable from the tests regardless of the
directory the test runner is invoked from."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
