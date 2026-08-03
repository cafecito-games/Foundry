# The declaration is reusable: two receivers specializing the same generic base independently
# produce `Signal[[int]]` and `Signal[[String]]`, and resolving one does not contaminate the other.
class Base[T]:
	signal reported(value: T)


func test(ints: Base[int], strings: Base[String]) -> void:
	ints.reported.emit(1)
	strings.reported.emit("ok")
	ints.reported.emit("nope")
	strings.reported.emit(1)
