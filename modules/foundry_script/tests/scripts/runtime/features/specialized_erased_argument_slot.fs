# A slot spelled inside a generic body carries an *erased* type argument: `Box[T]` lowers `T` to an
# unconstrained node that is indistinguishable at run time from a slot naming `Variant`. Reading that as
# a demand for exactly `Variant` would reject the concrete `Box[int]` a correctly specialized receiver
# holds, so an argument short of completely concrete leaves the slot unenforced instead — the same rule
# the compiler already applies to a container element, where `Array[T]` erases whole rather than
# becoming `Array[Variant]`.
#
# The member store is unaffected because it resolves the parameter against the receiver first, so it
# keeps rejecting a mismatched argument with the resolved specialization named in the diagnostic.
class Box[A]:
	pass


class Wrapper[T]:
	var boxed: Box[T]

	func take(_value: Box[T]) -> String:
		return "took"

	func give(value: Variant) -> Box[T]:
		return value


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var wrapper: Object = Wrapper[int].new()
	wrapper.set("boxed", supply(Box[int].new()))
	print("member with matching argument: ", wrapper.get("boxed") != null)
	wrapper.set("boxed", null)
	wrapper.set("boxed", supply(Box[String].new()))
	print("member with mismatched argument: ", wrapper.get("boxed") != null)

	var raw: Object = Wrapper.new()
	raw.set("boxed", supply(Box[int].new()))
	print("raw receiver keeps the slot gradual: ", raw.get("boxed") != null)

	var specialized := Wrapper[int].new()
	var take: Callable = Callable(specialized, "take")
	print("erased parameter at the call boundary: ", take.call(Box[int].new()))
	print("erased return type: ", specialized.give(supply(Box[int].new())) != null)
	print("erased argument slot ok")
