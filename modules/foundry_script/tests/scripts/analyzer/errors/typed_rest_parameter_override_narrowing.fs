# An override may not narrow the element type of a rest tail, and may not answer a gradual base tail
# with a typed one: either would reject trailing arguments a polymorphic call was checked against.
class Animal:
	func label() -> String:
		return "animal"


class Dog:
	extends Animal

	func label() -> String:
		return "dog"


class Base:
	func visit(...pets: Array[Animal]) -> long:
		return pets.size()


class Narrower:
	extends Base

	func visit(...pets: Array[Dog]) -> long:
		return pets.size()


class GradualBase:
	func visit(...values: Array) -> long:
		return values.size()


class TypedTail:
	extends GradualBase

	func visit(...values: Array[int]) -> long:
		return values.size()


func test() -> void:
	print(Narrower.new().visit())
	print(TypedTail.new().visit())
