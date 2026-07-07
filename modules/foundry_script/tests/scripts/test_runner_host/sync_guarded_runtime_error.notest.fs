extends ScriptRunner

func runtime_error() -> void:
	var values: Array[int] = []
	values[0] = 1

func run(args: PackedStringArray) -> int:
	var execution := ScriptTestExecution.new()
	var result: Variant = execution.guard_callv(self, &"runtime_error", [])
	var parsed: ScriptTestExecutionResult = result as ScriptTestExecutionResult
	if parsed == null:
		return 1
	if parsed.get_status() != ScriptTestExecutionResult.STATUS_RUNTIME_ERROR:
		return 1
	return 0
