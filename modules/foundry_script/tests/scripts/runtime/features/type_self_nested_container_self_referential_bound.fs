# A type parameter whose bound names the class that declares it is legal, so `Self` substitution has
# to terminate on that edge instead of chasing the bound back into the declaration. Nested `Self`
# containers on such a class analyze and reify against the leaf like any other class.
class Recursive[T: Recursive]:
	func grid() -> Array[Array[Self]]:
		return [[self]]


class Leaf extends Recursive[Leaf]:
	pass


func test() -> void:
	var leaf := Leaf.new()
	var grid := leaf.grid()
	print("inner array is Leaf typed: %s" % [grid[0].get_typed_script() == Leaf])
	print("nested element is Leaf: %s" % [grid[0][0] is Leaf])
