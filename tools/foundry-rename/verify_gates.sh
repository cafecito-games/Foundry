#!/usr/bin/env bash
#
# Foundry rename — re-runnable verification gates.
#
# Static gates (2 straggler grep, 3 license/attribution, 5 history) run by
# default and are fast. The heavy build+test gates (1 build, 4 functional smoke
# + full test suite) run only when --with-build is passed.
#
# Usage:
#   tools/foundry-rename/verify_gates.sh                # static gates only
#   tools/foundry-rename/verify_gates.sh --with-build   # all gates
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

BASELINE_TAG="${BASELINE_TAG:-godot-baseline-final}"
BIN="${FOUNDRY_BIN:-bin/godot.macos.editor.arm64}"
WITH_BUILD=0
[ "${1:-}" = "--with-build" ] && WITH_BUILD=1

# Use plain grep, never a user alias (e.g. ugrep), for predictable behavior.
GREP="$(command -v grep)"

PASS_COUNT=0
FAIL_COUNT=0

pass() { printf '  \033[32mPASS\033[0m  %s\n' "$1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }
note() { printf '        %s\n' "$1"; }
hdr()  { printf '\n=== %s ===\n' "$1"; }

# Paths excluded from the "must be clean" code sweeps. These are intentional
# residuals per the rename's documented scope:
#   thirdparty            never renamed (vendored upstream)
#   tools/foundry-rename  the rename map literally contains the old strings
#   *.po / *.pot          translations, pending regeneration
#   CHANGELOG / docs      historical records
#   LICENSE/COPYRIGHT/... inherited attribution kept verbatim by design
EXCLUDE_CODE=(
  ':!thirdparty'
  ':!tools/foundry-rename'
  ':!CHANGELOG.md'
  ':!docs/superpowers'
)

# ---------------------------------------------------------------------------
gate2_stragglers() {
  hdr "GATE 2 — straggler grep (gdscript / gdextension / GODOT_ families)"

  local n
  n=$(git grep -nE 'gdscript|GDScript|GDSCRIPT' -- "${EXCLUDE_CODE[@]}" \
        '*.h' '*.cpp' '*.mm' '*.py' '*.java' '*.kt' | wc -l | tr -d ' ')
  if [ "$n" -eq 0 ]; then pass "0 gdscript tokens in code"; else
    fail "$n gdscript tokens in code (expected 0)"
    git grep -nE 'gdscript|GDScript|GDSCRIPT' -- "${EXCLUDE_CODE[@]}" \
      '*.h' '*.cpp' '*.mm' '*.py' '*.java' '*.kt' | head; fi

  n=$(git grep -nE 'gdextension|GDExtension|GDEXTENSION' -- "${EXCLUDE_CODE[@]}" \
        '*.h' '*.cpp' '*.mm' | wc -l | tr -d ' ')
  if [ "$n" -eq 0 ]; then pass "0 gdextension/GDExtension C-API tokens in C/C++"; else
    fail "$n gdextension tokens in C/C++ (expected 0; C-API is now FoundryExtension*)"
    git grep -nE 'gdextension|GDExtension|GDEXTENSION' -- "${EXCLUDE_CODE[@]}" \
      '*.h' '*.cpp' '*.mm' | head; fi

  # GODOT_* / project.godot residuals — only the documented intentional set is
  # allowed. We assert membership in that allow-list rather than a raw count.
  local allow='libfoundry\.h|emscripten_helpers\.py|eslint\.config\.cjs|editor\.html|service-worker\.js|export_plugin\.cpp|bindings_generator\.cpp|register_types\.cpp|fsr2\.cpp|motion_vector_inc\.glsl|CMakeLists\.txt|org\.godotengine\.Godot\.xml|editor_import_blend_runner\.cpp|project_settings\.cpp|SConstruct|\.po$|\.pot$'
  local unexpected
  unexpected=$(git grep -lE 'GODOT_[A-Z]|project\.godot' -- "${EXCLUDE_CODE[@]}" \
                 | "$GREP" -vE "$allow" || true)
  if [ -z "$unexpected" ]; then
    pass "GODOT_* / project.godot residuals all within documented intentional set"
  else
    fail "unexpected GODOT_* / project.godot residuals:"; printf '%s\n' "$unexpected"; fi

  # No corrupted Foundry banner forged from the inherited Godot header.
  n=$(git grep -lE 'FOUNDRY ENGINE|Foundry Engine contributors|foundryengine\.org' \
        -- ':!tools/foundry-rename' | wc -l | tr -d ' ')
  if [ "$n" -eq 0 ]; then pass "0 corrupted FOUNDRY ENGINE / foundryengine.org banners"; else
    fail "$n files with a corrupted Foundry banner"; fi
}

# ---------------------------------------------------------------------------
gate3_license() {
  hdr "GATE 3 — license / attribution"

  if ! git rev-parse -q --verify "$BASELINE_TAG" >/dev/null; then
    note "baseline tag '$BASELINE_TAG' not found; skipping diff checks"
  else
    local d
    d=$(git diff --stat "$BASELINE_TAG"..HEAD -- thirdparty | wc -l | tr -d ' ')
    [ "$d" -eq 0 ] && pass "thirdparty/ untouched since baseline" \
                   || fail "thirdparty/ changed since baseline ($d lines)"

    d=$(git diff --stat "$BASELINE_TAG"..HEAD -- LICENSE.txt COPYRIGHT.txt AUTHORS.md | wc -l | tr -d ' ')
    [ "$d" -eq 0 ] && pass "LICENSE.txt / COPYRIGHT.txt / AUTHORS.md unchanged" \
                   || fail "inherited attribution files changed ($d lines)"
  fi

  [ -f NOTICE ] && "$GREP" -q 'fork of Godot Engine' NOTICE \
    && pass "NOTICE present with fork attribution" || fail "NOTICE missing/incomplete"

  # Sample inherited headers and confirm the banner + dual copyright + MIT text.
  local ok=0 total=0 f
  while IFS= read -r f; do
    total=$((total + 1))
    if "$GREP" -q 'GODOT ENGINE' "$f" \
       && "$GREP" -q 'Godot Engine contributors' "$f" \
       && "$GREP" -q 'Juan Linietsky' "$f" \
       && "$GREP" -q 'without limitation the rights' "$f"; then
      ok=$((ok + 1))
    else note "header incomplete: $f"; fi
  done < <(git ls-files 'core/**/*.cpp' 'core/**/*.h' 'scene/**/*.cpp' \
             'servers/**/*.cpp' 'editor/**/*.cpp' 'drivers/**/*.cpp' \
             | "$GREP" -v 'modules/foundry_script' | "$GREP" -v '\.gen\.' \
             | awk 'NR % 211 == 0' | head -20)
  [ "$total" -gt 0 ] && [ "$ok" -eq "$total" ] \
    && pass "$ok/$total sampled inherited headers intact" \
    || fail "$ok/$total sampled inherited headers intact"
}

# ---------------------------------------------------------------------------
gate5_history() {
  hdr "GATE 5 — git history preserved through rename"
  local f="modules/foundry_script/foundry_script.cpp"
  if [ ! -f "$f" ]; then fail "$f not found"; return; fi

  local count
  count=$(git log --follow --oneline "$f" | wc -l | tr -d ' ')
  [ "$count" -gt 50 ] \
    && pass "git log --follow spans $count commits (pre-rename history intact)" \
    || fail "git log --follow only $count commits (history may be squashed)"

  local authors
  authors=$(git blame -L 1,40 "$f" 2>/dev/null | "$GREP" -oE '\([^)]+[0-9]{4}' | wc -l | tr -d ' ')
  local distinct
  distinct=$(git blame -L 1,40 "$f" 2>/dev/null \
               | sed -E 's/^[0-9a-f]+ +\(([^0-9]+) [0-9].*/\1/' | sort -u | wc -l | tr -d ' ')
  [ "$distinct" -ge 2 ] \
    && pass "git blame shows $distinct distinct authors (original authorship retained)" \
    || fail "git blame shows $distinct author(s) (looks like a single-author squash)"
}

# ---------------------------------------------------------------------------
gate1_build() {
  hdr "GATE 1 — build (editor + export template)"
  local jobs; jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

  # scons prints "done building targets" after work, or "is up to date" on a
  # no-op incremental build; either means the target is green.
  local green='done building targets|is up to date'

  note "building editor (target=editor tests=yes) ..."
  if uv run --no-project --with scons scons platform=macos target=editor tests=yes \
       -j"$jobs" 2>&1 | tail -2 | "$GREP" -qE "$green"; then
    pass "editor build green"
  else
    fail "editor build failed"; fi

  note "building export template (target=template_debug) ..."
  if uv run --no-project --with scons scons platform=macos target=template_debug \
       -j"$jobs" 2>&1 | tail -2 | "$GREP" -qE "$green"; then
    pass "export template build green"
  else
    fail "export template build failed"; fi
}

# ---------------------------------------------------------------------------
gate4_functional() {
  hdr "GATE 4 — functional smoke + full test suite"
  if [ ! -x "$BIN" ]; then fail "engine binary not found at $BIN"; return; fi

  note "running full C++ test suite ..."
  local out; out=$("$BIN" --headless --test 2>&1)
  if printf '%s' "$out" | "$GREP" -qE 'Status:.*SUCCESS' \
     && ! printf '%s' "$out" | "$GREP" -qE 'failures: *[1-9]'; then
    pass "full test suite SUCCESS ($(printf '%s' "$out" | "$GREP" -oE 'test cases: *[0-9]+' | head -1))"
  else
    fail "test suite did not report clean SUCCESS"
    printf '%s\n' "$out" | tail -15; fi

  local proj; proj="$(mktemp -d)"
  printf 'config_version=5\n\n[application]\n\nconfig/name="Foundry Gate"\n' > "$proj/project.foundry"
  cat > "$proj/hello.fs" <<'FS'
extends SceneTree
func _initialize():
	print("FOUNDRY_SCRIPT_RAN")
	quit()
FS

  note "opening project.foundry in editor + checking for .foundry/ data dir ..."
  timeout 120 "$BIN" --headless --editor --path "$proj" --quit >/dev/null 2>&1
  if [ -d "$proj/.foundry" ]; then pass "engine created .foundry/ data dir (not .godot/)";
  else fail ".foundry/ data dir not created"; fi
  [ -d "$proj/.godot" ] && fail "stale .godot/ data dir created" \
                        || pass "no legacy .godot/ data dir"

  note "running a .fs script via --script ..."
  if timeout 90 "$BIN" --headless --path "$proj" --script hello.fs 2>&1 | "$GREP" -q 'FOUNDRY_SCRIPT_RAN'; then
    pass ".fs script loaded and executed (FoundryScript)"
  else
    fail ".fs script did not run"; fi

  if git grep -q '"foundryextension"' -- core/extension/foundry_extension.cpp; then
    pass "extension loader recognizes .foundryextension"
  else
    fail ".foundryextension not a recognized extension format"; fi
  if [ -f core/extension/foundry_extension_interface.gen.h ]; then
    pass "generated foundry_extension_interface.gen.h present"
  else
    note "foundry_extension_interface.gen.h absent (build first)"; fi

  rm -rf "$proj"
}

# ---------------------------------------------------------------------------
gate2_stragglers
gate3_license
gate5_history
if [ "$WITH_BUILD" -eq 1 ]; then
  gate1_build
  gate4_functional
else
  hdr "GATE 1 + GATE 4 skipped (pass --with-build to run build + tests)"
fi

printf '\n=== SUMMARY: %d passed, %d failed ===\n' "$PASS_COUNT" "$FAIL_COUNT"
[ "$FAIL_COUNT" -eq 0 ]
