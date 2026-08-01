# A witness resolves names against its conformance target first and then falls back to the lexical
# type scope of the file that declares the `extend`. The native target's stand-in has no lexical
# outer, so without that fallback this file's own generic would be unreachable from both the witness
# signature and its body.
class Box[T]:
	func marker() -> String:
		return "boxed"


trait Boxable:
	abstract static func boxed() -> Box[Self]


extend RefCounted uses Boxable:
	static func boxed() -> Box[Self]:
		var made: Box[Self] = Box[Self].new()
		return made


func test() -> void:
	print(RefCounted.boxed().marker())
