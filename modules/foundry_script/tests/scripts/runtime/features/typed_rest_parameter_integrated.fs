# One integrated exercise of the shipped typed rest contract: fixed defaults and named fixed
# arguments ahead of a typed tail, a typed variadic Callable, a broadening override, a trait
# witness, and a generic rest helper. Each assertion stands alone so a failure names one surface.

class Animal:
	func label() -> String:
		return "animal"


class Dog:
	extends Animal

	func label() -> String:
		return "dog"


trait Collects:
	abstract func collect(...pets: Array[Dog]) -> int


class Base:
	func collect(...pets: Array[Dog]) -> int:
		return pets.size()


class Broadened:
	extends Base

	# Rest elements are contravariant, so an override may accept a broader element type.
	func collect(...pets: Array[Animal]) -> int:
		return 100 + pets.size()


class Witness:
	uses Collects

	func collect(...pets: Array[Dog]) -> int:
		return 200 + pets.size()


func tally(prefix: String, scale: int = 2, ...values: Array[int]) -> int:
	Utils.check(values.is_typed())
	Utils.check(values.get_typed_builtin() == TYPE_INT)
	var total := prefix.length()
	for value in values:
		total += value * scale
	return total


func generic_tally[T](...values: Array[T]) -> int:
	# Method type arguments are not reified in the call frame, so a method-dependent rest element is
	# erased at runtime even though the call site statically sees a concrete element type.
	Utils.check(not values.is_typed())
	return values.size()


func test() -> void:
	# A fixed default in front of the tail, with no surplus argument at all.
	Utils.check(tally("ab") == 2)
	# Fixed arguments passed positionally, then surplus arguments collected and typed.
	Utils.check(tally("ab", 3, 1, 2) == 11)
	# A named fixed argument binds the fixed slot; no positional argument may follow it, so the
	# rest parameter stays empty.
	Utils.check(tally("ab", scale = 5) == 2)

	# A typed variadic Callable carries the rich signature through a value.
	var callback: Callable[[String, int, ...Array[int]], int] = tally
	Utils.check(callback.call("ab", 3, 1, 2) == 11)
	Utils.check(callback.callv(["ab", 3, 4]) == 14)

	# A broadening override is reached through the base static type.
	var base: Base = Broadened.new()
	Utils.check(base.collect(Dog.new(), Dog.new()) == 102)
	Utils.check(Base.new().collect(Dog.new()) == 1)

	# A trait witness satisfies the same typed requirement.
	var witness: Collects = Witness.new()
	Utils.check(witness.collect(Dog.new(), Dog.new(), Dog.new()) == 203)

	# Concrete and generic rest arrays sit on opposite sides of the locked reification boundary.
	Utils.check(generic_tally(1, 2, 3) == 3)
	Utils.check(generic_tally[int]() == 0)
	print("ok")
