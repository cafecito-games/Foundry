# A constant bound to a specialized class handle whose arguments name a class type parameter is
# rejected wherever the analyzer can see the class the parameter belongs to. A generic trait is
# analyzed once, before any implementer's arguments are known, so its own bodies are left alone -- and
# the constant a trait body folds falls back to the bare unspecialized handle rather than baking the
# parameter's erasure in. Baking it in would record the definite evidence `Variant`, which the
# `Holder[V]` return slot then rejects: the construction would fail on legitimate code.
class Holder[T]:
	var value: T


trait Keeper[V]:
	func build_local() -> Holder[V]:
		const Local = Holder[V]
		return Local.new()


class ConcreteKeeper:
	uses Keeper[int]


class ForwardingKeeper[U]:
	uses Keeper[U]


func test() -> void:
	var concrete: Variant = ConcreteKeeper.new().build_local()
	print(concrete is Holder)
	print(concrete is Holder[Variant])

	var forwarded: Variant = ForwardingKeeper[int].new().build_local()
	print(forwarded is Holder)
	print(forwarded is Holder[Variant])
	print("trait constant fallback ok")
