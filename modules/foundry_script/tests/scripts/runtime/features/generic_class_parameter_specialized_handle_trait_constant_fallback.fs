# A constant bound to a specialized class handle whose arguments name a class type parameter is
# rejected wherever the analyzer can see the class the parameter belongs to. A generic trait is
# analyzed once, before any implementer's arguments are known, so its own bodies are left alone. The
# implementer's applied arguments are known when the flattened copy is folded, so a concrete
# application folds the handle it really means, while an implementer that forwards its own parameter
# falls back to the bare unspecialized handle: one compiled function and one constant pool are shared
# by every specialization of that implementer. The fallback never bakes the parameter's erasure in --
# recording the definite evidence `Variant` would make the `Holder[V]` return slot reject the
# construction on legitimate code.
#
# The handle is read back as a value, because constructing straight through the constant name is a
# construction and reifies from the receiver whatever the folded constant holds.
class Holder[T]:
	var value: T


trait Keeper[V]:
	func local_handle() -> Variant:
		const Local = Holder[V]
		return Local

	func build_local() -> Holder[V]:
		const Local = Holder[V]
		return Local.new()


class ConcreteKeeper:
	uses Keeper[int]


class ForwardingKeeper[U]:
	uses Keeper[U]


func test() -> void:
	print(ConcreteKeeper.new().local_handle() == Holder[int])
	print(ConcreteKeeper.new().local_handle() == Holder)
	print(ConcreteKeeper.new().build_local() is Holder[int])

	print(ForwardingKeeper[int].new().local_handle() == Holder)
	print(ForwardingKeeper[int].new().local_handle() == Holder[int])
	print(ForwardingKeeper[int].new().build_local() is Holder)
	print("trait constant fallback ok")
