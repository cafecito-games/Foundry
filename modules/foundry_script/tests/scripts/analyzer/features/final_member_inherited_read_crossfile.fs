# Reading an inherited final declared in a cross-file base is allowed.
extends "res://analyzer/features/final_member_inherited_readable_base.notest.fs"

func plus_one() -> int:
	return id + 1

func test() -> void:
	print(plus_one())
