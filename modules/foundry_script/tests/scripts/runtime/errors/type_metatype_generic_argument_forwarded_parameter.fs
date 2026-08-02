# A subclass that wraps its own parameter in a handle (`extends Slot[Type[U]]`) is not forwarding `U`:
# the leaf reifies `U` as an instance type, so the inherited `value: T` slot still has to validate
# class handles rather than instances of `U`.
class Slot[T]:
	var value: T


class Forwarded[U] extends Slot[Type[U]]:
	pass


func test() -> void:
	var forwarded := Forwarded[Node].new()
	forwarded.value = Node
	print(forwarded.value == Node)

	# Even through an untyped alias, the inherited slot rejects an instance of the represented type.
	var dynamic: Variant = forwarded
	dynamic.value = RefCounted.new()
	print(forwarded.value == Node)
