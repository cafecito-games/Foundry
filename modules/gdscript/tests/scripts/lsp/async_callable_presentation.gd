extends Node

var on_ready: AsyncCallable[[int], String]

async func fetch(handler: AsyncCallable[[int], String]) -> String:
	return await handler.call(1)

func use_fetch() -> void:
	fetch(on_ready)
