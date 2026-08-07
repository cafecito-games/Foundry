# The qualified spelling of the same construction, kept beside the shorthand form so the two stay
# provably at parity when a constant collection is passed into a typed collection field.
enum Box[T]:
	Wrap(value: T)


const NUMBERS = [1, 2, 3]


func test():
	var qualified: Box[Array[int]] = Box[Array[int]].Wrap(NUMBERS)
	print(qualified)
