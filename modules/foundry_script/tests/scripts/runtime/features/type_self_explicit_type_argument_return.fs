# An explicit type-argument list binds the method's own type parameters and leaves the remaining
# `Self` to the receiver, through the same substitution the bracket-free spelling uses. The two
# spellings of one call therefore agree on the receiver-relative return type at every nesting depth,
# whatever the receiver happens to be typed as.
class Pair[A, B]:
	var first: A
	var second: B


class Cell:
	var label: String = "?"

	func pick[T](value: T) -> Self:
		print("pick ", value)
		return self

	func boxed[T](value: T) -> Pair[T, Self]:
		var pair := Pair[T, Self].new()
		pair.first = value
		pair.second = self
		return pair

	func tupled[T](value: T) -> (T, Self):
		return (value, self)

	func from_inside() -> void:
		# Unqualified and `self`-qualified explicit calls run against this frame's own receiver.
		var unqualified := pick[int](1)
		print(unqualified.label)
		var qualified: Self = self.pick[int](2)
		print(qualified.label)


class Derived extends Cell:
	var extra: String = "extra"


# A receiver typed as a type parameter keeps its own identity: `Self` resolves to `U`, not to the
# bound the generic method was looked up through.
func through_type_parameter[U: Cell](value: U) -> void:
	var explicit: U = value.pick[int](3)
	var inferred: U = value.pick(4)
	print(explicit.label, " ", inferred.label)


func test() -> void:
	var cell := Cell.new()
	cell.label = "cell"
	cell.from_inside()

	# Foreign receiver: `Self` is the receiver's type, not the calling frame's.
	var foreign: Cell = cell.pick[int](5)
	print(foreign.label)

	# A subclass receiver narrows the return to the subclass, so its own members are reachable.
	var derived := Derived.new()
	derived.label = "derived"
	print(derived.pick[int](6).extra)

	# A nested return substitutes the method type parameter and `Self` at every position.
	var pair := derived.boxed[String]("boxed")
	print(pair.first, " ", pair.second.extra)
	var packed := derived.tupled[String]("tupled")
	print(packed[0], " ", packed[1].extra)

	through_type_parameter(derived)
