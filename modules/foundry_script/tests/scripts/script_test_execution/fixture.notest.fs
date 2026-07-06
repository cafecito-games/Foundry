extends Node

var abort_ran_after := false

func sync_return() -> int:
	return 42

func async_frames() -> int:
	await get_tree().process_frame
	await get_tree().process_frame
	return 7

func async_never_signal() -> void:
	await get_tree().process_frame
	while true:
		pass

func cpu_spin() -> void:
	while true:
		pass

func sync_abort() -> int:
	ScriptTestAbort.abort_current("sync abort")
	abort_ran_after = true
	return 0

func async_abort() -> void:
	await get_tree().process_frame
	ScriptTestAbort.abort_current("async abort")
	abort_ran_after = true

func async_capture_diagnostic() -> int:
	await get_tree().process_frame
	push_error("async capture error")
	return 13

func runtime_error() -> void:
	var values: Array[int] = []
	values[0] = 1

func push_error_only() -> void:
	push_error("fatal test error")

func long_await_no_timeout() -> int:
	await get_tree().create_timer(0.15).timeout
	return 99

func await_guard_call_pattern() -> int:
	var execution := ScriptTestExecution.new()
	execution.timeout_seconds = 1.0
	execution.abort_on_fatal = false
	var execution_result: ScriptTestExecutionResult = await execution.guard_callv(self, &"async_frames", [])
	return int(execution_result.get_return_value())

func probe_abort_available() -> bool:
	return ScriptTestAbort.is_available()
