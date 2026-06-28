# godot.reflection annotation accessors take an `effective` switch: the default true keeps the
# inherited/trait-flattened view, while false restricts results to annotations declared on the
# exact target script. Class annotations stay direct-only regardless of the flag.
namespace cafecito.reflect_switch

annotation suite(name: String = "") targets CLASS
annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation fixture targets VARIABLE
annotation label(text: String) targets VARIABLE

@suite(name = "Base Suite")
class Base:
	@fixture
	var base_var: int = 0

	@test
	@timeout(5.0)
	func base_method() -> void:
		pass

class Derived extends Base:
	@test
	func derived_method() -> void:
		pass

trait Mixin:
	@label("from_trait")
	var mixin_var: int = 0

	@test
	func mixin_method() -> void:
		pass

class Impl uses Mixin:
	pass

func test() -> void:
	# Methods: effective (default) walks the base chain; direct restricts to the exact script.
	print(godot.reflection.get_method_annotations(Derived, "base_method").size())
	print(godot.reflection.get_method_annotations(Derived, "base_method", false).size())
	print(godot.reflection.get_method_annotations(Base, "base_method", false).size())
	print(godot.reflection.get_method_annotations(Base, "base_method", true).size())

	# Variables behave the same way.
	print(godot.reflection.get_variable_annotations(Derived, "base_var").size())
	print(godot.reflection.get_variable_annotations(Derived, "base_var", false).size())
	print(godot.reflection.get_variable_annotations(Base, "base_var", false).size())

	# Trait-flattened members are declared on the implementer, so the direct view still sees them.
	print(godot.reflection.get_method_annotations(Impl, "mixin_method", false).size())
	print(godot.reflection.get_variable_annotations(Impl, "mixin_var", false).size())

	# The generic get_annotations dispatches on kind and honors the flag.
	print(godot.reflection.get_annotations(Derived, "base_method", "method").size())
	print(godot.reflection.get_annotations(Derived, "base_method", "method", false).size())

	# has_annotation / get_annotation honor the flag.
	print(godot.reflection.has_annotation(Derived, "base_method", "test"))
	print(godot.reflection.has_annotation(Derived, "base_method", "test", "method", false))
	print(godot.reflection.get_annotation(Derived, "base_method", "test", "method", false) == null)
	print(godot.reflection.get_annotation(Derived, "base_method", "test").name)

	# Class annotations are direct-only; the flag is a no-op for class kind.
	print(godot.reflection.get_class_annotations(Derived).size())
	print(godot.reflection.get_annotations(Base, "", "class", false).size())
	print(godot.reflection.get_annotations(Base, "", "class", true).size())
