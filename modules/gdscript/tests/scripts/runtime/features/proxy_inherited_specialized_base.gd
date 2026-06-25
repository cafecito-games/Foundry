# `create_proxy[T]`, compiled once inside a generic base, resolves `T`'s bound script for a derived
# instance whose base was specialized — both for a non-generic subclass that fixes the argument
# (`Mock extends Base[Greeter]`) and through a generic subclass that forwards its own parameter
# (`Forwarder[U] extends Base[U]`). A direct specialization (`Base[Greeter].new()`) still works.
trait Greeter:
	@abstract func greet(subject: String) -> String


class Base[T]:
	func _handle(method_name: StringName, _args: Array) -> Variant:
		return "stub:" + str(method_name)

	func build() -> T:
		return create_proxy[T](_handle)


class Mock extends Base[Greeter]:
	pass


class DeeperMock extends Mock:
	pass


class Forwarder[U] extends Base[U]:
	pass


func test() -> void:
	# Non-generic subclass of a specialized base.
	var mock := Mock.new()
	var g1 := mock.build()
	print(g1 != null and g1 is Greeter)
	print(g1.greet("a"))

	# A further non-generic subclass inherits the same fixed binding.
	var deeper := DeeperMock.new()
	var g2 := deeper.build()
	print(g2 != null and g2 is Greeter)

	# Generic subclass forwarding its own parameter, specialized at construction.
	var forwarder := Forwarder[Greeter].new()
	var g3 := forwarder.build()
	print(g3 != null and g3 is Greeter)

	# Direct specialization of the base.
	var base := Base[Greeter].new()
	var g4 := base.build()
	print(g4 != null and g4 is Greeter)
	print("inherited proxy ok")
