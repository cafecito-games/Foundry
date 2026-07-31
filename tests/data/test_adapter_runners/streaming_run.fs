extends ScriptRunner

# Streams a two-point TAP13 report and blocks between the points until the caller
# creates the gate file, so the engine transport test can observe an incrementally
# flushed report while the runner process is still alive.

const GATE_TIMEOUT_MSEC := 60000
const GATE_POLL_MSEC := 20

func run(args: PackedStringArray) -> int:
	if args.size() != 8:
		return 2
	if args[0] != "adapter" or args[1] != "run":
		return 2
	if args[2] != "--protocol-version" or args[3] != "1":
		return 2
	if args[4] != "--report" or args[6] != "--gate":
		return 2

	var file := FileAccess.open(args[5], FileAccess.WRITE)
	if file == null:
		return 2
	_write_header(file, 2)
	_write_point(file, 1, "transport::first")

	var waited: int = 0
	while not FileAccess.file_exists(args[7]):
		if waited >= GATE_TIMEOUT_MSEC:
			file.close()
			return 2
		OS.delay_msec(GATE_POLL_MSEC)
		waited += GATE_POLL_MSEC

	_write_point(file, 2, "transport::second")
	file.close()
	return 0

func _write_header(file: FileAccess, planned: int) -> void:
	file.store_string("TAP version 13\n")
	file.store_string("# foundry-test-adapter: 1\n")
	file.store_string("1.." + str(planned) + "\n")
	file.flush()

func _write_point(file: FileAccess, number: int, id: String) -> void:
	file.store_string("ok " + str(number) + " - transport.point_" + str(number) + "\n")
	file.store_string("  ---\n")
	file.store_string("  _foundry:\n")
	file.store_string("    id: \"" + id + "\"\n")
	file.store_string("    duration_ms: 0\n")
	file.store_string("    status_detail: \"\"\n")
	file.store_string("  ...\n")
	file.flush()
