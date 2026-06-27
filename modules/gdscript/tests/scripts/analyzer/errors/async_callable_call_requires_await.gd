# Invoking an AsyncCallable produces a coroutine. Using the result without "await" in a non-root
# position is an error, exactly as for a direct coroutine call.
async func _work(value: int) -> String:
	return str(value)


func test() -> void:
	var handler: AsyncCallable[[int], String] = _work
	var result: String = handler.call(7)
	print(result)
