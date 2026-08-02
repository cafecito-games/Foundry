# A generic trait's type argument is reified onto every implementer, so writes to a flattened trait
# member are validated against that implementer's own argument. Well-typed values are accepted,
# including through a nested trait, a specialized generic argument, and a forwarded class parameter.
trait Slotted[T]:
	var slot: T


trait Relayed[U]:
	uses Slotted[U]


class Box[V]:
	var value: V


class IntHolder:
	uses Slotted[int]


class StringHolder:
	uses Slotted[String]


class BoxHolder:
	uses Slotted[Box[int]]


class RelayHolder:
	uses Relayed[int]


class Forwarder[W]:
	uses Slotted[W]


func test() -> void:
	var int_holder := IntHolder.new()
	var dynamic_int: Variant = int_holder
	dynamic_int.slot = 1
	print(int_holder.slot)

	var string_holder := StringHolder.new()
	var dynamic_string: Variant = string_holder
	dynamic_string.slot = "hello"
	print(string_holder.slot)

	var box_holder := BoxHolder.new()
	var dynamic_box: Variant = box_holder
	dynamic_box.slot = Box[int].new()
	print(box_holder.slot != null)

	var relay_holder := RelayHolder.new()
	var dynamic_relay: Variant = relay_holder
	dynamic_relay.slot = 7
	print(relay_holder.slot)

	var forwarder := Forwarder[int].new()
	var dynamic_forwarder: Variant = forwarder
	dynamic_forwarder.slot = 9
	print(forwarder.slot)
