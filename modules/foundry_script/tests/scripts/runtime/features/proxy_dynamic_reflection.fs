# `godot.reflection.create_proxy_dynamic(type, handler)` is the namespaced surface
# for building a dynamic proxy: every contract call on the proxied trait/abstract
# type is routed through the handler, invoked as `handler.call(method_name, args)`.
trait Repository:
	abstract func find(id: int) -> String
	abstract func count() -> int

func test() -> void:
	var calls: Array = []
	var stub := {
		"find": "stubbed-find",
		"count": 3,
	}

	var repo := godot.reflection.create_proxy_dynamic(Repository, func(method_name: StringName, _args: Array) -> Variant:
		calls.append(str(method_name))
		return stub.get(method_name, null)) as Repository

	# Each call reaches the handler and returns its (coerced) stubbed value.
	print(repo.find(7))
	print(repo.count())
	# The handler observed both calls, in order.
	print(calls)
	# The proxy satisfies the proxied trait.
	print(repo is Repository)
