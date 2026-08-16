# Reification is all-or-nothing per construction: a reified argument vector has no representation for
# "this argument is unknown", so recording a blank would read back as the definite evidence `Variant`.
# A receiver that supplies no argument therefore builds an unspecialized instance, which is honest "no
# evidence" -- and the destination projects the same parameter to unknown, so the store still passes.
class Holder[T]:
	var value: T


class Wrapper[U]:
	var holder: Holder[U] = Holder[U].new()


func test() -> void:
	var wrapper := Wrapper.new()
	var built: Variant = wrapper.holder
	print(built is Holder)
	print(built is Holder[int])
	# Unspecialized, so the constructed holder's own slot is unconstrained rather than pinned to the
	# `Object` the erased parameter would have described.
	built.value = 5
	print(built.value)

	var dynamic: Variant = wrapper
	dynamic.holder = Holder[String].new()
	print(wrapper.holder != null)
	print("unspecialized receiver ok")
