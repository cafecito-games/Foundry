# Laundering a type parameter value through `Variant` before passing it to a `T?` parameter stays a
# dynamic call: it is still accepted, and it still reports UNSAFE_CALL_ARGUMENT. Only the direct
# `T` -> `T?` form is statically safe.
class Holder[T]:
	func accepts_nullable(value: T?) -> void:
		print(value)

	func bridge_through_variant(value: T) -> void:
		var bridged: Variant = value
		accepts_nullable(bridged)

	func pass_directly(value: T) -> void:
		accepts_nullable(value) # No warning.


func test():
	var container: Holder[int] = Holder.new()
	container.bridge_through_variant(1)
	container.pass_directly(2)
