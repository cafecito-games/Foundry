# A non-async func that contains "await" is itself a coroutine; with a "void" result the root-discard
# is a Coroutine[void] fire-and-forget launch, which is exempted from MISSING_AWAIT.
func coroutine() -> void:
	@warning_ignore("redundant_await")
	await 0

func test() -> void:
	await coroutine()
	coroutine()
