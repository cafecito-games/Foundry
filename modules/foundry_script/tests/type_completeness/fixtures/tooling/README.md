# Captured tooling host records

These two files are what a real tooling host produced, captured by driving
`foundry tooling serve --project <dir> --lsp-port 0 --dap-port 0` against a project whose only
script declares one member of a declared type, and asking the language server for a hover over that
member.

- `host_readiness_line.txt` is the single `FOUNDRY_TOOLING {...}` line the host prints once both
  listeners are bound. It is what a tooling family that needs a host process reads the loopback port
  off; `parse_tooling_host_readiness` is held to it, and everything it rejects becomes the
  `tooling_host_unavailable` structural stage rather than a family that observed nothing.
- `hover_response.json` is the JSON-RPC response the host returned for `textDocument/hover`. Its
  `result.contents.value` is the body a user reads, and `rendered_type_in_hover_contents` - the same
  extractor the in-process observation uses - is held to it.

Only the two values that name the machine the capture ran on were rewritten, to a fixed
`/tmp/foundry-tooling-capture` project path and a fixed process id. Nothing that carries a type, a
port, or a protocol shape was touched, so the records still stand for what the host emits.
