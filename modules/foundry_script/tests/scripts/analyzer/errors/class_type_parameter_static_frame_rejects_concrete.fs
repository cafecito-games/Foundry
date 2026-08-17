# A class type parameter is reified onto the instance, which is what lets an instance method check a
# concrete value against the receiver's actual argument. A static function has no receiver, so there
# is nothing to reify against and no check to license the assignment: the concrete value would be
# laundered into the parameter slot untested, exactly as it would through a method-scope parameter.
# A gradual source is judged by the same criterion; see
# `class_type_parameter_static_frame_rejects_gradual.fs`.
class Crate[T]:
	static func seeded_local() -> void:
		var kept: T = 5
		print(kept)

	static func seeded_return() -> T:
		return 5

	static func seeded_container(concrete: Array[int]) -> Array[T]:
		var kept: Array[T] = concrete
		return kept

	static func seeded_tuple() -> void:
		# A tuple slot is checked against the receiver's reified argument in an instance frame; a static
		# frame has none, so the concrete element cannot be licensed here either.
		var kept: (int, T) = (1, 5)
		print(kept)

	static func seeded_nullable_tuple() -> void:
		# A nullable tuple element is receiver-relative on its own -- the descriptor expresses "this type
		# or null" -- so a static frame has just as little to license this assignment with.
		var kept: (int, T?) = (1, 5)
		print(kept)

	static func seeded_tuple_in_lambda() -> void:
		# A lambda declared in a static frame is static too, so it has no receiver to resolve against
		# and the slot is rejected there rather than compiled into a check that could never run.
		var keeper := func():
			var kept: (int, T?) = (1, 5)
			return kept
		print(keeper.call())

	func instance_local() -> T:
		# The same assignment inside an instance method is checked against this receiver's argument.
		var kept: T = 5
		return kept


func test():
	Crate[int].seeded_local()
	print(Crate[int].seeded_return())
	print(Crate[int].seeded_container([1, 2]))
	Crate[int].seeded_tuple()
	Crate[int].seeded_nullable_tuple()
	Crate[int].seeded_tuple_in_lambda()
	print(Crate[int].new().instance_local())
