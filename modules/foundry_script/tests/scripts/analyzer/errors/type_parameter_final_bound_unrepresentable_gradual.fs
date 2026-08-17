# A parameter bounded by a `final` class is decidable only where compiled lowering can still state the
# bound it resolves to. A callable signature erases completely at run time, a typed container cannot
# say "or null", and a tuple nested under a typed container keeps no element shape at all. None of
# those positions checks the bound, so a gradual source into one is refused instead of laundered: an
# allowance that claims a runtime check must name one the runtime actually performs.
final class FbuGauge:
	func read() -> String:
		return "gauge"


func callable_signature[T: FbuGauge](value: Variant) -> void:
	var kept: Callable[[T], void] = value
	print(kept)


func nullable_under_container[T: FbuGauge](value: Variant) -> void:
	var kept: Array[T?] = value
	print(kept)


func tuple_under_container[T: FbuGauge](value: Variant) -> void:
	var kept: Array[(int, T)] = value
	print(kept)


func dictionary_of_tuples[T: FbuGauge](value: Variant) -> void:
	var kept: Dictionary[String, (int, T)] = value
	print(kept)


func test() -> void:
	callable_signature[FbuGauge](1)
	nullable_under_container[FbuGauge](1)
	tuple_under_container[FbuGauge](1)
	dictionary_of_tuples[FbuGauge](1)
