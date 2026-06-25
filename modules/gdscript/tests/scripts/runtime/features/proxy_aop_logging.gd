# Spring-style AOP via the pure proxy core: a handler logs every call and then
# "proceeds" by forwarding to a real target. No special engine support beyond
# create_proxy_dynamic -- interception is an ordinary handler that wraps a call.
@abstract class UserService:
	@abstract func get_name(id: int) -> String
	@abstract func get_age(id: int) -> int

class RealUserService extends UserService:
	func get_name(id: int) -> String:
		return "user-" + str(id)
	func get_age(id: int) -> int:
		return id * 2

func test() -> void:
	var real := RealUserService.new()
	var call_log: Array = []

	var logged := create_proxy_dynamic(UserService, func(method_name: StringName, args: Array) -> Variant:
		call_log.append(str(method_name))
		return real.callv(method_name, args)) as UserService

	print(logged.get_name(7))
	print(logged.get_age(7))
	print(call_log)
