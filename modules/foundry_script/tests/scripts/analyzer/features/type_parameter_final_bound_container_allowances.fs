# A parameter bounded by a `final` class denotes exactly that class -- no subtype of it can exist --
# so a value the bound accepts is a value every possible type argument accepts. That argument does
# not weaken with nesting, so a final-bounded parameter stays satisfiable at every depth a bare one
# is: as an array element, in either dictionary slot, and inside mixed nesting.
final class Worker:
	func label() -> String:
		return "worker"


func bare[W: Worker]() -> W:
	return Worker.new()


func array_elements[W: Worker]() -> Array[W]:
	var built: Array[W] = [Worker.new()]
	return built


func dictionary_values[W: Worker]() -> Dictionary[String, W]:
	var built: Dictionary[String, W] = { "first": Worker.new() }
	return built


func dictionary_keys[W: Worker]() -> Dictionary[W, String]:
	var built: Dictionary[W, String] = {}
	return built


func mixed_nesting[W: Worker]() -> Array[Dictionary[String, Array[W]]]:
	var built: Array[Dictionary[String, Array[W]]] = [{ "crew": [Worker.new()] }]
	return built


func test():
	print(bare[Worker]().label())
	print(array_elements[Worker]().size())
	print(dictionary_values[Worker]().size())
	print(dictionary_keys[Worker]().size())
	print(mixed_nesting[Worker]().size())
