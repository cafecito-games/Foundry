extends ScriptTestRunner

const FIXTURE := preload("res://script_test_execution/fixture.notest.fs")

func run(args: PackedStringArray) -> int:
	var suite := FIXTURE.new()
	var execution := ScriptTestExecution.new()
	var result := execution.guard_callv(suite, &"runtime_error", [])
	var parsed := result as ScriptTestExecutionResult
	if parsed == null:
		return 1
	if parsed.get_status() != ScriptTestExecutionResult.STATUS_RUNTIME_ERROR:
		return 1
	return 0
