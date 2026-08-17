# A class type parameter is reified onto the instance, which is what lets an instance method check a
# value against the receiver's actual argument. A static function has no receiver, so the same slot
# has nothing to check against and emits no check. A gradual source -- a `Variant` or any other
# non-hard value -- gets the same answer a concrete one gets in `class_type_parameter_static_frame_
# rejects_concrete.fs`: gradual means "checked at the destination boundary at run time", and a
# destination that emits no check can honor neither half of that contract.
class Crate[T]:
	static func bare_local(value: Variant) -> void:
		var kept: T = value
		print(kept)

	static func later_store(seed: Variant, value: Variant) -> void:
		var kept: T = seed
		kept = value
		print(kept)

	static func gradual_return(value: Variant) -> T:
		return value

	static func array_local(value: Variant) -> void:
		var kept: Array[T] = value
		print(kept)

	static func dictionary_local(value: Variant) -> void:
		var kept: Dictionary[String, T] = value
		print(kept)

	static func weak_source(value) -> void:
		var kept: T = value
		print(kept)

	static func literal_element(value: Variant) -> void:
		# A container literal's element reaches the parameter slot on its own, and a static frame can
		# decide it no better there than it can decide the whole container.
		var kept: Array[T] = [value]
		print(kept)

	static func concrete_literal_element() -> void:
		var kept: Array[T] = [1]
		print(kept)

	static func gradual_tuple(value: Variant) -> void:
		var kept: (int, T) = value
		print(kept)

	static func gradual_nullable_tuple(value: Variant) -> void:
		# A tuple element carries its own "or null" to run time, so a nullable element is receiver-
		# relative exactly as a bare one is -- and a static frame can decide neither.
		var kept: (int, T?) = value
		print(kept)

	static func inside_lambda(value: Variant):
		# A lambda in a static frame has no receiver either, and the capture it would need to check the
		# slot does not exist there.
		var keeper := func(v):
			var kept: T = v
			return kept
		return keeper.call(value)

	func instance_local(value: Variant) -> T:
		# The same assignment inside an instance method is checked against this receiver's argument.
		var kept: T = value
		return kept


func test():
	Crate[int].bare_local(1)
	Crate[int].later_store(1, 2)
	print(Crate[int].gradual_return(3))
	Crate[int].array_local([4])
	Crate[int].dictionary_local({"a": 5})
	Crate[int].weak_source(6)
	Crate[int].literal_element(7)
	Crate[int].concrete_literal_element()
	Crate[int].gradual_tuple((1, 2))
	Crate[int].gradual_nullable_tuple((1, 2))
	print(Crate[int].inside_lambda(7))
	print(Crate[int].new().instance_local(8))
