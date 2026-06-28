# godot.reflection exposes passive custom annotation metadata for signal and constant
# declarations, with the same effective base-chain view as member variables.
namespace cafecito.reflect_signal_constant

annotation event(channel: String = "") targets SIGNAL
annotation config(key: String) targets CONSTANT
annotation doc(text: String) targets SIGNAL, CONSTANT

class Base:
	@event(channel = "combat")
	@doc("fired on hit")
	signal damage_taken(amount: int)

	@config("max_health")
	@doc("upper bound")
	const MAX_HEALTH = 100

class Derived extends Base:
	pass

func test() -> void:
	# Signal annotations preserve source order.
	var signal_annotations := godot.reflection.get_signal_annotations(Base, "damage_taken")
	print(signal_annotations.size())
	print(signal_annotations[0].name)
	print(signal_annotations[0].kwargs["channel"])
	print(signal_annotations[1].name)
	print(signal_annotations[1].args[0])

	# Constant annotations preserve source order.
	var constant_annotations := godot.reflection.get_constant_annotations(Base, "MAX_HEALTH")
	print(constant_annotations.size())
	print(constant_annotations[0].name)
	print(constant_annotations[0].args[0])
	print(constant_annotations[1].name)

	# The effective view is visible through the derived script.
	print(godot.reflection.get_signal_annotations(Derived, "damage_taken").size())
	print(godot.reflection.get_constant_annotations(Derived, "MAX_HEALTH").size())

	# The generic get_annotations dispatches on signal / constant kinds.
	print(godot.reflection.get_annotations(Base, "damage_taken", "signal").size())
	print(godot.reflection.get_annotations(Base, "MAX_HEALTH", "constant").size())

	# has_annotation / get_annotation match short and qualified names by kind.
	print(godot.reflection.has_annotation(Base, "damage_taken", "event", "signal"))
	print(godot.reflection.has_annotation(Base, "MAX_HEALTH", "cafecito.reflect_signal_constant.config", "constant"))
	print(godot.reflection.has_annotation(Base, "damage_taken", "missing", "signal"))
	print(godot.reflection.get_annotation(Base, "MAX_HEALTH", "config", "constant").args[0])

	# Unannotated and missing members, and non-script targets, stay empty / null / false.
	print(godot.reflection.get_signal_annotations(Base, "missing_signal").size())
	print(godot.reflection.get_constant_annotations(42, "MAX_HEALTH").size())
	print(godot.reflection.get_annotation(Base, "missing_constant", "config", "constant") == null)
