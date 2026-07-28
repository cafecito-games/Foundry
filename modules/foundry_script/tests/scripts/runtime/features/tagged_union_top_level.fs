# A whole-file tagged-union declaration registers a global enum whose payload-less cases are the same
# read-only `[tag]` singletons a nested declaration produces. The cases are reachable through the
# global enum name and, unqualified, as constants of the declaring script.
const EnumFile = preload("./tagged_union_top_level_values.notest.fs")

func test():
	print(RuntimeTopLevelMessage.Quit)
	print(RuntimeTopLevelMessage.Move(1, 2))
	print(RuntimeTopLevelMessage.Quit == RuntimeTopLevelMessage.Quit)
	print(RuntimeTopLevelMessage.Quit == RuntimeTopLevelMessage.Move(1, 2))
	print(RuntimeTopLevelMessage)

	var enum_file: Variant = EnumFile
	@warning_ignore("unsafe_property_access")
	print(enum_file.Quit)
