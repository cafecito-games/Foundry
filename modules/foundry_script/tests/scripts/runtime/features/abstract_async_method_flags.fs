# `abstract async func` is allowed: the keyword is a superset of the old
# `@abstract async` annotation. The bodiless `abstract async` declaration only
# compiles because both modifiers take effect (a bodiless non-abstract function
# would be rejected), and it declares an async contract that a concrete override
# must honor (the sync-override rejection lives in
# analyzer/errors/abstract_async_method_implementation.gd). The async override is
# reported as async via reflection and can be awaited.
abstract class Loader:
	abstract async func load_value() -> String

class RemoteLoader extends Loader:
	async func load_value() -> String:
		return "loaded"

func test() -> void:
	var loader := RemoteLoader.new()
	for method in loader.get_method_list():
		if str(method.name) == "load_value":
			print(Utils.get_method_signature(method))
	print(await loader.load_value())
