# A static variable has one storage slot per declaring class, and specializing a generic class does
# not produce a distinct runtime class. A slot typed by a class type parameter would therefore be
# seen as `int` through `Box[int]` and as `String` through `Box[String]` while holding a single
# value, so sibling specializations would silently overwrite each other's statically typed storage.
# The declaration is rejected outright: directly, nested inside a container element type, and nested
# inside a type argument.
class Box[T]:
	static var value: T


class Bag[T]:
	static var items: Array[T]


class Handles[T]:
	static var handle: Type[T]


class Nested[T]:
	static var boxes: Array[Box[T]]


class Callbacks[T]:
	static var handler: Callable[[T], void]
	static var maker: Callable[[], T]
	static var variadic: Callable[[int, ...Array[T]], void]


func test() -> void:
	print(Box, Bag, Handles, Nested, Callbacks)
