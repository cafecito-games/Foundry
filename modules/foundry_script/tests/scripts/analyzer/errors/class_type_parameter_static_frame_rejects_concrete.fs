# A class type parameter is reified onto the instance, which is what lets an instance method check a
# concrete value against the receiver's actual argument. A static function has no receiver, so there
# is nothing to reify against and no check to license the assignment: the concrete value would be
# laundered into the parameter slot untested, exactly as it would through a method-scope parameter.
class Crate[T]:
	static func seeded_local() -> void:
		var kept: T = 5
		print(kept)

	static func seeded_return() -> T:
		return 5

	static func seeded_container(concrete: Array[int]) -> Array[T]:
		var kept: Array[T] = concrete
		return kept

	func instance_local() -> T:
		# The same assignment inside an instance method is checked against this receiver's argument.
		var kept: T = 5
		return kept


func test():
	Crate[int].seeded_local()
	print(Crate[int].seeded_return())
	print(Crate[int].seeded_container([1, 2]))
	print(Crate[int].new().instance_local())
