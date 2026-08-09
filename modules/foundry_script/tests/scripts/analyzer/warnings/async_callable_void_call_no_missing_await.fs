# An AsyncCallable whose signature returns "void" produces a Coroutine[void] from .call(); discarding
# it at root loses nothing, so MISSING_AWAIT does not fire.
async func _work(_value: int) -> void:
	pass


func test() -> void:
	var handler: AsyncCallable[[int], void] = _work
	handler.call(7)
