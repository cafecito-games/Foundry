# Applied arguments are checked against the declared bounds, including a bound that names a sibling
# parameter, and a decorated recursive spelling inside the declaration is an ordinary application.
enum Bounded[T: Resource, U: T]:
	Pair(first: T, second: U)

enum Slot[T]:
	Value(value: T)
	Nested(inner: Slot[T?])

func exact(pair: Bounded[Resource, Resource]) -> void:
	print(pair)

func derived(pair: Bounded[Resource, Texture2D]) -> void:
	print(pair)

func decorated(slot: Slot[int?]) -> void:
	print(slot)
