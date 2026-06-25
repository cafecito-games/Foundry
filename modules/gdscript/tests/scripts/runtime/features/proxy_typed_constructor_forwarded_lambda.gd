# Forwarding a class type parameter into create_proxy[T] from inside a lambda needs
# `self` (to read the reified binding), so create_proxy[T] marks the enclosing
# lambda as using self even when it touches nothing else on self.
trait Greeter:
	@abstract func greet(subject: String) -> String

class Mock[T]:
	func make_builder(handler: Callable) -> Callable:
		return func() -> T:
			return create_proxy[T](handler)

func test() -> void:
	var calls: Array = []
	var handler := func(method_name: StringName, _args: Array) -> Variant:
		calls.append(str(method_name))
		return "stub:" + str(method_name)

	var mock := Mock[Greeter].new()
	var builder := mock.make_builder(handler)
	var greeter = builder.call()

	print(greeter.greet("World"))
	print(greeter is Greeter)
	print(calls)
