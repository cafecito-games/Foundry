# `create_proxy[T](handler)` can forward an enclosing generic class's type
# parameter: inside `Mock[T]`, `T` is reified onto the instance at construction
# (e.g. `Mock[Greeter].new()` binds T = Greeter), so the proxy's runtime descriptor
# is the bound concrete type.
trait Greeter:
	abstract func greet(subject: String) -> String

class Mock[T]:
	var calls: Array = []

	func _handle(method_name: StringName, _args: Array) -> Variant:
		calls.append(str(method_name))
		return "stub:" + str(method_name)

	func build() -> T:
		return create_proxy[T](_handle)

func test() -> void:
	var mock := Mock[Greeter].new()
	var greeter := mock.build()

	# The proxy routes the contract call through the handler and satisfies `is T`.
	print(greeter.greet("World"))
	print(greeter is Greeter)
	print(mock.calls)
