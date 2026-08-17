# Absent evidence is accepted at a specialized store, but evidence that contradicts the declaration
# is not: a `Box[String]` never satisfies a `Box[int]` declaration, and a `Keeper[String]` never
# satisfies a `Keeper[int]` one. Each store lives in its own function because a rejected store aborts
# the function that performed it.
class Box[T]:
	var value: T


trait Keeper[T]:
	func label() -> String:
		return "keeper"


class ForwardingKeeper[U]:
	uses Keeper[U]


func store_class(source: Variant) -> void:
	var slot: Box[int] = source
	print("class store accepted: ", slot != null)


func store_trait(source: Variant) -> void:
	var slot: Keeper[int] = source
	print("trait store accepted: ", slot != null)


func test() -> void:
	store_class(Box[String].new())
	store_trait(ForwardingKeeper[String].new())
	print("both conflicting stores rejected")
