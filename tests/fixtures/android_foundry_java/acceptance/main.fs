extends Node

func _ready() -> void:
	if not ClassDB.class_exists("DemoExtension"):
		push_error("FOUNDRY_JAVA_EXPORT_ACCEPTANCE_FAILED class_missing")
		return
	var probe: Object = ClassDB.instantiate("DemoExtension")
	if probe == null:
		push_error("FOUNDRY_JAVA_EXPORT_ACCEPTANCE_FAILED instantiate_failed")
		return
	var result: int = probe.call("callback_probe", 41)
	probe.free()
	if result != 42:
		push_error("FOUNDRY_JAVA_EXPORT_ACCEPTANCE_FAILED probe_result")
		return
	print("FOUNDRY_JAVA_EXPORT_ACCEPTANCE_READY")
