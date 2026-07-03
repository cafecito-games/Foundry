static var discovery_side_effect_counter := _record_side_effect()

static func _record_side_effect() -> int:
	print("PROJECT_SCRIPTS_DISCOVERY_SIDE_EFFECT")
	return 1

class SideEffectSuite extends RefCounted:
	func _init() -> void:
		print("PROJECT_SCRIPTS_DISCOVERY_INIT")
