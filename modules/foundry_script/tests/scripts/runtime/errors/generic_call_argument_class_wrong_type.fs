# A substituted class destination is decided by the same class check a concrete parameter uses, so a
# gradual object of an unrelated class is refused at the call boundary rather than laundered into the
# callee's erased slot.
func identity[T](value: T) -> T:
	print("callee entered")
	return value


func untyped_resource() -> Variant:
	return Resource.new()


func test() -> void:
	print(identity[RegEx](untyped_resource()))
