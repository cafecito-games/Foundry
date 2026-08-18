# A tuple member flattened in from a generic trait names the TRAIT's parameters, so the arguments the
# implementer applied are substituted into the recorded shape: a forwarded parameter stays receiver-
# resolved, and a concrete application enforces the applied argument outright.
trait Slotted[V]:
	var slot: (int, V) = (0, null)


class Holder[U] extends RefCounted:
	uses Slotted[U]


class IntHolder extends RefCounted:
	uses Slotted[int]


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var holder := Holder[String].new()
	var holder_object: Object = holder
	holder_object.set("slot", supply((7, 8)))
	print(holder.slot)
	holder_object.set("slot", supply((7, "eight")))
	print(holder.slot)
	var int_holder := IntHolder.new()
	var int_object: Object = int_holder
	int_object.set("slot", supply((7, "eight")))
	print(int_holder.slot)
	int_object.set("slot", supply((7, 8)))
	print(int_holder.slot)
