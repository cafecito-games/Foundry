#!/usr/bin/env python3
"""Foundry review gallery: a stdlib-only screenshot storyboard for remote review.

Collaborating on Foundry remotely, much of the work produces *visual* output
(editor GUI, docks, dialogs, inspector). This helper lets an agent capture
editor screenshots, group them into captioned boards, and serve them over a
single URL a reviewer can open from anywhere.

The gallery ingests PNGs plus captions and knows nothing about how a PNG was
produced, which keeps it decoupled from the capture layer: it improves for free
once on-demand capture lands, and it works today with any PNG.

Layout (a stable dir outside the repo so the URL and content survive sessions):

    ~/.foundry-gallery/
        manifest.json   # boards + shots (caption, order, type: image|video)
        index.html      # regenerated on every add; self-contained, theme-aware
        shots/          # the PNGs (later: mp4s)

Three review modes, expressed as board types:

    proof        a compact 1-few shot block confirming a change works.
    design       before/after pairs for aesthetic/layout review.
    walkthrough  an ordered storyboard (open -> act -> observe), step-captioned.

Commands:

    review_gallery.py add <png> --board "<title>" --mode proof|design|walkthrough
        --caption "..." [--pair before|after] [--order N] [--type image|video]
    review_gallery.py board "<title>" --mode <mode>
    review_gallery.py serve [--port N] [--basic-auth user:pass]

The gallery directory defaults to ~/.foundry-gallery and can be overridden with
--root or the FOUNDRY_GALLERY_DIR environment variable (used by the tests).

Stdlib only: no pip dependencies.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import json
import os
import shutil
import socket
import subprocess
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable

MANIFEST_VERSION = 1
MODES = ("proof", "design", "walkthrough")
SHOT_TYPES = ("image", "video")
DEFAULT_ROOT = Path.home() / ".foundry-gallery"
NGROK_API = "http://127.0.0.1:4040/api/tunnels"


# ---------------------------------------------------------------------------
# Paths and manifest I/O
# ---------------------------------------------------------------------------


def gallery_root(explicit: str | os.PathLike[str] | None = None) -> Path:
    """Resolve the gallery directory (flag > env var > default)."""
    if explicit:
        return Path(explicit).expanduser()
    env = os.environ.get("FOUNDRY_GALLERY_DIR")
    if env:
        return Path(env).expanduser()
    return DEFAULT_ROOT


def _empty_manifest() -> dict:
    return {"version": MANIFEST_VERSION, "boards": []}


def load_manifest(root: Path) -> dict:
    """Load manifest.json, returning an empty manifest when absent."""
    path = Path(root) / "manifest.json"
    if not path.is_file():
        return _empty_manifest()
    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest.setdefault("version", MANIFEST_VERSION)
    manifest.setdefault("boards", [])
    return manifest


def save_manifest(root: Path, manifest: dict) -> None:
    """Persist the manifest atomically so a crash mid-write can't corrupt it."""
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    path = root / "manifest.json"
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(manifest, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    tmp.replace(path)


# ---------------------------------------------------------------------------
# Board / shot mutation
# ---------------------------------------------------------------------------


def find_board(manifest: dict, title: str) -> dict | None:
    for board in manifest["boards"]:
        if board["title"] == title:
            return board
    return None


def ensure_board(manifest: dict, title: str, mode: str) -> dict:
    """Return the board with `title`, creating it (append = newest) if needed."""
    if mode not in MODES:
        raise ValueError(f"unknown mode {mode!r}; expected one of {', '.join(MODES)}")
    board = find_board(manifest, title)
    if board is None:
        board = {"title": title, "mode": mode, "shots": []}
        manifest["boards"].append(board)
    return board


def _shot_filename(source: Path, data: bytes) -> str:
    """Content-addressed name so re-adding the same file is idempotent."""
    digest = hashlib.sha1(data).hexdigest()[:16]
    suffix = source.suffix.lower() or ".png"
    return f"{digest}{suffix}"


def add_shot(
    root: Path,
    source: str | os.PathLike[str],
    *,
    board_title: str,
    mode: str = "proof",
    caption: str = "",
    pair: str | None = None,
    order: int | None = None,
    shot_type: str = "image",
) -> dict:
    """Copy `source` into shots/, append a manifest entry, regenerate index.html.

    Returns the new shot entry. When the board already exists its stored mode is
    kept, so `mode` only matters for the first shot of a board.
    """
    if shot_type not in SHOT_TYPES:
        raise ValueError(f"unknown type {shot_type!r}; expected one of {', '.join(SHOT_TYPES)}")
    if pair is not None and pair not in ("before", "after"):
        raise ValueError("--pair must be 'before' or 'after'")

    root = Path(root)
    source = Path(source)
    if not source.is_file():
        raise FileNotFoundError(f"no such file: {source}")

    shots_dir = root / "shots"
    shots_dir.mkdir(parents=True, exist_ok=True)
    data = source.read_bytes()
    filename = _shot_filename(source, data)
    dest = shots_dir / filename
    if not dest.exists():
        shutil.copyfile(source, dest)

    manifest = load_manifest(root)
    board = ensure_board(manifest, board_title, mode)
    if order is None:
        order = len(board["shots"])

    entry: dict = {
        "file": f"shots/{filename}",
        "caption": caption,
        "type": shot_type,
        "order": order,
    }
    if pair is not None:
        entry["pair"] = pair
    board["shots"].append(entry)

    save_manifest(root, manifest)
    regenerate(root, manifest)
    return entry


def regenerate(root: Path, manifest: dict | None = None) -> Path:
    """Write index.html deterministically from the manifest. Returns its path."""
    root = Path(root)
    if manifest is None:
        manifest = load_manifest(root)
    path = root / "index.html"
    root.mkdir(parents=True, exist_ok=True)
    path.write_text(render_html(manifest), encoding="utf-8")
    return path


# ---------------------------------------------------------------------------
# HTML rendering (pure function of the manifest -> deterministic output)
# ---------------------------------------------------------------------------

_PAGE_CSS = """\
:root {
  --bg: #ffffff;
  --panel: #f6f7f9;
  --border: #e3e6ea;
  --text: #1b1f24;
  --muted: #6b7480;
  --accent: #3b82f6;
  --shadow: 0 1px 3px rgba(16, 24, 40, 0.08), 0 1px 2px rgba(16, 24, 40, 0.06);
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #0e1116;
    --panel: #161b22;
    --border: #262c36;
    --text: #e6edf3;
    --muted: #8b949e;
    --accent: #58a6ff;
    --shadow: 0 1px 3px rgba(0, 0, 0, 0.4);
  }
}
:root[data-theme="light"] {
  --bg: #ffffff; --panel: #f6f7f9; --border: #e3e6ea;
  --text: #1b1f24; --muted: #6b7480; --accent: #3b82f6;
  --shadow: 0 1px 3px rgba(16, 24, 40, 0.08), 0 1px 2px rgba(16, 24, 40, 0.06);
}
:root[data-theme="dark"] {
  --bg: #0e1116; --panel: #161b22; --border: #262c36;
  --text: #e6edf3; --muted: #8b949e; --accent: #58a6ff;
  --shadow: 0 1px 3px rgba(0, 0, 0, 0.4);
}
* { box-sizing: border-box; }
body {
  margin: 0;
  background: var(--bg);
  color: var(--text);
  font: 15px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
}
header {
  position: sticky; top: 0; z-index: 5;
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 24px;
  background: var(--panel);
  border-bottom: 1px solid var(--border);
}
header h1 { font-size: 16px; margin: 0; font-weight: 600; letter-spacing: 0.2px; }
main { max-width: 1100px; margin: 0 auto; padding: 24px; }
.board {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 20px;
  margin-bottom: 24px;
  box-shadow: var(--shadow);
}
.board-head { display: flex; align-items: baseline; gap: 12px; margin-bottom: 16px; }
.board-head h2 { font-size: 18px; margin: 0; font-weight: 600; }
.mode-tag {
  font-size: 11px; text-transform: uppercase; letter-spacing: 0.6px;
  color: var(--muted); border: 1px solid var(--border);
  border-radius: 999px; padding: 2px 10px;
}
.shots { display: grid; gap: 16px; }
.shots.proof { grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); }
.shots.walkthrough { grid-template-columns: 1fr; }
.pairs { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
@media (max-width: 640px) { .pairs { grid-template-columns: 1fr; } }
.shot { border: 1px solid var(--border); border-radius: 10px; overflow: hidden; background: var(--bg); }
.shot figure { margin: 0; }
.shot img, .shot video { display: block; width: 100%; height: auto; background: #000; }
.shot figcaption { padding: 10px 12px; font-size: 13px; color: var(--muted); border-top: 1px solid var(--border); }
.step-index { color: var(--accent); font-weight: 600; margin-right: 6px; }
.pair-label { font-size: 11px; text-transform: uppercase; letter-spacing: 0.6px; color: var(--muted); padding: 8px 12px 0; }
.empty { color: var(--muted); text-align: center; padding: 48px 0; }
button.theme {
  font: inherit; font-size: 13px; cursor: pointer;
  background: var(--bg); color: var(--text);
  border: 1px solid var(--border); border-radius: 8px; padding: 6px 12px;
}
"""

_THEME_SCRIPT = """\
(function () {
  var KEY = "foundry-gallery-theme";
  var root = document.documentElement;
  var saved = null;
  try { saved = localStorage.getItem(KEY); } catch (e) {}
  if (saved === "light" || saved === "dark") { root.setAttribute("data-theme", saved); }
  var btn = document.getElementById("theme-toggle");
  function label() {
    var t = root.getAttribute("data-theme");
    btn.textContent = t === "dark" ? "Light theme" : t === "light" ? "Dark theme" : "Toggle theme";
  }
  btn.addEventListener("click", function () {
    var current = root.getAttribute("data-theme");
    if (!current) {
      current = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
    }
    var next = current === "dark" ? "light" : "dark";
    root.setAttribute("data-theme", next);
    try { localStorage.setItem(KEY, next); } catch (e) {}
    label();
  });
  label();
})();
"""


def _esc(value: str) -> str:
    return html.escape(str(value), quote=True)


def _render_media(shot: dict) -> str:
    """Render a single shot's media element, branching on its `type`.

    The `video` branch is intentionally present so day-two video capture only
    touches the capture step and this branch, never the surrounding template.
    """
    src = _esc(shot.get("file", ""))
    caption = _esc(shot.get("caption", ""))
    shot_type = shot.get("type", "image")
    if shot_type == "video":
        media = f'<video controls preload="metadata" src="{src}"></video>'
    else:
        media = f'<img loading="lazy" src="{src}" alt="{caption}">'
    return media


def _render_shot(shot: dict, *, step: int | None = None) -> str:
    caption = _esc(shot.get("caption", ""))
    step_html = f'<span class="step-index">{step}.</span>' if step is not None else ""
    caption_block = f"<figcaption>{step_html}{caption}</figcaption>" if (caption or step_html) else ""
    return f'<div class="shot"><figure>{_render_media(shot)}{caption_block}</figure></div>'


def _render_board(board: dict) -> str:
    title = _esc(board.get("title", ""))
    mode = board.get("mode", "proof")
    shots = sorted(board.get("shots", []), key=lambda s: s.get("order", 0))

    parts = [
        '<section class="board">',
        f'<div class="board-head"><h2>{title}</h2><span class="mode-tag">{_esc(mode)}</span></div>',
    ]

    if mode == "design":
        before = [s for s in shots if s.get("pair") == "before"]
        after = [s for s in shots if s.get("pair") == "after"]
        unpaired = [s for s in shots if s.get("pair") not in ("before", "after")]
        pair_count = max(len(before), len(after))
        parts.append('<div class="shots design">')
        for i in range(pair_count):
            parts.append('<div class="pairs">')
            for label, column in (("Before", before), ("After", after)):
                if i < len(column):
                    parts.append(f'<div><div class="pair-label">{label}</div>{_render_shot(column[i])}</div>')
                else:
                    parts.append("<div></div>")
            parts.append("</div>")
        for shot in unpaired:
            parts.append(_render_shot(shot))
        parts.append("</div>")
    elif mode == "walkthrough":
        parts.append('<div class="shots walkthrough">')
        for index, shot in enumerate(shots, start=1):
            parts.append(_render_shot(shot, step=index))
        parts.append("</div>")
    else:  # proof
        parts.append('<div class="shots proof">')
        for shot in shots:
            parts.append(_render_shot(shot))
        parts.append("</div>")

    parts.append("</section>")
    return "".join(parts)


def render_html(manifest: dict) -> str:
    """Render the full self-contained gallery page from a manifest.

    Pure function of the manifest: no timestamps or randomness, so identical
    manifests produce byte-identical HTML.
    """
    boards = list(manifest.get("boards", []))
    # Newest board first (boards are appended in creation order).
    body_boards = "".join(_render_board(b) for b in reversed(boards))
    if not body_boards:
        body_boards = '<p class="empty">No boards yet. Add a screenshot to get started.</p>'

    return (
        "<!DOCTYPE html>\n"
        '<html lang="en">\n'
        "<head>\n"
        '<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        "<title>Foundry Review Gallery</title>\n"
        f"<style>\n{_PAGE_CSS}</style>\n"
        "</head>\n"
        "<body>\n"
        "<header><h1>Foundry Review Gallery</h1>"
        '<button id="theme-toggle" class="theme" type="button">Toggle theme</button></header>\n'
        f"<main>\n{body_boards}\n</main>\n"
        f"<script>\n{_THEME_SCRIPT}</script>\n"
        "</body>\n"
        "</html>\n"
    )


# ---------------------------------------------------------------------------
# Serving (local http.server + optional ngrok tunnel)
# ---------------------------------------------------------------------------


@dataclass
class ServeUrl:
    url: str
    public: bool
    message: str


def query_ngrok_url(port: int, *, api: str = NGROK_API, timeout: float = 2.0) -> str | None:
    """Return the public https tunnel for `port` from ngrok's local API, or None."""
    try:
        with urllib.request.urlopen(api, timeout=timeout) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, OSError, ValueError):
        return None
    tunnels = payload.get("tunnels", [])
    https = [t.get("public_url") for t in tunnels if str(t.get("public_url", "")).startswith("https://")]
    if https:
        return https[0]
    for tunnel in tunnels:
        url = tunnel.get("public_url")
        if url:
            return url
    return None


def _spawn_ngrok(binary: str, port: int) -> subprocess.Popen | None:
    try:
        return subprocess.Popen(
            [binary, "http", str(port), "--log", "stdout"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError:
        return None


def resolve_public_url(
    port: int,
    *,
    ngrok_path: str | None = None,
    query_fn: Callable[[int], str | None] | None = None,
    start_fn: Callable[[int], object] | None = None,
    poll_attempts: int = 15,
    poll_interval: float = 0.4,
    sleep_fn: Callable[[float], None] | None = None,
) -> ServeUrl:
    """Decide which URL to advertise for the gallery.

    With ngrok available and a tunnel reachable, returns the public URL. When
    ngrok is missing or the tunnel never comes up, falls back to the local URL
    with a clear message instead of failing hard.
    """
    local = f"http://127.0.0.1:{port}"
    if ngrok_path is None:
        return ServeUrl(
            url=local,
            public=False,
            message=(
                "ngrok not found on PATH; serving locally only. Install and authenticate ngrok to expose a public URL."
            ),
        )

    query = query_fn or query_ngrok_url
    if start_fn is not None:
        start_fn(port)
    else:
        _spawn_ngrok(ngrok_path, port)

    if sleep_fn is None:
        import time

        sleep_fn = time.sleep

    for attempt in range(poll_attempts):
        url = query(port)
        if url:
            return ServeUrl(url=url, public=True, message=f"Public tunnel: {url}")
        if attempt < poll_attempts - 1:
            sleep_fn(poll_interval)

    return ServeUrl(
        url=local,
        public=False,
        message=(
            "ngrok is installed but no tunnel came up (not authenticated, or the "
            "agent failed to start); serving locally only."
        ),
    )


def _make_handler(root: Path, basic_auth: str | None) -> type[SimpleHTTPRequestHandler]:
    expected = None
    if basic_auth:
        expected = "Basic " + base64.b64encode(basic_auth.encode("utf-8")).decode("ascii")

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(root), **kwargs)

        def _authorized(self) -> bool:
            if expected is None:
                return True
            if self.headers.get("Authorization") == expected:
                return True
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="Foundry Review Gallery"')
            self.send_header("Content-Length", "0")
            self.end_headers()
            return False

        def do_GET(self) -> None:  # noqa: N802 (http.server API)
            if self._authorized():
                super().do_GET()

        def do_HEAD(self) -> None:  # noqa: N802
            if self._authorized():
                super().do_HEAD()

        def log_message(self, *args) -> None:  # keep serve output clean
            pass

    return Handler


def serve(
    root: Path,
    *,
    port: int = 8000,
    basic_auth: str | None = None,
    use_ngrok: bool = True,
) -> None:
    """Serve the gallery locally and, when possible, over an ngrok tunnel."""
    root = Path(root)
    if not (root / "index.html").is_file():
        regenerate(root)

    handler = _make_handler(root, basic_auth)
    httpd = ThreadingHTTPServer(("127.0.0.1", port), handler)
    port = httpd.server_address[1]

    ngrok_path = shutil.which("ngrok") if use_ngrok else None
    resolved = resolve_public_url(port, ngrok_path=ngrok_path)

    print(f"Serving {root} at http://127.0.0.1:{port}")
    if basic_auth:
        print("HTTP basic auth is required for the public URL.")
    print(resolved.message)
    if resolved.public:
        print(f"Open this from anywhere: {resolved.url}")
    print("Press Ctrl+C to stop.")

    last_public = resolved.url if resolved.public else None
    try:
        if ngrok_path and use_ngrok:
            _serve_with_rotation_watch(httpd, port, last_public)
        else:
            httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.shutdown()


def _serve_with_rotation_watch(httpd: ThreadingHTTPServer, port: int, last_public: str | None) -> None:
    """Serve while watching for the free-tier ngrok URL rotating, re-printing it."""
    import threading

    stop = threading.Event()

    def watch() -> None:
        nonlocal last_public
        while not stop.wait(5.0):
            current = query_ngrok_url(port)
            if current and current != last_public:
                last_public = current
                print(f"ngrok URL changed -> {current}")

    watcher = threading.Thread(target=watch, daemon=True)
    watcher.start()
    try:
        httpd.serve_forever()
    finally:
        stop.set()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _find_free_port(preferred: int) -> int:
    """Return `preferred` if free, otherwise an OS-assigned free port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        try:
            probe.bind(("127.0.0.1", preferred))
            return preferred
        except OSError:
            probe.bind(("127.0.0.1", 0))
            return probe.getsockname()[1]


def _cmd_add(args: argparse.Namespace) -> int:
    root = gallery_root(args.root)
    entry = add_shot(
        root,
        args.png,
        board_title=args.board,
        mode=args.mode,
        caption=args.caption,
        pair=args.pair,
        order=args.order,
        shot_type=args.type,
    )
    print(f"Added {entry['file']} to board {args.board!r}.")
    print(f"Gallery: {root / 'index.html'}")
    return 0


def _cmd_board(args: argparse.Namespace) -> int:
    root = gallery_root(args.root)
    manifest = load_manifest(root)
    board = ensure_board(manifest, args.title, args.mode)
    save_manifest(root, manifest)
    regenerate(root, manifest)
    print(f"Board {board['title']!r} ({board['mode']}) ready with {len(board['shots'])} shot(s).")
    print(f"Gallery: {root / 'index.html'}")
    return 0


def _cmd_serve(args: argparse.Namespace) -> int:
    root = gallery_root(args.root)
    port = _find_free_port(args.port)
    serve(
        root,
        port=port,
        basic_auth=args.basic_auth,
        use_ngrok=not args.no_ngrok,
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="review_gallery.py",
        description="Capture editor screenshots into captioned boards and serve them for remote review.",
    )
    parser.add_argument(
        "--root",
        default=None,
        help="Gallery directory (default: $FOUNDRY_GALLERY_DIR or ~/.foundry-gallery).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    add = sub.add_parser("add", help="Add a PNG to a board.")
    add.add_argument("png", help="Path to the PNG (or, day-two, video) to ingest.")
    add.add_argument("--board", required=True, help="Board title to add the shot to.")
    add.add_argument("--mode", choices=MODES, default="proof", help="Board mode (used when creating the board).")
    add.add_argument("--caption", default="", help="Caption for this shot.")
    add.add_argument("--pair", choices=("before", "after"), default=None, help="For design boards: pairing side.")
    add.add_argument("--order", type=int, default=None, help="Explicit order within the board (default: append).")
    add.add_argument("--type", choices=SHOT_TYPES, default="image", help="Media type (default: image).")
    add.set_defaults(func=_cmd_add)

    board = sub.add_parser("board", help="Create/open a board.")
    board.add_argument("title", help="Board title.")
    board.add_argument("--mode", choices=MODES, default="proof", help="Board mode.")
    board.set_defaults(func=_cmd_board)

    serve_cmd = sub.add_parser("serve", help="Serve the gallery (local + ngrok tunnel).")
    serve_cmd.add_argument("--port", type=int, default=8000, help="Preferred local port (default: 8000).")
    serve_cmd.add_argument("--basic-auth", default=None, metavar="USER:PASS", help="Require HTTP basic auth.")
    serve_cmd.add_argument("--no-ngrok", action="store_true", help="Serve locally only, skip ngrok.")
    serve_cmd.set_defaults(func=_cmd_serve)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
