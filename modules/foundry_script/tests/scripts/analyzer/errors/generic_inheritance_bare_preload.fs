# A preloaded generic base resolves to the same declaration as a global name, so it is rejected the
# same way.
const BoxScript = preload("generic_inheritance_bare_external_library.notest.fs")


class Bad extends BoxScript:
	pass


func test() -> void:
	print("unreachable")
