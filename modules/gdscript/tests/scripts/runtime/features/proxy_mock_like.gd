# A mock-like fixture: the dynamic-proxy primitive supports record/stub/verify
# end to end, with no mock library. The handler records each call and returns a
# stubbed value; verification inspects the recorded calls afterwards.
trait Repository:
	@abstract func find(id: int) -> String
	@abstract func save(value: String) -> bool

class Mock:
	var calls: Array = []
	var stubs: Dictionary = {}

	func handle(method_name: StringName, args: Array) -> Variant:
		calls.append([str(method_name), args])
		return stubs.get(method_name, null)

	func build() -> Object:
		return create_proxy_dynamic(Repository, handle)

	func was_called(method_name: StringName) -> bool:
		for entry in calls:
			if entry[0] == str(method_name):
				return true
		return false

func test() -> void:
	var mock := Mock.new()
	mock.stubs["find"] = "stubbed-name"
	mock.stubs["save"] = true

	var repository := mock.build() as Repository

	# Calls return the stubbed values...
	print(repository.find(42))
	print(repository.save("hello"))

	# ...and were recorded for verification.
	print(mock.was_called("find"))
	print(mock.was_called("save"))
	print(mock.was_called("delete"))
	print(mock.calls.size())
	# The recorded arguments are available for verification too.
	print(mock.calls[0][1])
