# Quiet Precision Board Rail Polish

## Summary

This is a small visual follow-up to merged PR #2116. It keeps the Board Rail's
behavior and layout contract intact while bringing the switcher closer to the
approved **Quiet Precision / Soft Lift** mockup: a low-contrast floating island,
a softly raised active segment, a short accent marker, and a restrained glide
when the active board changes.

The work ships from a new branch and pull request targeting `develop`.

## Visual Treatment

- Replace the hard capsule outline with a quieter theme-derived surface, faint
  edge definition, rounded corners, and restrained depth.
- Render the active board as a neutral raised pill inside the rail. Its fill is
  distinct without competing with the board title.
- Center a short, two-pixel theme-accent marker along the active pill's bottom
  edge. It must not become a full-width tab underline.
- Keep inactive segments transparent. Hover uses a quiet neutral wash.
- Integrate the trailing menu button into the same surface, separated from the
  board segments by a faint divider and using the same restrained hover language.
- Derive every color, radius, margin, and size from editor theme values and
  `EDSCALE`; do not copy literal mockup colors into the control.
- Preserve legible hover, pressed, keyboard-focus, and disabled states in Modern
  and Classic styles and in both light and dark themes.

## Interaction and Motion

Board switching, compact mode, rename, context menus, and the trailing menu keep
their existing behavior. Only the selection decoration animates; the destination
board becomes active immediately.

On an ordinary active-board change, the raised pill and accent marker glide to
the destination segment over about 115 ms using cubic ease-out with no bounce.
When the operating system reports reduced motion, the decoration snaps directly
to the destination. Setup, theme changes, structural rebuilds, reorder, and
compact/full transitions also snap so stale geometry is never animated.

## Implementation Shape

Keep the existing buttons as the input and accessibility controls. Draw the
active pill, marker, and menu divider as Board Rail decoration behind those
buttons, using theme styleboxes and constants. The board buttons' pressed styles
become transparent so they do not duplicate the shared active surface.

The switcher retains the current active decoration rectangle across an
active-board rebuild, resolves the destination after container layout, and uses
one short tween to interpolate between them. Any newer change replaces the
in-flight tween. All other rebuild paths synchronize the decoration without
animation.

## Scope

This polish does not change board storage, switching semantics, compact-mode
rules, title sizing, menus, rename behavior, scene modes, or per-board
customization. It does not introduce a new preference or a general animation
framework.

## Verification

- Extend `tests/editor/test_editor_board_switcher.h` with observable checks for
  the active decoration targeting the selected board, snapping on structural or
  compact changes, and honoring reduced-motion behavior where it can be injected
  deterministically.
- Keep the existing Board Switcher suite green.
- Run the repository's strict native validation build before publishing.
- Exercise the real editor through Foundry automation at normal and compact
  widths, then publish a small before/after design review gallery covering dark
  and light themes.
