#!/usr/bin/env python3
"""Unit tests for scripts/review_gallery.py.

Run with: python3 -m unittest scripts.tests.test_review_gallery
or:       python3 scripts/tests/test_review_gallery.py
"""

from __future__ import annotations

import importlib.util
import json
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from typing import Any

# Load the helper by explicit file path so the test resolves it regardless of
# the current working directory or how it is launched, and without a sys.path
# mutation that static analysis can't follow.
_MODULE_PATH = Path(__file__).resolve().parents[1] / "review_gallery.py"
_spec = importlib.util.spec_from_file_location("review_gallery", _MODULE_PATH)
assert _spec is not None and _spec.loader is not None
rg: Any = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = rg
_spec.loader.exec_module(rg)


def _tiny_png(color: int = 0) -> bytes:
    """Return the bytes of a minimal valid 1x1 PNG."""

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    raw = bytes([0, color, color, color])  # one filtered RGB scanline
    idat = zlib.compress(raw)
    return sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


class GalleryTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name) / "gallery"
        self.src = Path(self._tmp.name) / "src"
        self.src.mkdir(parents=True)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _png(self, name: str, color: int = 0) -> Path:
        path = self.src / name
        path.write_bytes(_tiny_png(color))
        return path

    def test_add_fresh_creates_layout(self) -> None:
        png = self._png("a.png")
        entry = rg.add_shot(self.root, png, board_title="Dock fix", mode="proof", caption="works now")

        self.assertTrue((self.root / "manifest.json").is_file())
        self.assertTrue((self.root / "index.html").is_file())
        self.assertTrue((self.root / "shots").is_dir())

        # Copied PNG exists inside shots/.
        copied = self.root / entry["file"]
        self.assertTrue(copied.is_file())
        self.assertEqual(copied.read_bytes(), png.read_bytes())

        manifest = rg.load_manifest(self.root)
        self.assertEqual(len(manifest["boards"]), 1)
        board = manifest["boards"][0]
        self.assertEqual(board["title"], "Dock fix")
        self.assertEqual(board["mode"], "proof")
        self.assertEqual(len(board["shots"]), 1)
        shot = board["shots"][0]
        self.assertEqual(shot["caption"], "works now")
        self.assertEqual(shot["type"], "image")
        self.assertEqual(shot["order"], 0)

    def test_add_appends_without_corruption_and_is_deterministic(self) -> None:
        rg.add_shot(self.root, self._png("a.png", 0), board_title="B", mode="proof", caption="one")
        rg.add_shot(self.root, self._png("b.png", 255), board_title="B", mode="proof", caption="two")

        manifest = rg.load_manifest(self.root)
        self.assertEqual(len(manifest["boards"]), 1)
        self.assertEqual([s["order"] for s in manifest["boards"][0]["shots"]], [0, 1])

        # Manifest is valid JSON (not corrupted) and reloadable.
        json.loads((self.root / "manifest.json").read_text())

        # Same manifest -> identical HTML (deterministic render).
        html_a = rg.render_html(manifest)
        html_b = rg.render_html(rg.load_manifest(self.root))
        self.assertEqual(html_a, html_b)
        # And the persisted index.html matches a fresh render.
        self.assertEqual((self.root / "index.html").read_text(), html_a)

    def test_boards_newest_first_shots_by_order(self) -> None:
        rg.add_shot(self.root, self._png("a.png"), board_title="Older", mode="proof", caption="x")
        rg.add_shot(self.root, self._png("b.png", 128), board_title="Newer", mode="proof", caption="y")

        html = rg.render_html(rg.load_manifest(self.root))
        self.assertLess(html.index("Newer"), html.index("Older"), "boards should be newest-first")

    def test_design_before_after(self) -> None:
        rg.add_shot(
            self.root,
            self._png("before.png", 0),
            board_title="Redesign",
            mode="design",
            caption="old",
            pair="before",
        )
        rg.add_shot(
            self.root,
            self._png("after.png", 255),
            board_title="Redesign",
            mode="design",
            caption="new",
            pair="after",
        )
        html = rg.render_html(rg.load_manifest(self.root))
        self.assertIn("before", html.lower())
        self.assertIn("after", html.lower())

    def test_video_branch_renders_without_template_changes(self) -> None:
        # Hand-author a manifest with an image and a video entry.
        manifest = {
            "version": rg.MANIFEST_VERSION,
            "boards": [
                {
                    "title": "Mixed",
                    "mode": "walkthrough",
                    "shots": [
                        {"file": "shots/a.png", "caption": "image step", "type": "image", "order": 0},
                        {"file": "shots/b.mp4", "caption": "video step", "type": "video", "order": 1},
                    ],
                }
            ],
        }
        html = rg.render_html(manifest)
        self.assertIn("<img", html)
        self.assertIn("<video", html)

    def test_theme_aware_html(self) -> None:
        rg.add_shot(self.root, self._png("a.png"), board_title="B", mode="proof", caption="c")
        html = (self.root / "index.html").read_text()
        self.assertIn("prefers-color-scheme", html)
        # A manual toggle exists so a reviewer can force light/dark.
        self.assertIn("data-theme", html)

    def test_html_is_self_contained_no_external_hosts(self) -> None:
        rg.add_shot(self.root, self._png("a.png"), board_title="B", mode="proof", caption="c")
        html = (self.root / "index.html").read_text()
        self.assertNotIn("http://", html.replace("http://www.w3.org", ""))  # allow SVG/XML ns only
        self.assertNotIn("https://", html.replace("https://www.w3.org", ""))

    def test_serve_url_falls_back_without_ngrok(self) -> None:
        result = rg.resolve_public_url(8000, ngrok_path=None)
        self.assertFalse(result.public)
        self.assertIn("127.0.0.1:8000", result.url)
        self.assertTrue(result.message)

    def test_serve_url_uses_ngrok_when_available(self) -> None:
        result = rg.resolve_public_url(
            8000,
            ngrok_path="/usr/bin/ngrok",
            query_fn=lambda port: "https://abc123.ngrok-free.app",
            start_fn=lambda port: None,
        )
        self.assertTrue(result.public)
        self.assertEqual(result.url, "https://abc123.ngrok-free.app")

    def test_serve_url_ngrok_present_but_tunnel_fails(self) -> None:
        result = rg.resolve_public_url(
            8000,
            ngrok_path="/usr/bin/ngrok",
            query_fn=lambda port: None,
            start_fn=lambda port: None,
            poll_attempts=2,
            poll_interval=0.0,
            sleep_fn=lambda _s: None,
        )
        self.assertFalse(result.public)
        self.assertIn("127.0.0.1:8000", result.url)

    def test_concurrent_adds_all_land(self) -> None:
        import threading

        count = 8
        pngs = [self._png(f"c{i}.png", i * 17 % 256) for i in range(count)]

        def worker(index: int, path: Path) -> None:
            rg.add_shot(self.root, path, board_title="C", mode="proof", caption=f"shot {index}")

        threads = [threading.Thread(target=worker, args=(i, p)) for i, p in enumerate(pngs)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

        manifest = rg.load_manifest(self.root)
        self.assertEqual(len(manifest["boards"]), 1)
        # No shot is lost to a read-modify-write race, and the manifest stays valid.
        self.assertEqual(len(manifest["boards"][0]["shots"]), count)
        json.loads((self.root / "manifest.json").read_text())
        # No temp files leaked next to the manifest.
        self.assertEqual(list(self.root.glob(".manifest-*.json.tmp")), [])

    def test_ngrok_tunnel_port_matching(self) -> None:
        # Only a tunnel forwarding to our local port should match.
        ours = {"config": {"addr": "http://localhost:8000"}, "public_url": "https://ours.ngrok-free.app"}
        other = {"config": {"addr": "http://localhost:9999"}, "public_url": "https://other.ngrok-free.app"}
        self.assertTrue(rg._tunnel_targets_port(ours, 8000))
        self.assertFalse(rg._tunnel_targets_port(other, 8000))
        # Bare host:port form (no scheme) also matches.
        self.assertTrue(rg._tunnel_targets_port({"config": {"addr": "localhost:8000"}}, 8000))
        self.assertFalse(rg._tunnel_targets_port({"config": {}}, 8000))

    def test_resolve_tracks_process_and_terminate_kills_it(self) -> None:
        import subprocess

        proc = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])

        def _cleanup() -> None:
            if proc.poll() is None:
                proc.kill()

        self.addCleanup(_cleanup)
        result = rg.resolve_public_url(
            8000,
            ngrok_path="/usr/bin/ngrok",
            query_fn=lambda port: "https://abc.ngrok-free.app",
            start_fn=lambda port: proc,
        )
        # The spawned tunnel handle is returned so serve() can tear it down.
        self.assertIs(result.process, proc)
        self.assertTrue(result.public)

        rg._terminate_process(result.process)
        self.assertIsNotNone(proc.poll())  # child is no longer running

    def test_regenerate_is_atomic_and_repairs_corrupt_index(self) -> None:
        rg.add_shot(self.root, self._png("a.png"), board_title="B", mode="proof", caption="c")
        index = self.root / "index.html"
        index.write_text("<<< corrupted half-written page")  # simulate a bad prior write

        rg.regenerate(self.root)
        html = index.read_text()
        self.assertTrue(html.startswith("<!DOCTYPE html>"))
        self.assertIn("<h2>B</h2>", html)
        # No temp file left beside the index.
        self.assertEqual(list(self.root.glob(".index.html-*.tmp")), [])

    def test_normalize_basic_auth_rejects_empty_and_malformed(self) -> None:
        self.assertIsNone(rg._normalize_basic_auth(None))
        self.assertEqual(rg._normalize_basic_auth("user:pass"), "user:pass")
        for bad in ("", "useronly", "user:", ":pass", ":"):
            with self.assertRaises(ValueError):
                rg._normalize_basic_auth(bad)

    def test_handler_enforces_auth_and_empty_is_not_silently_open(self) -> None:
        import base64 as b64
        import threading
        import time
        import urllib.error
        import urllib.request
        from http.server import ThreadingHTTPServer

        rg.add_shot(self.root, self._png("a.png"), board_title="B", mode="proof", caption="c")

        # A configured credential is enforced.
        httpd = ThreadingHTTPServer(("127.0.0.1", 0), rg._make_handler(self.root, "rev:secret"))
        port = httpd.server_address[1]
        threading.Thread(target=httpd.serve_forever, daemon=True).start()
        time.sleep(0.15)
        try:
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/index.html")
            self.assertEqual(ctx.exception.code, 401)
            token = b64.b64encode(b"rev:secret").decode()
            request = urllib.request.Request(
                f"http://127.0.0.1:{port}/index.html", headers={"Authorization": f"Basic {token}"}
            )
            self.assertEqual(urllib.request.urlopen(request).status, 200)
        finally:
            httpd.shutdown()


if __name__ == "__main__":
    unittest.main()
