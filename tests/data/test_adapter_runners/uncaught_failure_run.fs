extends ScriptRunner

# Flushes a partial report and then fails with an uncaught runtime error, so the
# engine transport test can prove that an incomplete artifact, not the process
# exit code alone, identifies infrastructure failure.

func run(args: PackedStringArray) -> int:
	if args.size() != 6:
		return 2
	if args[0] != "adapter" or args[1] != "run":
		return 2
	if args[2] != "--protocol-version" or args[3] != "1" or args[4] != "--report":
		return 2

	var file := FileAccess.open(args[5], FileAccess.WRITE)
	if file == null:
		return 2
	file.store_string("TAP version 13\n")
	file.store_string("# foundry-test-adapter: 1\n")
	file.store_string("1..2\n")
	file.store_string("ok 1 - transport.point_1\n")
	file.store_string("  ---\n")
	file.store_string("  _foundry:\n")
	file.store_string("    id: \"transport::first\"\n")
	file.store_string("    duration_ms: 0\n")
	file.store_string("    status_detail: \"\"\n")
	file.store_string("  ...\n")
	file.flush()

	var divisor: int = 0
	var value: int = 1
	value /= divisor
	return 0
