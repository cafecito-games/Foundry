# A rest tail sits in parameter position, so its element type is contravariant: an override may keep
# the element type, broaden it, or drop to a gradual tail. A base that promises no trailing arguments
# constrains nothing, so an override is free to add a rest tail of its own.
class Animal:
	func label() -> String:
		return "animal"


class Dog:
	extends Animal

	func label() -> String:
		return "dog"


class Base:
	func visit(...pets: Array[Dog]) -> long:
		return pets.size()


class SameElement:
	extends Base

	func visit(...pets: Array[Dog]) -> long:
		return 10 + pets.size()


class BroaderElement:
	extends Base

	func visit(...pets: Array[Animal]) -> long:
		return 20 + pets.size()


class GradualTail:
	extends Base

	func visit(...pets: Array) -> long:
		return 30 + pets.size()


abstract class AbstractSink:
	abstract func absorb(...pets: Array[Dog]) -> long


class BroaderSink:
	extends AbstractSink

	func absorb(...pets: Array[Animal]) -> long:
		return 40 + pets.size()


class FixedArity:
	func visit(count: int) -> int:
		return count


class AddsOwnTail:
	extends FixedArity

	func visit(count: int, ...extra: Array[String]) -> int:
		return 50 + count + extra.size()


func test() -> void:
	var same: Base = SameElement.new()
	print(same.visit(Dog.new(), Dog.new()))

	var broader: Base = BroaderElement.new()
	print(broader.visit(Dog.new()))

	var gradual: Base = GradualTail.new()
	print(gradual.visit(Dog.new(), Dog.new(), Dog.new()))

	var sink: AbstractSink = BroaderSink.new()
	print(sink.absorb(Dog.new()))

	var fixed: FixedArity = AddsOwnTail.new()
	print(fixed.visit(1))

	print(AddsOwnTail.new().visit(1, "a", "b"))
