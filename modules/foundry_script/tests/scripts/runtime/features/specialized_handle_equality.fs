# Specialized class handle equality is exact structural identity: the same script, and every type
# argument equal recursively, including arity. Gradual compatibility — where absent evidence is
# accepted rather than rejected — is expressed by `is` and by the shared type relation, never by
# `==`, so that `handle == Holder[int]` proves the handle really carries `int`. `_hash_code()`
# agrees with that equality, so equal specializations collapse to one dictionary entry and unequal
# ones stay apart.
class Holder[T]:
	var value: T


class Pair[K, V]:
	var key: K
	var value: V


func gradual_slot(handle: Type[Holder[int]]) -> Variant:
	return handle


func test() -> void:
	var int_handle: Variant = Holder[int]
	var int_handle_again: Variant = Holder[int]
	var string_handle: Variant = Holder[String]
	var variant_handle: Variant = Holder[Variant]
	var bare_handle: Variant = Holder

	print("exact arguments equal: ", int_handle == int_handle_again)
	print("differing argument: ", int_handle == string_handle)
	print("differing argument reversed: ", string_handle == int_handle)
	print("variant argument against concrete: ", variant_handle == int_handle)
	print("concrete against variant argument: ", int_handle == variant_handle)
	print("bare against specialized: ", bare_handle == int_handle)
	print("specialized against bare: ", int_handle == bare_handle)
	print("bare against bare: ", bare_handle == Holder)

	var pair_handle: Variant = Pair[int, String]
	var bare_pair_handle: Variant = Pair
	print("two arguments exact: ", pair_handle == Pair[int, String])
	print("two arguments transposed: ", pair_handle == Pair[String, int])
	print("differing arity across classes: ", pair_handle == int_handle)
	print("bare against two arguments: ", bare_pair_handle == pair_handle)

	var nested_int: Variant = Holder[Array[int]]
	var nested_string: Variant = Holder[Array[String]]
	print("nested argument exact: ", nested_int == Holder[Array[int]])
	print("nested argument differing element: ", nested_int == nested_string)
	print("nested against unnested: ", nested_int == int_handle)

	var handle_layer: Variant = Holder[Type[Node]]
	var class_layer: Variant = Holder[Node]
	print("handle layer exact: ", handle_layer == Holder[Type[Node]])
	print("handle layer against bare class: ", handle_layer == class_layer)

	# The gradual side accepts a handle whose argument evidence is absent, while `==` against a
	# concrete specialization still refuses it. That split is the point of the operator.
	var stored: Variant = gradual_slot(bare_handle)
	print("gradual slot accepts absent evidence: ", stored != null)
	print("stored handle is Type[Holder]: ", stored is Type[Holder])
	print("stored handle equals concrete: ", stored == int_handle)

	var registry: Dictionary = {}
	registry[Holder[int]] = "first"
	registry[Holder[int]] = "second"
	print("equal specializations collapse: %d" % registry.size())
	print("collapsed entry keeps the last write: ", registry[Holder[int]])
	registry[Holder[String]] = "other"
	registry[Holder] = "bare"
	print("unequal specializations separate: %d" % registry.size())

	print("specialized handle equality ok")
