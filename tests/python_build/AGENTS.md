# tests/python_build/

Tests here **execute** the build system: invoke SCons, Gradle, `javap`, R8, or a
built binary and assert on the result. Imitate `test_android_jni_contract.py`
(compiles a Java fixture and runs real `javap`) and
`test_android_gradle_behavioral.py` (runs real Gradle).

**Never** add a test here that only calls `read_text()` / `assertIn` on a source
file. That is a file-content check, not a build-system test — put the rule in
`misc/checks/file_policy.toml` instead.

See the root `### Test authoring rules` in `../../AGENTS.md` for the full policy
and self-check commands.
