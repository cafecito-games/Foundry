# misc/checks/

A `check_*.py` here inspects files or build artifacts, reports every
violation it finds (not just the first), and exits non-zero if any exist.
Each check carries a unit test under `misc/checks/tests/`.

**Never** add a new file here for a single literal-token rule. Add an entry to
`misc/checks/file_policy.toml` instead — new token policy is data, not code.

See the root `### Test authoring rules` in `../../AGENTS.md` for the full policy
and self-check commands.
