# .github/scripts/

CI helper modules live here and are tested by importing and calling them.
Imitate `test_pr_platform_check_request.py`, which tests
`pr_platform_check_request.py` by calling its functions and asserting on
results.

**Never** assert on workflow YAML as text (indentation, key order, phrase
matches). Parse it with `yaml.safe_load` and assert on the graph via
`workflow_graph.py` instead.

Logic that needs testing does not live inline in a workflow `run:` block;
extract it to a module here first.

See the root `### Test authoring rules` in `../../AGENTS.md` for the full policy
and self-check commands.
