# A delegating proxy advises a subset of methods and forwards everything else --
# the remaining methods and all property access -- to a real target object.
abstract class Service:
	var label: String
	abstract func greet(subject: String) -> String
	abstract func add(a: int, b: int) -> int

class RealService extends Service:
	func greet(subject: String) -> String:
		return "hello " + subject
	func add(a: int, b: int) -> int:
		return a + b

func test() -> void:
	var real := RealService.new()
	real.label = "real-label"

	var calls: Array = []
	var interceptor := {
		"greet": (func(method_name: StringName, args: Array, target: Object) -> Variant:
			calls.append(str(method_name))
			return "wrapped:" + str(target.callv(method_name, args))),
	}

	var service := godot.reflection.create_delegating_proxy(Service, real, interceptor) as Service

	# Advised method: the advice runs and "proceeds" to the target.
	print(service.greet("world"))
	# Unadvised method: forwarded straight to the target.
	print(service.add(2, 3))
	# Property read forwards to the target.
	print(service.label)
	# Property write forwards to the target.
	service.label = "changed"
	print(real.label)
	# Only the advised method reached the interceptor.
	print(calls)
