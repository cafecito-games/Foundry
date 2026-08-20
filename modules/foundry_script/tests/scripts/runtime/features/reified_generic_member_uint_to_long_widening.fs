# The design-6.1 `uint` -> `long` widening reaches a reified generic member on every dynamic write
# path: an external `set()` through a `Variant` receiver, a direct in-body member store, and a
# `T`-typed local store all re-carrier an in-`uint`-range value onto the `long` carrier, so
# `Box[long]` accepts exactly what a plain `var value: long` accepts. The reified width still
# decides what fits, and an instance with no reified argument keeps an untyped slot.
class Box[T]:
	var value: T

	func put(v) -> void:
		value = v

	func store_local(v) -> Variant:
		var local: T = v
		return local


func test() -> void:
	var wide: uint = 4000000000
	var small: uint = 5

	var long_box := Box[long].new()
	var dynamic: Variant = long_box
	dynamic.value = wide
	var observed: Variant = long_box.value
	print(observed, " ", observed is long, " ", observed is uint)

	long_box.put(small)
	observed = long_box.value
	print(observed, " ", observed is long)

	var local_stored: Variant = long_box.store_local(wide)
	print(local_stored, " ", local_stored is long)

	# The declared width is asked about the widened value, so a small `uint` lands in a `Box[int]`.
	var int_box := Box[int].new()
	var dynamic_int: Variant = int_box
	dynamic_int.value = small
	observed = int_box.value
	print(observed, " ", observed is int)

	# An unspecialized instance has no evidence to widen against, so the slot stays untyped.
	var raw := Box.new()
	raw.put(wide)
	observed = raw.value
	print(observed, " ", observed is uint)

	print("reified generic member uint to long widening ok")
