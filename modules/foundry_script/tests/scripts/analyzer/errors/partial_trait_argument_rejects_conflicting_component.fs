# A composite trait argument states the components it fixes even when another component is still an
# unreified parameter, so `Keeper[Pair[int, U]]` contradicts `Keeper[Pair[String, float]]` on the
# first component alone. Every static boundary asks the same membership question, so the
# contradiction is rejected at a store, a parameter, a return, a member write, an `is` and an `as`.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class Pair[A, B]:
	pass


class Partial[U]:
	uses Keeper[Pair[int, U]]


class Holder:
	var kept: Keeper[Pair[String, float]]


func take(value: Keeper[Pair[String, float]]) -> void:
	print(value.label())


func give() -> Keeper[Pair[String, float]]:
	return Partial[float].new()


func test() -> void:
	var slot: Keeper[Pair[String, float]] = Partial[float].new()
	print(slot.label())
	take(Partial[float].new())
	var holder := Holder.new()
	holder.kept = Partial[float].new()
	print(give())
	var conformer := Partial[float].new()
	print(conformer is Keeper[Pair[String, float]])
	print(conformer as Keeper[Pair[String, float]])
