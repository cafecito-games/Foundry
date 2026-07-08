#!/usr/bin/env python3
"""Run the Foundry editor automation stdio MCP bridge."""

from __future__ import annotations

from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from scripts.foundry_mcp import FoundryMCPStdioServer


def main() -> int:
    FoundryMCPStdioServer().serve()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
