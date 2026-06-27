# A subclass in another file cannot reassign a final declared in its cross-file
# base; the base script owns the single assignment slot.
extends "res://analyzer/errors/final_member_inherited_base.notest.gd"

func reset() -> void:
	id = 2

func test() -> void:
	pass
