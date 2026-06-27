# Invoking a bare AsyncCallable yields a coroutine, so using its result without "await" in a
# non-root position is an error.
async func _async() -> int:
	return 1


func test() -> void:
	var cb: AsyncCallable = _async
	var value = cb.call()
	print(value)
