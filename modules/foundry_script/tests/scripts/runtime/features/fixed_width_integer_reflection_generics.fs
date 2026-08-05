# Reflection names a reified generic argument, a class handle, and a nested typed container by
# walking the stored descriptor. The declarations here are self-referential (`LinkedBox[T]` holds a
# `LinkedBox[T]`, and a `Shape` payload holds a `Shape`), so naming them recurses only through the
# written type expression and terminates instead of following the declaration back onto itself.
#
# A tagged union erases to a read-only Array before execution, so its member names that carrier.
class LinkedBox[T]:
	var payload: T
	var next: LinkedBox[T]


enum Shape:
	Leaf(value: ulong)
	Branch(left: Shape, right: Shape)


var boxed: LinkedBox[ulong]
var narrow_boxed: LinkedBox[uint]
var nested: Array[Dictionary[uint, Array[long]]] = []
var shape: Shape = Shape.Leaf(1UL)
var handle: Type[LinkedBox[uint]] = LinkedBox[uint]


func test() -> void:
	for property in foundry.reflection.get_property_descriptors(self):
		print(property.name, " ", property.type_name)
