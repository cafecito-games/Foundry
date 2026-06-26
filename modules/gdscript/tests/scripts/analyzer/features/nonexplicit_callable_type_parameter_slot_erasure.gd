# A generic function reference records its type-parameter slot (`T`) in the rich Callable signature.
# The MethodInfo comparison path erased type parameters to Variant, so referencing a generic function
# against a `Callable[[Variant], void]` target stays accepted: the structural comparison must erase
# type-parameter slots the same way rather than treating `T` as a concrete type distinct from Variant.
func accept_any_generic[T](_value: T) -> void:
	pass


func test() -> void:
	var handler: Callable[[Variant], void] = accept_any_generic
	print(handler != null)
