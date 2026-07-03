class_name ScriptTestExecutionFixture
extends Node

var abort_ran_after := false

func sync_return() -> int:
	return 42

func async_frames() -> int:
	await get_tree().process_frame
	await get_tree().process_frame
	return 7

func async_never_signal() -> void:
	var emitter := _SignalEmitter.new()
	add_child(emitter)
	await emitter.stuck

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

func runtime_error() -> void:
	var values: Array[int] = []
	values[0] = 1

func push_error_only() -> void:
	push_error("fatal test error")

func nested_inner_spin() -> void:
	var execution := ScriptTestExecution.new()
	execution.timeout_seconds = 0.1
	var result: ScriptTestExecutionResult = await execution.callv(self, &"cpu_spin", [])
	print(result.status)

func nested_outer() -> int:
	var execution := ScriptTestExecution.new()
	execution.timeout_seconds = 0.5
	var result: ScriptTestExecutionResult = await execution.callv(self, &"nested_inner_spin", [])
	return result.status

func long_await_no_timeout() -> int:
	await get_tree().create_timer(0.15).timeout
	return 99

func probe_abort_available() -> bool:
	return ScriptTestAbort.is_available()

class _SignalEmitter:
	extends Node
	signal stuck
