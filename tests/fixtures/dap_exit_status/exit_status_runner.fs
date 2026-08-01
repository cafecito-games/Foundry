extends ScriptRunner

# Foundry Test Adapter Protocol runner used by the debug adapter exit-status
# regressions. The launch surface a debug adapter client drives is fixed, so the
# result this runner returns is chosen through a selected identifier of the form
# `exit::<code>`. A report path that cannot be opened yields the protocol's
# infrastructure result `2` without any cooperation from the runner, which is what
# a real infrastructure failure looks like from the outside.

func run(args: PackedStringArray) -> int:
	if args.size() < 2 or args[0] != "adapter" or args[1] != "run":
		return 2

	var report: String = ""
	var protocol_version: String = ""
	var selections := PackedStringArray()

	var index: int = 2
	while index < args.size():
		var argument: String = args[index]
		if index + 1 >= args.size():
			return 2
		var value: String = args[index + 1]
		index += 2
		if argument == "--report":
			report = value
		elif argument == "--protocol-version":
			protocol_version = value
		elif argument == "--select":
			selections.append(value)
		else:
			return 2

	if protocol_version != "1" or report.is_empty():
		return 2

	var file := FileAccess.open(report, FileAccess.WRITE)
	if file == null:
		return 2

	var result: int = _requested_result(selections)
	var leaves := selections
	if leaves.size() == 0:
		leaves = PackedStringArray(["exit::0"])

	file.store_string("TAP version 13\n")
	file.store_string("# foundry-test-adapter: 1\n")
	file.store_string("1.." + str(leaves.size()) + "\n")
	for i in leaves.size():
		_store_point(file, i + 1, leaves[i], result == 0)
	file.close()
	return result

func _requested_result(selections: PackedStringArray) -> int:
	for selection in selections:
		if selection.begins_with("exit::"):
			return selection.substr(6).to_int()
	return 0

func _store_point(file: FileAccess, number: int, leaf_id: String, passed: bool) -> void:
	# The complete point and its diagnostic block are assembled before the write so a
	# reader never observes a half-written unit.
	var status: String = "not ok "
	if passed:
		status = "ok "
	var unit: String = status + str(number) + " - exit_status.point_" + str(number) + "\n"
	unit += "  ---\n"
	unit += "  _foundry:\n"
	unit += "    id: \"" + leaf_id + "\"\n"
	unit += "    duration_ms: 0\n"
	unit += "    status_detail: \"\"\n"
	unit += "  ...\n"
	file.store_string(unit)
	file.flush()
