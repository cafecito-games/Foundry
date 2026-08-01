# The declaration-site fallback is one language rule, not a native-target exception: a builtin
# value-type target's stand-in has no lexical outer either, so its witness reaches this file's own
# generic through the same fallback, in the return annotation and in the body.
class Crate[T]:
	var values: Array[T] = []

	func summary() -> String:
		return "crate:" + str(values.size())


trait Cratable:
	abstract static func packed() -> Crate[Self]


extend int uses Cratable:
	static func packed() -> Crate[Self]:
		var made: Crate[Self] = Crate[Self].new()
		return made


func test() -> void:
	print(int.packed().summary())
