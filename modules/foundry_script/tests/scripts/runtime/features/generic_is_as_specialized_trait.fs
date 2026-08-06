# `is`/`as` against a generic trait observe the type arguments the implementer conformed with, using
# the same invariant relation a generic class target uses. A raw trait target still asks only the
# nominal conformance question; a specialized trait target additionally requires the implementer's
# effective arguments for that trait to be known and invariantly equal, so an implementer that
# conformed with a different argument, or with none at all, fails.
trait Holder[T]:
	var item: T


trait Relayed[U]:
	uses Holder[U]


class Box[V]:
	var value: V


class IntHolder:
	uses Holder[int]


class StringHolder:
	uses Holder[String]


class BoxHolder:
	uses Holder[Box[int]]


class RelayHolder:
	uses Relayed[int]


class Forwarder[W]:
	uses Holder[W]


class DerivedIntHolder extends IntHolder:
	var extra: int = 0


class Plain:
	var value: int = 0


func test() -> void:
	var int_holder: Variant = IntHolder.new()
	var string_holder: Variant = StringHolder.new()
	var box_holder: Variant = BoxHolder.new()
	var relay_holder: Variant = RelayHolder.new()
	var forwarded_int: Variant = Forwarder[int].new()
	var raw_forwarder: Variant = Forwarder.new()
	var derived_holder: Variant = DerivedIntHolder.new()
	var plain: Variant = Plain.new()
	var absent: Variant = null

	print("int holder is Holder: ", int_holder is Holder)
	print("string holder is Holder: ", string_holder is Holder)
	print("raw forwarder is Holder: ", raw_forwarder is Holder)
	print("plain is Holder: ", plain is Holder)

	print("int holder is Holder[int]: ", int_holder is Holder[int])
	print("int holder is Holder[String]: ", int_holder is Holder[String])
	print("string holder is Holder[int]: ", string_holder is Holder[int])
	print("string holder is Holder[String]: ", string_holder is Holder[String])
	print("box holder is Holder[Box[int]]: ", box_holder is Holder[Box[int]])
	print("box holder is Holder[Box[String]]: ", box_holder is Holder[Box[String]])

	print("forwarded int is Holder[int]: ", forwarded_int is Holder[int])
	print("forwarded int is Holder[String]: ", forwarded_int is Holder[String])
	print("raw forwarder is Holder[int]: ", raw_forwarder is Holder[int])

	print("relay holder is Relayed[int]: ", relay_holder is Relayed[int])
	print("relay holder is Relayed[String]: ", relay_holder is Relayed[String])
	print("relay holder is Holder[int]: ", relay_holder is Holder[int])
	print("relay holder is Holder[String]: ", relay_holder is Holder[String])

	print("derived holder is Holder: ", derived_holder is Holder)
	print("derived holder is Holder[int]: ", derived_holder is Holder[int])
	print("derived holder is Holder[String]: ", derived_holder is Holder[String])

	print("plain is Holder[int]: ", plain is Holder[int])
	print("null is Holder[int]: ", absent is Holder[int])

	var matched_cast: Variant = int_holder as Holder[int]
	print("matched cast keeps identity: ", matched_cast == int_holder)
	var mismatched_cast: Variant = int_holder as Holder[String]
	print("mismatched cast is null: ", mismatched_cast == null)
	var raw_target_cast: Variant = int_holder as Holder
	print("raw target cast keeps identity: ", raw_target_cast == int_holder)
	var raw_value_cast: Variant = raw_forwarder as Holder[int]
	print("raw value cast is null: ", raw_value_cast == null)
	var null_cast: Variant = absent as Holder[int]
	print("null cast is null: ", null_cast == null)

	print("is/as specialized trait ok")
