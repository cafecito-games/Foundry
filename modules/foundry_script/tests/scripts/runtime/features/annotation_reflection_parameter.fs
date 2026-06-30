# foundry.reflection exposes passive custom annotation metadata for method and signal
# parameters, with the same effective owner-declaration view as method annotations.
namespace cafecito.reflect_parameter

annotation inject targets PARAMETER
annotation range(min: float, max: float) targets PARAMETER

class Base:
	func spawn(@inject factory: String, @range(0.0, 10.0) threat: float) -> void:
		pass

	signal damaged(@range(0.0, 999.0) amount: float)

class Derived extends Base:
	func spawn(@inject override_factory: String, @range(1.0, 5.0) threat: float) -> void:
		pass

trait Mixin:
	func mixin_call(@inject helper: String) -> void:
		pass

class Impl uses Mixin:
	pass

func test() -> void:
	var method_param := foundry.reflection.get_method_parameter_annotations(Base, "spawn", "factory")
	print(method_param.size())
	print(method_param[0].name)

	var method_range := foundry.reflection.get_method_parameter_annotations(Base, "spawn", "threat")
	print(method_range.size())
	print(method_range[0].args[0])
	print(method_range[0].args[1])

	var signal_param := foundry.reflection.get_signal_parameter_annotations(Base, "damaged", "amount")
	print(signal_param.size())
	print(signal_param[0].args[0])
	print(signal_param[0].args[1])

	# Inherited methods expose their parameter annotations through derived scripts.
	print(foundry.reflection.get_method_parameter_annotations(Derived, "spawn", "factory").size())

	# Overriding methods replace the base method's parameter annotation view.
	print(foundry.reflection.get_method_parameter_annotations(Derived, "spawn", "factory", false).size())
	print(foundry.reflection.get_method_parameter_annotations(Derived, "spawn", "override_factory", false).size())
	print(foundry.reflection.get_method_parameter_annotations(Base, "spawn", "factory", false).size())

	# Trait-flattened methods carry their declaration parameter annotations.
	print(foundry.reflection.get_method_parameter_annotations(Impl, "mixin_call", "helper").size())

	# Method descriptors embed parameter annotations alongside argument metadata.
	var descriptor := foundry.reflection.get_method_descriptor(Base, "spawn")
	var args: Array = descriptor.get_arguments()
	print(args.size())
	print(args[0].has("annotations"))
	print(args[0]["annotations"].size())
	print(args[1]["annotations"].size())

	# Invalid targets, unknown members, and unknown parameters return empty arrays.
	print(foundry.reflection.get_method_parameter_annotations(Base, "missing", "factory").size())
	print(foundry.reflection.get_method_parameter_annotations(Base, "spawn", "missing").size())
	print(foundry.reflection.get_method_parameter_annotations(42, "spawn", "factory").size())
