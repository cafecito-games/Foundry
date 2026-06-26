# Reflection over custom annotation argument binding: positional values land in args,
# named values land in kwargs, omitted optional parameters keep their defaults out of
# both collections, variadic parameters accept zero or many extra positional arguments,
# constant-expression arguments are reduced to their resolved values, and static methods
# and static member variables carry annotation metadata like their instance counterparts.
namespace cafecito.binding_demo

annotation skip(reason: String = "") targets METHOD
annotation config(first: int, second: int = 7, label: String = "") targets METHOD
annotation tags(...names: String) targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation priority(value: int) targets METHOD
annotation test targets METHOD
annotation fixture targets VARIABLE

const BASE_PRIORITY := 5

class Subject:
	@fixture
	static var shared_state: int = 0

	# Marker usage of a declaration whose only parameter has a default: the default is
	# validated but must not be injected into args or kwargs.
	@skip
	func skipped_without_reason() -> void:
		pass

	@skip("flaky")
	func skipped_with_reason() -> void:
		pass

	# Positional first, named label; the defaulted "second" parameter stays out of both
	# collections.
	@config(1, label = "x")
	func partially_bound() -> void:
		pass

	# Variadic with zero extra positional arguments.
	@tags()
	func no_tags() -> void:
		pass

	# Variadic with several extra positional arguments.
	@tags("integration", "slow")
	func many_tags() -> void:
		pass

	# Constant-expression arguments are reduced to their resolved runtime values.
	@timeout(1.0 + 2.0)
	@priority(BASE_PRIORITY * 2)
	static func computed() -> void:
		pass


func test() -> void:
	# Default of the omitted-only parameter is not injected.
	var skip_marker = godot.reflection.get_annotation(Subject, "skipped_without_reason", "skip")
	print(skip_marker.args.size())
	print(skip_marker.kwargs.size())

	# Explicit positional argument is preserved.
	var skip_reason = godot.reflection.get_annotation(Subject, "skipped_with_reason", "skip")
	print(skip_reason.args.size())
	print(skip_reason.args[0])

	# Positional in args, named in kwargs, defaulted parameter omitted from both.
	var config = godot.reflection.get_annotation(Subject, "partially_bound", "config")
	print(config.args.size())
	print(config.args[0])
	print(config.kwargs.size())
	print(config.kwargs["label"])
	print(config.kwargs.has("second"))

	# Variadic accepts zero extra positional arguments.
	print(godot.reflection.get_annotation(Subject, "no_tags", "tags").args.size())

	# Variadic accepts many extra positional arguments, preserved in order.
	var many = godot.reflection.get_annotation(Subject, "many_tags", "tags")
	print(many.args.size())
	print(many.args[0])
	print(many.args[1])

	# Constant-expression arguments are reduced to resolved values on a static method.
	print(godot.reflection.get_annotation(Subject, "computed", "timeout").args[0])
	print(godot.reflection.get_annotation(Subject, "computed", "priority").args[0])

	# Static member variables carry annotation metadata.
	var static_variable = godot.reflection.get_variable_annotations(Subject, "shared_state")
	print(static_variable.size())
	print(static_variable[0].name)

	# get_methods() (plural) descriptors embed the same annotation objects.
	for method in godot.reflection.get_methods(Subject):
		if str(method["name"]) == "computed":
			var method_annotations: Array = method["annotations"]
			print(method_annotations.size())
			print(method_annotations[0].name)
			print(method_annotations[1].name)
