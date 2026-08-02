# A trait member reached through a second trait keeps the argument the implementer supplied, and two
# implementers of the same trait with different arguments do not share a binding: writing a `String`
# into `RelayHolder`'s `int` slot fails even though `StringHolder` accepts strings.
trait Slotted[T]:
	var slot: T


trait Relayed[U]:
	uses Slotted[U]


class StringHolder:
	uses Slotted[String]


class RelayHolder:
	uses Relayed[int]


func test() -> void:
	var string_holder := StringHolder.new()
	var dynamic_string: Variant = string_holder
	dynamic_string.slot = "fine"
	print(string_holder.slot)

	var relay_holder := RelayHolder.new()
	var dynamic_relay: Variant = relay_holder
	dynamic_relay.slot = "not an int"
	print(relay_holder.slot)
