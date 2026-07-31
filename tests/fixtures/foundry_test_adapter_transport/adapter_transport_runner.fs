extends ScriptRunner

# Synthetic Foundry Test Adapter Protocol runner used by the engine transport
# regressions. It implements only enough of the normative command grammar to prove
# what the engine guarantees: runner arguments arrive unchanged and ordered after
# both separators, artifacts are owned by the runner and isolated from process
# output, writes are observable while the process is alive, and the runner's return
# value reaches the caller as the process exit code.
#
# Reserved options are parsed only before the optional second `--`, and every
# reserved option consumes its next token unconditionally. Everything after the
# separator is opaque framework input, recorded verbatim in additive artifact
# metadata so a test can assert exact pass-through.
#
# Fixture-only behavior is requested through framework arguments so the reserved
# grammar stays normative:
#   noise                     write deliberate stdout and stderr output
#   return-code=<n>           return <n> after writing a complete artifact
#   delayed-report=<path>     flush point 1, wait for <path>, then flush point 2
#   uncaught-error            flush point 1 and then fail with a runtime error

const CONTINUATION_TIMEOUT_MSEC := 60000
const CONTINUATION_POLL_MSEC := 20

func run(args: PackedStringArray) -> int:
	if args.size() < 2 or args[0] != "adapter":
		return 2
	var operation: String = args[1]
	if operation != "capabilities" and operation != "discover" and operation != "run":
		return 2

	var output: String = ""
	var has_output: bool = false
	var report: String = ""
	var has_report: bool = false
	var protocol_version: String = ""
	var has_protocol_version: bool = false
	var selections := PackedStringArray()
	var framework := PackedStringArray()

	var index: int = 2
	var separated: bool = false
	while index < args.size():
		var argument: String = args[index]
		if argument == "--":
			separated = true
			index += 1
			break
		if argument != "--output" and argument != "--report" and argument != "--protocol-version" and argument != "--select":
			return 2
		if index + 1 >= args.size():
			return 2
		# A reserved option consumes its next token even when that token is `--` or
		# looks like another option, so opaque identifiers stay transportable.
		var value: String = args[index + 1]
		index += 2
		if argument == "--output":
			if has_output:
				return 2
			output = value
			has_output = true
		elif argument == "--report":
			if has_report:
				return 2
			report = value
			has_report = true
		elif argument == "--protocol-version":
			if has_protocol_version:
				return 2
			protocol_version = value
			has_protocol_version = true
		else:
			selections.append(value)

	if separated:
		while index < args.size():
			framework.append(args[index])
			index += 1

	if _framework_flag(framework, "noise"):
		print("adapter-transport-stdout")
		printerr("adapter-transport-stderr")

	if operation == "capabilities":
		if not has_output or has_report or has_protocol_version or selections.size() > 0:
			return 2
		return _write_capabilities(output, framework)

	if not has_protocol_version or protocol_version != "1":
		return 2

	if operation == "discover":
		if not has_output or has_report:
			return 2
		return _write_discovery(output, framework)

	if not has_report or has_output:
		return 2
	return _write_report(report, selections, framework)

func _framework_flag(framework: PackedStringArray, name: String) -> bool:
	for argument in framework:
		if argument == name:
			return true
	return false

func _framework_value(framework: PackedStringArray, name: String) -> String:
	var prefix: String = name + "="
	for argument in framework:
		if argument.begins_with(prefix):
			return argument.substr(prefix.length())
	return ""

func _return_code(framework: PackedStringArray, fallback: int) -> int:
	var value: String = _framework_value(framework, "return-code")
	if value.is_empty():
		return fallback
	return value.to_int()

func _quoted_list(values: PackedStringArray) -> String:
	var document: String = "["
	for i in values.size():
		if i > 0:
			document += ","
		document += "\"" + values[i] + "\""
	return document + "]"

func _write_capabilities(output: String, framework: PackedStringArray) -> int:
	var file := FileAccess.open(output, FileAccess.WRITE)
	if file == null:
		return 2
	var document: String = "{\"protocol\":\"foundry-test-adapter\",\"supported_versions\":[1],"
	document += "\"framework\":{\"id\":\"foundry.transport_fixture\","
	document += "\"name\":\"Transport Fixture\",\"version\":\"1.0.0\"},\"extensions\":[],"
	document += "\"framework_args\":" + _quoted_list(framework) + "}\n"
	file.store_string(document)
	file.flush()
	file.close()
	return _return_code(framework, 0)

func _write_discovery(output: String, framework: PackedStringArray) -> int:
	var file := FileAccess.open(output, FileAccess.WRITE)
	if file == null:
		return 2
	var envelope: String = "{\"protocol\":\"foundry-test-adapter\",\"version\":1,"
	file.store_string(envelope + "\"event\":\"discovery_start\",\"root\":\"res://tests\","
			+ "\"framework_args\":" + _quoted_list(framework) + "}\n")
	file.store_string(envelope + "\"event\":\"suite\",\"id\":\"transport::suite\","
			+ "\"label\":\"TransportSuite\",\"parent_id\":null,\"path\":\"res://tests/transport.fs\","
			+ "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":9,\"character\":0}},"
			+ "\"runnable\":true,\"skipped\":false,\"skip_reason\":null}\n")
	file.store_string(envelope + "\"event\":\"test\",\"id\":\"transport::first\","
			+ "\"label\":\"first case\",\"parent_id\":\"transport::suite\","
			+ "\"path\":\"res://tests/transport.fs\","
			+ "\"range\":{\"start\":{\"line\":1,\"character\":0},\"end\":{\"line\":3,\"character\":1}},"
			+ "\"runnable\":true,\"skipped\":false,\"skip_reason\":null,\"case_key\":null}\n")
	file.store_string(envelope + "\"event\":\"test\",\"id\":\"transport::second\","
			+ "\"label\":\"second case\",\"parent_id\":\"transport::suite\","
			+ "\"path\":\"res://tests/transport.fs\","
			+ "\"range\":{\"start\":{\"line\":5,\"character\":0},\"end\":{\"line\":7,\"character\":1}},"
			+ "\"runnable\":true,\"skipped\":false,\"skip_reason\":null,\"case_key\":null}\n")
	file.store_string(envelope + "\"event\":\"discovery_end\",\"suite_count\":1,"
			+ "\"test_count\":2,\"error_count\":0}\n")
	file.flush()
	file.close()
	return _return_code(framework, 0)

func _write_report(report: String, selections: PackedStringArray, framework: PackedStringArray) -> int:
	var leaves := PackedStringArray(["transport::first", "transport::second"])
	if selections.size() > 0:
		leaves = selections
	var file := FileAccess.open(report, FileAccess.WRITE)
	if file == null:
		return 2
	file.store_string("TAP version 13\n")
	file.store_string("# foundry-test-adapter: 1\n")
	file.store_string("1.." + str(leaves.size()) + "\n")
	file.flush()

	if _framework_flag(framework, "uncaught-error"):
		_store_point(file, 1, leaves[0])
		var divisor: int = 0
		var value: int = 1
		value /= divisor
		return 0

	var continuation: String = _framework_value(framework, "delayed-report")
	for i in leaves.size():
		if i == 1 and not continuation.is_empty():
			var waited: int = 0
			while not FileAccess.file_exists(continuation):
				if waited >= CONTINUATION_TIMEOUT_MSEC:
					file.close()
					return 2
				OS.delay_msec(CONTINUATION_POLL_MSEC)
				waited += CONTINUATION_POLL_MSEC
		_store_point(file, i + 1, leaves[i])
	file.close()
	return _return_code(framework, 0)

func _store_point(file: FileAccess, number: int, leaf_id: String) -> void:
	# The complete point and its diagnostic block are assembled before the write so a
	# reader never observes a half-written unit.
	var unit: String = "ok " + str(number) + " - transport.point_" + str(number) + "\n"
	unit += "  ---\n"
	unit += "  _foundry:\n"
	unit += "    id: \"" + leaf_id + "\"\n"
	unit += "    duration_ms: 0\n"
	unit += "    status_detail: \"\"\n"
	unit += "  ...\n"
	file.store_string(unit)
	file.flush()
