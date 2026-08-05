# Specialization is static only. Once a specialized union's value crosses a `Variant` boundary the
# arguments are gone: the value is the same read-only `[tag, payload...]` Array every union uses, a
# runtime `is` test can prove only the declared tag domain, and equality and hashing stay deep Array
# value semantics across specializations of one declaration.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

func erase(value: Variant) -> Variant:
	return value

func accepts_int_result(value: Variant) -> bool:
	return value is Result[int, String]

func accepts_float_result(value: Variant) -> bool:
	return value is Result[float, String]

func test():
	var erased: Variant = erase(Result[int, String].Ok(1))

	# The erased value is the ordinary tagged-union Array, unchanged by the specialization.
	print(typeof(erased) == TYPE_ARRAY)
	@warning_ignore("unsafe_method_access")
	print(erased.is_read_only())
	print(erased[0])
	@warning_ignore("unsafe_method_access")
	print(erased.size())

	# A dynamic test can only check the tag domain, so a differently specialized `Result` accepts the
	# very same value. This is the documented erasure limit, not a soundness claim.
	print(accepts_int_result(erased))
	print(accepts_float_result(erased))

	# Two values built from different specializations that agree on tag and payload are equal and
	# hash alike, exactly as two ordinary tagged-union values would.
	var twin: Variant = erase(Result[int, float].Ok(1))
	print(erased == twin)
	print(hash(erased) == hash(twin))
	print(erased == erase(Result[int, String].Err("1")))
