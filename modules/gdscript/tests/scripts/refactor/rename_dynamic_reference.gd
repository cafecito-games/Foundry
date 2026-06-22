extends Node

var dynamic_value := 0

func read_dynamic() -> Variant:
	return get("dynamic_value")


func unrelated_strings() -> void:
	widget("dynamic_value")
	offset("dynamic_value")
	recall("dynamic_value")
	var label := "dynamic_value"; call("unrelated_method")


func widget(_value: String) -> void:
	pass


func offset(_value: String) -> void:
	pass


func recall(_value: String) -> void:
	pass


func unrelated_method() -> void:
	pass
