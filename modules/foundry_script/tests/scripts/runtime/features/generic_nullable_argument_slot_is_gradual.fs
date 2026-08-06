# A container type descriptor cannot express "this type or null", so a nullable type argument leaves
# the projected slot without runtime evidence and the write is accepted. This is a pinned limitation
# of the runtime model, not an oversight: the analyzer is what enforces the declared type statically.
class Payload:
	pass


class Base[X]:
	var value: X


class Mid extends Base[Payload?]:
	pass


func test() -> void:
	var dynamic: Variant = Mid.new()
	dynamic.value = 1.0
	print("nullable slot stayed gradual")
