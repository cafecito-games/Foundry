# A trait may require a generic method; an implementation satisfies it up to type-parameter renaming
# (alpha-equivalence by ordinal position). Here `transform[V]` satisfies required `transform[U]`, and
# a matching bound aligns after renaming.
trait Mapper:
	abstract func transform[U](value: U) -> Array[U]


trait Bounded:
	abstract func keep[U: RefCounted](value: U) -> U


class Impl:
	uses Mapper, Bounded

	func transform[V](value: V) -> Array[V]:
		return [value, value]

	func keep[W: RefCounted](value: W) -> W:
		return value


func test() -> void:
	var impl := Impl.new()
	print(impl.transform(7).size())
	var ref := RefCounted.new()
	print(impl.keep(ref) == ref)
	print("trait generic method ok")
