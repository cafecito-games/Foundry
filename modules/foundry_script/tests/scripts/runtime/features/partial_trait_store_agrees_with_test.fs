# Wherever the static relation accepts a partially known trait argument, the runtime relation must
# agree. Each row prints the statically typed store, the `is` test on the same value, and the
# `Variant`-routed store, which is the path the runtime decides on its own.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class Pair[A, B]:
	pass


class Partial[U]:
	uses Keeper[Pair[int, U]]


func test() -> void:
	var agreeing := Partial[float].new()
	var agreeing_slot: Keeper[Pair[int, float]] = agreeing
	print("agreeing store: ", agreeing_slot != null)
	print("agreeing test: ", agreeing is Keeper[Pair[int, float]])
	var agreeing_boxed: Variant = Partial[float].new()
	var agreeing_boxed_slot: Keeper[Pair[int, float]] = agreeing_boxed
	print("agreeing boxed store: ", agreeing_boxed_slot != null)

	var raw := Partial.new()
	var raw_slot: Keeper[Pair[int, float]] = raw
	print("raw store: ", raw_slot != null)
	print("raw test: ", raw is Keeper[Pair[int, float]])
	var raw_boxed: Variant = Partial.new()
	print("raw boxed test: ", raw_boxed is Keeper[Pair[int, float]])

	# A raw conformer fixes the first component and leaves the second open, so a destination that
	# differs only in the open component is accepted statically while the run time still answers the
	# test from the argument the value actually carries.
	var open_component := Partial.new()
	var open_component_slot: Keeper[Pair[int, String]] = open_component
	print("open component store: ", open_component_slot != null)
	print("open component test: ", open_component is Keeper[Pair[int, String]])
	var open_component_boxed: Variant = Partial.new()
	var open_component_boxed_slot: Keeper[Pair[int, String]] = open_component_boxed
	print("open component boxed store: ", open_component_boxed_slot != null)
