# foundry.reflection exposes passive custom annotation metadata as FSAnnotation
# descriptors: per class, method, and member variable, with effective base-chain and
# trait-flattened views, plus annotations embedded in method/property descriptors.
namespace cafecito.reflect_demo

annotation suite(name: String = "") targets CLASS
annotation tags(...names: String) targets CLASS, METHOD
annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation fixture targets VARIABLE
annotation label(text: String) targets VARIABLE
annotation repeatable(value: String) targets METHOD

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
	@tags("trait")
	func mixin_method() -> void:
		pass

class Impl uses Mixin:
	@repeatable("a")
	@repeatable("b")
	func repeated_method() -> void:
		pass

func test() -> void:
	# Class annotations are direct-only and not inherited.
	var base_class := foundry.reflection.get_class_annotations(Base)
	print(base_class.size())
	print(base_class[0].name)
	print(base_class[0].qualified_name)
	print(base_class[0].kwargs["name"])
	print(foundry.reflection.get_class_annotations(Derived).size())

	# Method annotations preserve source order; the base view is visible through the derived script.
	var base_method := foundry.reflection.get_method_annotations(Base, "base_method")
	print(base_method.size())
	print(base_method[0].name)
	print(base_method[1].name)
	print(base_method[1].args[0])
	print(foundry.reflection.get_method_annotations(Derived, "base_method").size())

	# Member-variable annotations, effective through the derived script.
	var base_variable := foundry.reflection.get_variable_annotations(Base, "base_var")
	print(base_variable.size())
	print(base_variable[0].name)
	print(foundry.reflection.get_variable_annotations(Derived, "base_var").size())

	# Trait-flattened members carry their declaration annotations through the implementer.
	var mixin_method := foundry.reflection.get_method_annotations(Impl, "mixin_method")
	print(mixin_method.size())
	print(mixin_method[0].name)
	print(mixin_method[1].name)
	print(mixin_method[1].args[0])
	print(foundry.reflection.get_variable_annotations(Impl, "mixin_var")[0].name)

	# has_annotation / get_annotation match short and qualified names.
	print(foundry.reflection.has_annotation(Base, "base_method", "test"))
	print(foundry.reflection.has_annotation(Base, "base_method", "cafecito.reflect_demo.timeout"))
	print(foundry.reflection.has_annotation(Base, "base_method", "missing"))
	print(foundry.reflection.get_annotation(Base, "base_method", "timeout").args[0])
	print(foundry.reflection.has_annotation(Base, "", "suite", "class"))
	print(foundry.reflection.has_annotation(Base, "base_var", "fixture", "variable"))
	print(foundry.reflection.get_annotation(Base, "missing_method", "test") == null)

	# Repeated annotations are preserved in source order.
	var repeated := foundry.reflection.get_method_annotations(Impl, "repeated_method")
	print(repeated.size())
	print(repeated[0].args[0])
	print(repeated[1].args[0])

	# The generic get_annotations dispatches on kind.
	print(foundry.reflection.get_annotations(Base, "", "class").size())
	print(foundry.reflection.get_annotations(Base, "base_method", "method").size())
	print(foundry.reflection.get_annotations(Base, "base_var", "variable").size())

	# Descriptor embedding in get_method_info / get_properties.
	var method_info := foundry.reflection.get_method_info(Base, "base_method")
	var method_annotations: Array = method_info["annotations"]
	print(method_annotations.size())
	for property in foundry.reflection.get_properties(Base):
		if str(property["name"]) == "base_var":
			var variable_annotations: Array = property["annotations"]
			print(variable_annotations.size())

	# Invalid / non-script targets return empty / null / false without crashing.
	print(foundry.reflection.get_class_annotations(42).size())
	print(foundry.reflection.get_method_annotations(null, "x").size())
	print(foundry.reflection.has_annotation(42, "x", "y"))
	print(foundry.reflection.get_annotation(42, "x", "y") == null)
