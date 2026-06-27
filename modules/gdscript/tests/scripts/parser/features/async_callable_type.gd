# AsyncCallable parses as an async-marked variant of the typed Callable. This covers the
# parser/AST support: an explicit signature, a nullable variant, an empty parameter list, a
# nested signature, and the bare form without a signature. Full async typing semantics arrive
# with later work in the structured-concurrency epic.
func _double(value: int) -> String:
	return str(value * 2)


func test() -> void:
	var handler: AsyncCallable[[int], String] = _double
	var nullable_handler: AsyncCallable[[int], String]? = null
	var no_args: AsyncCallable[[], void] = func() -> void: pass
	var nested: AsyncCallable[[Callable[[int], void]], void] = func(_cb: Callable[[int], void]) -> void: pass
	var bare: AsyncCallable = _double
	print(handler.call(2))
	print(nullable_handler.is_valid())
	print(no_args.is_valid())
	print(nested.is_valid())
	print(bare.is_valid())
