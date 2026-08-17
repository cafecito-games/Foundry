# A tuple parameter carries its declared shape to run time, so the callee-side boundary enforces the
# same thing a store does: matching arity, per-element type test, null accepted only where the
# declaration is nullable. Nothing is converted, and the accepted value is normalized to the canonical
# read-only, untyped carrier a tuple value has -- a parameter performs no store of its own, so without
# that the body would hold whatever Array the caller still owns.
tuple Vec2(x: float, y: float)


trait Marker:
	func marked() -> bool:
		return true


class Marked extends RefCounted:
	uses Marker


class Receiver extends RefCounted:
	func take(pair: (int, String)) -> void:
		print("took ", pair)

	func carrier(pair: (int, String)) -> void:
		# Reaching a tuple's carrier takes a deliberate unsafe cast, since a tuple type is not an Array
		# type to the analyzer.
		var value: Variant = pair
		var carrier_array := value as Array
		print("read_only=", carrier_array.is_read_only(), " typed=", carrier_array.is_typed())

	func nested(pair: (int, (String, bool))) -> void:
		print("nested ", pair)

	func maybe(pair: (int, String)?) -> void:
		print("maybe ", pair)

	func nullable_element(pair: (int, String?)) -> void:
		print("nullable element ", pair)

	func named(point: Vec2) -> void:
		print("named ", point)

	func trait_element(pair: (int, Marker)) -> void:
		print("trait element ", pair.1.marked())

	func container_element(pair: (int, Array[int])) -> void:
		print("container element ", pair)

	func generic[T](pair: (int, T)) -> void:
		print("generic ", pair)

	func rest(...values: Array[(int, String)]) -> void:
		print("rest ", values)

	static func static_take(pair: (int, String)) -> void:
		print("static took ", pair)


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var receiver := Receiver.new()

	# Every call kind binds the parameter through the same boundary, and a matching value passes.
	var callback: Callable = receiver.take
	callback.call((1, "one"))
	var static_callback: Callable = Receiver.static_take
	static_callback.call((2, "two"))
	var lambda := func(pair: (int, String)) -> void:
		print("lambda took ", pair)
	var lambda_callback: Callable = lambda
	lambda_callback.call((3, "three"))
	receiver.take(supply((4, "four")))

	# The carrier the body observes is canonical whatever the caller supplied: a mutable untyped Array
	# and a typed Array of matching shape are both normalized rather than stored as handed over.
	receiver.carrier(supply((5, "five")))
	receiver.carrier(supply([6, "six"]))
	receiver.carrier(supply(Array([7, "seven"], TYPE_NIL, "", null)))

	# Every element form accepts the value its declaration describes.
	receiver.nested(supply((8, ("eight", true))))
	receiver.maybe(supply(null))
	receiver.maybe(supply((9, "nine")))
	receiver.nullable_element(supply((10, null)))
	receiver.nullable_element(supply((11, "eleven")))
	receiver.named(supply(Vec2(1.5, 2.5)))
	receiver.named(supply((3.5, 4.5)))
	receiver.trait_element(supply((12, Marked.new())))

	# A container element has to arrive already typed, exactly as a store demands.
	var typed: Array[int] = [1, 2]
	receiver.container_element(supply((13, typed)))

	# A method type parameter is erased in the compiled shape, so the second element takes anything
	# while the declared arity and the concrete `int` still hold.
	receiver.generic[String](supply((14, "fourteen")))
	receiver.generic[bool](supply((15, true)))

	# A rest tail of tuples collects into an untyped array whose elements are each validated.
	receiver.rest((16, "sixteen"), (17, "seventeen"))
	receiver.rest()

	# A named tuple construction types its literal fields from the declaration, so a container field
	# built inline is already typed and is unaffected by the rule above.
	print(Vec2(0.5, 1.5))
