# A generic method can be applied with an explicit type-argument list on an instance or attribute
# receiver (`receiver.method[T](...)`), not only on a bare-identifier (self) receiver.
class Holder:
	func pick[T](value: T) -> T:
		return value


class Outer:
	var holder := Holder.new()


func test() -> void:
	var holder := Holder.new()

	# Instance receiver.
	var picked: int = holder.pick[int](5)
	print(picked)

	# `self` receiver still works through the same path.
	var via_self: String = self.identity[String]("hi")
	print(via_self)

	# Attribute-chain receiver: the method lives on a member of another object.
	var outer := Outer.new()
	var nested: float = outer.holder.pick[float](1.5)
	print(nested)


func identity[T](value: T) -> T:
	return value
