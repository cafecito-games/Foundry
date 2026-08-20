# Every numeric constant a plain slot accepts is accepted by a union holding that same type as an
# alternative, and lands on the same carrier with the same value. Each row runs the identical literal
# through both spellings and prints whether the two answers agree; a `false` anywhere is the asymmetry
# this matrix exists to catch.
func describe(v) -> String:
	if v is uint:
		return "uint " + str(v)
	if v is int:
		return "int " + str(v)
	if v is float:
		return "float " + str(v)
	return "other"


func plain_uint(v: uint) -> String:
	return describe(v)


func union_uint(v: uint | String) -> String:
	return describe(v)


func plain_int(v: int) -> String:
	return describe(v)


func union_int(v: int | String) -> String:
	return describe(v)


func plain_long(v: long) -> String:
	return describe(v)


func union_long(v: long | String) -> String:
	return describe(v)


func plain_ulong(v: ulong) -> String:
	return describe(v)


func union_ulong(v: ulong | String) -> String:
	return describe(v)


func plain_float(v: float) -> String:
	return describe(v)


func union_float(v: float | String) -> String:
	return describe(v)


func compare(label: String, plain: String, unioned: String) -> void:
	print(label + ": " + plain + " / " + unioned + " -> " + str(plain == unioned))


func test():
	compare("uint <- 5", plain_uint(5), union_uint(5))
	compare("uint <- 5L", plain_uint(5L), union_uint(5L))
	compare("uint <- 5UL", plain_uint(5UL), union_uint(5UL))
	compare("int <- 5", plain_int(5), union_int(5))
	compare("int <- 5L", plain_int(5L), union_int(5L))
	compare("int <- 5U", plain_int(5U), union_int(5U))
	compare("long <- 5", plain_long(5), union_long(5))
	compare("ulong <- 5", plain_ulong(5), union_ulong(5))
	compare("float <- 5", plain_float(5), union_float(5))
	compare("float <- 5L", plain_float(5L), union_float(5L))
	compare("float <- 5UL", plain_float(5UL), union_float(5UL))
