# Supplies frw_gadget() for FrwWidget from a separate file, and specializes a helper generic
# class's explicit type argument with an alias declared here rather than in the target's file.
type Meters = float


class WitnessBox[T]:
	var value: T


extend FrwWidget uses FrwGadgetlike:
	func frw_gadget() -> String:
		var box := WitnessBox[Meters].new()
		box.value = 1.5
		return "gadget:" + str(box.value)
