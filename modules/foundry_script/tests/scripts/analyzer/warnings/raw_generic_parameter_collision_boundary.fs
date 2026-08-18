# A type parameter is identified by its name, scope, and ordinal, and generic declarations
# conventionally name their first parameter `T`. A value handed over by a raw `Box` is still undecided
# no matter what the receiving declaration calls its own parameter, so the crossing is reported the
# same way whether the names collide or not.
class Box[T]:
	var stored: T

	func get_value() -> T:
		return stored

	func set_value(value: T) -> void:
		stored = value


class HolderSame[T]:
	func consume(value: T) -> void:
		print(value)

	# We don't want to execute it because of errors, just analyze.
	func no_exec_forward(box: Box) -> void:
		consume(box.get_value())


class HolderOther[V]:
	func consume(value: V) -> void:
		print(value)

	func no_exec_forward(box: Box) -> void:
		consume(box.get_value())


func no_exec_same_parameter(box: Box) -> void:
	box.set_value(box.get_value()) # No warning: the slot names the very parameter the value carries.


func test():
	pass
