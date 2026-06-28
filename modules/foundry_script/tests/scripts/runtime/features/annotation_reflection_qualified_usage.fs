# A fully qualified annotation usage (`@namespace.name`) persists the same passive metadata as a
# short-name usage: the descriptor exposes the bare short name and the full canonical identity, and
# both are matchable through has_annotation / get_annotation.
namespace cafecito.qualified_reflect

annotation suite(name: String = "") targets CLASS
annotation timeout(seconds: float) targets METHOD
annotation fixture targets VARIABLE

@cafecito.qualified_reflect.suite(name = "Qualified Suite")
class Probe:
	@cafecito.qualified_reflect.fixture
	var world: int = 0

	@cafecito.qualified_reflect.timeout(7.0)
	func scenario() -> void:
		pass

func test() -> void:
	var class_annotations := godot.reflection.get_class_annotations(Probe)
	print(class_annotations.size())
	print(class_annotations[0].name)
	print(class_annotations[0].qualified_name)
	print(class_annotations[0].kwargs["name"])

	var method_annotations := godot.reflection.get_method_annotations(Probe, "scenario")
	print(method_annotations.size())
	print(method_annotations[0].name)
	print(method_annotations[0].qualified_name)
	print(method_annotations[0].args[0])

	print(godot.reflection.get_variable_annotations(Probe, "world")[0].qualified_name)

	# A qualified usage is matchable by both its short name and its canonical identity.
	print(godot.reflection.has_annotation(Probe, "scenario", "timeout"))
	print(godot.reflection.has_annotation(Probe, "scenario", "cafecito.qualified_reflect.timeout"))
