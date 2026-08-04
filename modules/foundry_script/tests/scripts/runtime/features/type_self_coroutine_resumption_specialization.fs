# Validation performed after a resumption reports the specialization the call was made through, not
# the class the witness was declared against. The witness below is declared for `Resource`, the call
# is made through `ImageTexture`, and the container built after the suspension rejects a `Material`
# by naming `ImageTexture`.
signal go

var outcome: String = ""


class Crate[T]:
	var value: T


trait Packing:
	abstract static async func pack(gate: Signal) -> Crate[Self]


extend Resource uses Packing:
	static async func pack(gate: Signal) -> Crate[Self]:
		await gate
		return Crate[Self].new()


async func drain(handle: Coroutine[Crate]) -> void:
	var crate := await handle
	var untyped: Variant = crate
	untyped.value = ImageTexture.new()
	outcome = untyped.value.get_class()
	untyped.value = Material.new()


func test() -> void:
	var handle: Coroutine[Crate] = ImageTexture.pack(go)
	@warning_ignore("missing_await")
	drain(handle)
	go.emit()
	print(outcome)
