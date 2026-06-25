# A type argument that satisfies its bound resolves, and inside the generic body a
# bounded type-parameter value exposes the members of its bound for static checking.
class Box[T: RefCounted]:
	var value: T

	func count() -> int:
		# `value` is typed `T`, bounded by `RefCounted`, so `RefCounted` members resolve.
		var refs := value.get_reference_count()
		return refs


# `RefCounted` satisfies the bound exactly; `Resource` satisfies it by derivation.
var exact: Box[RefCounted]
var derived: Box[Resource]


func test():
	print(exact)
	print(derived)
	print("generic class bounds ok")
