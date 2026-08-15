# A bare type parameter cannot be a union member, and an alias standing in for one does not
# launder that: a union of erased parameters has no static meaning.
class Box[T]:
	type Aliased = T

	var mixed: Aliased | int = 0


func test():
	print(Box.new().mixed)
