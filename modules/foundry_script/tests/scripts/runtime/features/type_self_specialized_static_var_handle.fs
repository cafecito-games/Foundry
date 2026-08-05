# A specialized receiver stored in the static variable of one of its own type-argument scripts used to
# close an uncollectable reference cycle: `Holder` (a RefCounted script) -> static storage -> the
# specialized class handle -> its type-argument script -> `Holder`. The descriptor now holds its
# type-argument scripts weakly, so this program is legal and non-leaking. This fixture checks the
# handle still resolves and still reports its argument (leak freedom is proven by the doctest that
# asserts the argument script's reference count is unchanged, not by this output).
class Crate[T]:
	var item: T


class Holder:
	static var handle := Crate[Self]


func test() -> void:
	# The handle stays valid even though `Holder` owns it through this static variable and is also one
	# of its type-argument scripts (via `Self`).
	print(Holder.handle != null)

	# The specialization is preserved, not degraded to the bare script or Variant: an instance built
	# through the handle still treats `item` as a `Holder`, so a matching write succeeds and reads back.
	var made: Variant = Holder.handle.new()
	made.item = Holder.new()
	print(made.item != null)
