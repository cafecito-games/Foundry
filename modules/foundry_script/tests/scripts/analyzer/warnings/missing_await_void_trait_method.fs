# Calling a flattened concrete async trait method that returns "void" without "await" is a
# fire-and-forget launch: the Coroutine[void] result loses nothing, so MISSING_AWAIT stays silent.
extends RefCounted
uses Fetcher

trait Fetcher:
	async func fetch() -> void:
		pass

func test() -> void:
	fetch()
