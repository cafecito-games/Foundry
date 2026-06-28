extends RefCounted

const Target = preload("res://refactor/callsite_parameter_property_target.gd")

var trigger:
	set(value):
		var target := Target.new()
		target.accept_property("from_setter")

func use_target() -> void:
	var target := Target.new()
	target.accept_property(1)
