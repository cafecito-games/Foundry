# An inherited generic signal is typed by its DECLARING class's parameters and the specialization
# passed up the `extends` chain, not by name-matching against the receiver. `reported` is declared in
# `A[T]`, and through `B[K] extends A[K]` then `C[T] extends B[int]` its `T` resolves to `int` — even
# though `C` reuses the name `T` and the receiver binds it to `String`.
class A[T]:
	signal reported(value: T)


class B[K] extends A[K]:
	pass


class C[T] extends B[int]:
	pass


func test(c: C[String]) -> void:
	c.reported.emit(1)
	c.reported.emit("nope")
