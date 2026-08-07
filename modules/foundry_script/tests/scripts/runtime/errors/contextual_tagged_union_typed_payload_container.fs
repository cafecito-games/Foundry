# A typed collection field is typed by the construction that builds the case, and a constant
# collection carries no container typing of its own. Neither spelling may bake the constant in place
# of that construction, so both reach the same runtime conversion failure.
enum Box[T]:
	Wrap(value: T)


const NUMBERS = [1, 2, 3]


func test():
	var shorthand: Box[Array[int]] = .Wrap(NUMBERS)
	print(shorthand)
