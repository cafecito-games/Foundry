# `is`/`as` against a generic class observe the reified type arguments. A raw target asks only the
# nominal question; a specialized target additionally requires the value's effective arguments for
# that base to be known and invariantly equal, so a raw value or a mismatched specialization fails.
# `as` succeeds exactly when the matching `is` does, returning the same object, and yields `null`
# otherwise. Instance tests and class-handle tests apply the same rules to their own runtime values.
class Crate[T]:
	var item: T


class Other[T]:
	var item: T


func test() -> void:
	var int_crate: Variant = Crate[int].new()
	var string_crate: Variant = Crate[String].new()
	var raw_crate: Variant = Crate.new()
	var variant_crate: Variant = Crate[Variant].new()
	var other_int: Variant = Other[int].new()
	var absent: Variant = null

	print("int crate is Crate: ", int_crate is Crate)
	print("string crate is Crate: ", string_crate is Crate)
	print("raw crate is Crate: ", raw_crate is Crate)

	print("int crate is Crate[int]: ", int_crate is Crate[int])
	print("int crate is Crate[String]: ", int_crate is Crate[String])
	print("string crate is Crate[int]: ", string_crate is Crate[int])
	print("raw crate is Crate[int]: ", raw_crate is Crate[int])

	print("variant crate is Crate[Variant]: ", variant_crate is Crate[Variant])
	print("variant crate is Crate[int]: ", variant_crate is Crate[int])
	print("int crate is Crate[Variant]: ", int_crate is Crate[Variant])

	print("other int is Crate[int]: ", other_int is Crate[int])
	print("null is Crate[int]: ", absent is Crate[int])

	var matched_cast: Variant = int_crate as Crate[int]
	print("matched cast keeps identity: ", matched_cast == int_crate)
	var mismatched_cast: Variant = int_crate as Crate[String]
	print("mismatched cast is null: ", mismatched_cast == null)
	var raw_target_cast: Variant = int_crate as Crate
	print("raw target cast keeps identity: ", raw_target_cast == int_crate)
	var raw_value_cast: Variant = raw_crate as Crate[int]
	print("raw value cast is null: ", raw_value_cast == null)
	var null_cast: Variant = absent as Crate[int]
	print("null cast is null: ", null_cast == null)

	var int_handle: Variant = Crate[int]
	var raw_handle: Variant = Crate

	print("int handle is Type[Crate]: ", int_handle is Type[Crate])
	print("int handle is Type[Crate[int]]: ", int_handle is Type[Crate[int]])
	print("int handle is Type[Crate[String]]: ", int_handle is Type[Crate[String]])
	print("raw handle is Type[Crate]: ", raw_handle is Type[Crate])
	print("raw handle is Type[Crate[int]]: ", raw_handle is Type[Crate[int]])
	print("null is Type[Crate[int]]: ", absent is Type[Crate[int]])

	var matched_handle_cast: Variant = int_handle as Type[Crate[int]]
	print("matched handle cast keeps identity: ", matched_handle_cast == int_handle)
	var mismatched_handle_cast: Variant = int_handle as Type[Crate[String]]
	print("mismatched handle cast is null: ", mismatched_handle_cast == null)
	var raw_handle_cast: Variant = raw_handle as Type[Crate[int]]
	print("raw handle cast is null: ", raw_handle_cast == null)

	print("is/as specialized ok")
