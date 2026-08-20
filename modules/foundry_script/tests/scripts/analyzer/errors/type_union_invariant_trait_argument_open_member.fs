# Reconciling a union's members as a set only forgives the reordering an unreified type parameter can
# still produce. A member that contradicts every alternative on the other side has no pairing left at
# any instantiation, so the open sibling beside it does not rescue the conformance.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class Unreconcilable[U]:
	uses Keeper[Array[U] | String]


func take(value: Keeper[Array[int] | Array[long]]) -> void:
	print(value.label())


func relay[U](value: Unreconcilable[U]) -> void:
	take(value)


func test() -> void:
	relay[long](Unreconcilable.new())
