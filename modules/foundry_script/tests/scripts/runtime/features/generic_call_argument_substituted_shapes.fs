# The call-site check reuses the concrete boundaries the language already has, so every substituted
# shape a concrete parameter can express is decided the same way here: declared integer widths,
# nullable slots, and native classes alike. It also runs after every argument expression has been
# evaluated, once each and in written order, so adding it changes no evaluation semantics.
var evaluation_order: Array[String] = []


func identity[T](value: T) -> T:
	return value


func pair[T](_left: T, right: T) -> T:
	return right


func note(label: String, value: Variant) -> Variant:
	evaluation_order.append(label)
	return value


func untyped_int() -> Variant:
	return 5


func untyped_float() -> Variant:
	return 7.0


func untyped_null() -> Variant:
	return null


func untyped_resource() -> Variant:
	return Resource.new()


func test() -> void:
	Utils.check(pair[int](note("left", untyped_int()), note("right", untyped_float())) == 7)
	Utils.check(evaluation_order == ["left", "right"])

	Utils.check(identity[long](untyped_int()) == 5)
	# A nullable substitution keeps admitting null, exactly as a concrete nullable parameter does.
	Utils.check(identity[int?](untyped_null()) == null)
	Utils.check(identity[int?](untyped_int()) == 5)

	var resource: Resource = identity[Resource](untyped_resource())
	Utils.check(resource != null)

	# A statically typed supertype narrows into the substituted type on the promise of a run-time
	# check, and that promise is kept here rather than at the erased destination.
	var holder: Object = Resource.new()
	var narrowed: Resource = identity[Resource](holder)
	Utils.check(narrowed != null)

	print("generic call argument shapes ok")
