# Foundry Review Gallery

A stdlib-only helper (`scripts/review_gallery.py`) that turns editor screenshots
into captioned **boards** and serves them over a single URL — so a remote
reviewer can verify visual changes (docks, dialogs, inspector) without a local
build.

The gallery ingests PNGs plus captions and knows nothing about how a PNG was
produced. It works today with any PNG and improves for free once on-demand
editor capture lands (see the capture tool in epic #868).

## Layout

Everything lives in a stable dir outside the repo so the URL and content survive
across sessions (override with `--root` or `$FOUNDRY_GALLERY_DIR`):

```
~/.foundry-gallery/
  manifest.json   # boards + shots (caption, order, type: image|video)
  index.html      # regenerated on every add; self-contained, theme-aware
  shots/          # the PNGs (later: mp4s)
```

## Board modes

- **proof** — a compact 1–few shot block confirming a change works.
- **design** — before/after pairs for aesthetic/layout review (`--pair before|after`).
- **walkthrough** — an ordered, step-captioned storyboard (open → act → observe).

Boards render newest-first; within a board, shots render by `order`.

## Commands

```sh
# Add a screenshot to a board (creates the board on first add).
python3 scripts/review_gallery.py add shot.png \
    --board "Dock fix" --mode proof --caption "dock stays put after close"

# Before/after design pair.
python3 scripts/review_gallery.py add old.png --board "Inspector redesign" \
    --mode design --caption "old" --pair before
python3 scripts/review_gallery.py add new.png --board "Inspector redesign" \
    --mode design --caption "new" --pair after

# Create/open an empty board.
python3 scripts/review_gallery.py board "Walkthrough: create node" --mode walkthrough

# Serve locally and (if available) over an ngrok tunnel.
python3 scripts/review_gallery.py serve --port 8000
python3 scripts/review_gallery.py serve --basic-auth reviewer:secret
```

`add` copies the PNG into `shots/`, appends a manifest entry, and regenerates
`index.html` deterministically (same manifest → byte-identical HTML).

## Serving

`serve` starts a local HTTP server rooted at the gallery dir. If the `ngrok`
binary is on `PATH` and authenticated, it also brings up a tunnel and prints the
public URL, re-printing it if the free-tier URL rotates. Without ngrok it serves
locally and says so rather than failing. `--basic-auth user:pass` adds HTTP basic
auth since a public tunnel URL is otherwise public-by-obscurity.

## Day-two video

Each shot entry carries a `type` field (`image` today). The template already has
a `video` branch, so adding video capture later only touches the capture step and
that branch — no template changes. A hand-authored `type: "video"` manifest entry
renders through the stubbed branch today.

## Tests

```sh
python3 scripts/tests/test_review_gallery.py
# or: python3 -m unittest scripts.tests.test_review_gallery
```
