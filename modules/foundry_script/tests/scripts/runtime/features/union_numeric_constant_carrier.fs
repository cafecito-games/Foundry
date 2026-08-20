# A numeric constant reaching a union is carried on the alternative that admitted it, so the `uint`
# alternative of `uint | String` holds a `uint` and narrows as one -- exactly what a plain `uint` slot
# holding the same literal does. The `uint` -> `long` widening every value survives crosses a union
# alternative too, with no constant needed to prove it.
const BAKED: uint | String = 9


func carrier_of(v) -> String:
	if v is uint:
		return "uint " + str(v)
	if v is int:
		return "int " + str(v)
	return "other"


func take(v: uint | String) -> String:
	if v is uint:
		return "uint " + str(v)
	if v is String:
		return "String " + v
	return "none"


func widen(v: long | String) -> String:
	if v is long:
		return "long " + str(v)
	return "other"


func plain(v: uint) -> String:
	if v is uint:
		return "uint " + str(v)
	return "none"


func test():
	print(plain(5))
	print(take(5))
	print(take("five"))

	# A declared union slot carries the literal the same way a parameter does.
	var stored: uint | String = 7
	print(stored is uint)
	print(take(stored))

	# A constant folded into a `const` is baked once and no store ever revisits it, so its carrier has
	# to be right at compile time.
	print(carrier_of(BAKED))

	# The one carrier crossing that needs no constant: every `uint` value is a `long`.
	var counted: uint = 4000000000U
	print(widen(counted))
	print(widen(5))
