# A reflective write validates against the same shape. `Object.set()` reports failure by returning
# without writing, so the member keeps its previous value rather than a corrupted one, and it prints
# the same sentence an in-body tuple store prints for the value it rejected.
class Holder extends RefCounted:
	var field: (int, String) = (0, "zero")


func test() -> void:
	var holder := Holder.new()
	var object: Object = holder
	object.set("field", (1, "one"))
	print(holder.field)
	object.set("field", (1, 2, 3))
	print(holder.field)
	object.set("field", (1, RefCounted.new()))
	print(holder.field)
