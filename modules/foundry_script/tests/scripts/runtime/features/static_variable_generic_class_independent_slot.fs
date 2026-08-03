# Rejecting a static variable typed by a class type parameter must not disturb the static storage
# model itself. A generic class may still declare static variables whose types are independent of its
# parameters, and those keep the ordinary one-slot-per-declaring-class semantics: every subclass and
# every specialization observes the same slot, whether it specializes the base or not. A concrete
# argument supplied to a generic trait (`uses Slotted[int]`) is likewise still allowed, because the
# trait member is flattened into a slot the implementer owns.
trait Slotted[T]:
	static var slot: T


class Counter[T]:
	static var count := 0
	static var label: String = "counter"

	var payload: T

	static func bump() -> int:
		count += 1
		return count


class IntCounter extends Counter[int]:
	pass


class StringCounter extends Counter[String]:
	pass


class DeepIntCounter extends IntCounter:
	pass


class IntSlot:
	uses Slotted[int]


class StringSlot:
	uses Slotted[String]


func test() -> void:
	# A parameter-independent static is one shared slot for the whole hierarchy, including code
	# compiled inside the declaring generic class itself.
	IntCounter.count = 5
	print(Counter.count, " ", StringCounter.count, " ", DeepIntCounter.count)
	print(IntCounter.bump(), " ", IntCounter.count)
	StringCounter.label = "shared"
	print(Counter.label, " ", DeepIntCounter.label)

	# Instances of two specializations still type their own members independently.
	var ints := IntCounter.new()
	ints.payload = 7
	var strings := StringCounter.new()
	strings.payload = "seven"
	print(ints.payload, " ", strings.payload)

	# Trait static variables fixed to a concrete argument are flattened per implementer, so two implementers
	# holding different argument types never share storage.
	IntSlot.slot = 1
	StringSlot.slot = "one"
	print(IntSlot.slot, " ", StringSlot.slot)
