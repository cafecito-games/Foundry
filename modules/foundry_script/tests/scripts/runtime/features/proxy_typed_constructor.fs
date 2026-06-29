# create_proxy[T](handler) -- the typed proxy constructor. The analyzer types the
# result as T, so contract methods are callable directly off the typed value with
# no `as` cast, and the runtime receives T's descriptor to scan the contract.
trait Greeter:
	abstract func greet(name: String) -> String

class Recorder:
	var calls: Array = []

	func handle(method_name: StringName, args: Array) -> Variant:
		calls.append([str(method_name), args])
		return "hi " + str(args[0])

func test() -> void:
	var recorder := Recorder.new()
	var greeter: Greeter = create_proxy[Greeter](recorder.handle)

	# The static type is `Greeter`, so this call type-checks with no cast.
	print(greeter.greet("world"))
	print(recorder.calls.size())
	print(recorder.calls[0][0])
