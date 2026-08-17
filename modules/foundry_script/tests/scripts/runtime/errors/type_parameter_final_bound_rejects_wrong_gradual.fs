# A type parameter whose only bound is a `final` class denotes exactly that class: no subtype of the
# bound can exist, so every position declared with the parameter is compiled as the bound itself and a
# store into one is really checked. A gradual value that is not the bound therefore fails where it
# crosses the declaration -- naming that boundary and the resolved bound -- instead of being laundered
# into the slot and failing much later at some use of what it holds.
final class FbrBadge:
	func read() -> String:
		return "badge"


func bare_local[T: FbrBadge](value: Variant) -> void:
	var kept: T = value
	print(kept.read())


func later_store[T: FbrBadge](initial: T, value: Variant) -> void:
	var kept: T = initial
	kept = value
	print(kept.read())


func gradual_return[T: FbrBadge](value: Variant) -> T:
	return value


func nullable_local[T: FbrBadge](value: Variant) -> void:
	var kept: T? = value
	print(kept)


func array_element[T: FbrBadge](value: Variant) -> void:
	var kept: Array[T] = [value]
	print(kept.size())


func dictionary_value[T: FbrBadge](value: Variant) -> void:
	var kept: Dictionary[String, T] = { "first": value }
	print(kept.size())


func dictionary_key[T: FbrBadge](value: Variant) -> void:
	var kept: Dictionary[T, String] = { value: "first" }
	print(kept.size())


func tuple_element[T: FbrBadge](value: Variant) -> void:
	var kept: (int, T) = (1, value)
	print(kept)


final class FbrFactory:
	func read() -> String:
		return "factory"


func handle_element[T: Type[FbrFactory]](value: Variant) -> void:
	# `ContainerType` records the class-handle layer, so a container element resolved from a
	# `Type[...]` bound is checked as the handle it is rather than erased.
	var kept: Array[T] = [value]
	print(kept.size())


class FbrHolder[T: FbrBadge]:
	# A static frame has no receiver, so the bound is the only thing that can decide the store -- and
	# resolving it is what makes the store decidable at all.
	static func static_bare_local(value: Variant) -> void:
		var kept: T = value
		print(kept.read())

	# An instance frame resolves the same parameter through the receiver's reification, which agrees
	# with the bound because the bound is the only argument this parameter can ever take.
	func instance_bare_local(value: Variant) -> void:
		var kept: T = value
		print(kept.read())


func untyped_int() -> Variant:
	return 5


func test() -> void:
	bare_local[FbrBadge](untyped_int())
	later_store[FbrBadge](FbrBadge.new(), untyped_int())
	print(gradual_return[FbrBadge](untyped_int()))
	nullable_local[FbrBadge](untyped_int())
	array_element[FbrBadge](untyped_int())
	dictionary_value[FbrBadge](untyped_int())
	dictionary_key[FbrBadge](untyped_int())
	tuple_element[FbrBadge](untyped_int())
	handle_element[Type[FbrFactory]](untyped_int())
	FbrHolder[FbrBadge].static_bare_local(untyped_int())
	FbrHolder[FbrBadge].new().instance_bare_local(untyped_int())
	# An unspecialized receiver reifies nothing, so the bound baked into the slot is the only thing left
	# to decide the store -- and it decides it.
	FbrHolder.new().instance_bare_local(untyped_int())
