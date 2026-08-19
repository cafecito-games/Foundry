# The declared width a gradual store enforces is a range check, not a carrier restriction: every
# value the destination's own type can hold still stores and returns exactly as before, and a
# destination with no declared width is not checked at all.
func produce_int(value: Variant) -> int:
	return value


func produce_long(value: Variant) -> long:
	return value


func produce_uint(value: Variant) -> uint:
	return value


func produce_text(value: Variant) -> String:
	return value


func test() -> void:
	# Same-carrier stores at each declared width.
	var signed_source: Variant = -2147483648
	var narrow: int = signed_source
	print(narrow)

	var wide_source: Variant = 5000000000
	var wide: long = wide_source
	print(wide)

	var unsigned_source: Variant = 4294967295U
	var unsigned_narrow: uint = unsigned_source
	print(unsigned_narrow)

	var unsigned_wide_source: Variant = 5000000000UL
	var unsigned_wide: ulong = unsigned_wide_source
	print(unsigned_wide)

	# The `uint` -> `long` widening: `long` contains the whole `uint` range, so it still crosses.
	var widen_source: Variant = 4294967295U
	var widened: long = widen_source
	print(widened)

	# A nullable destination stores null verbatim and range-checks anything else.
	var null_source: Variant = null
	var maybe_missing: int? = null_source
	print("stored null: ", maybe_missing == null)

	var present_source: Variant = 7
	var maybe_present: int? = present_source
	print(maybe_present)

	# A converting store is checked on the truncated result, which is in range here.
	var float_source: Variant = 2.5
	var truncated: int = float_source
	print(truncated)

	# A non-integer destination carries no width descriptor and is unaffected.
	var text_source: Variant = "unchecked"
	var text: String = text_source
	print(text)

	# The same rules at a typed return.
	print(produce_int(-2147483648))
	print(produce_long(5000000000))
	print(produce_uint(4294967295U))
	print(produce_long(4294967295U))
	print(produce_int(2.5))
	print(produce_text("unchecked"))
