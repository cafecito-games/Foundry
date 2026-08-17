# A `class_name`/`trait_name` pair declared in other files resolves to the same parse-tree nodes as a
# local declaration, so the conflicting argument is rejected across files too.
func test() -> void:
	var slot: ProbeKeeper[int] = ProbeForwarder[String].new()
	print(slot)
