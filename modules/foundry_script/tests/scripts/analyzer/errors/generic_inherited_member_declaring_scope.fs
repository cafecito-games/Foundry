# An inherited generic member is typed by its DECLARING class's parameters and the specialization
# passed up the `extends` chain, not by name-matching against the receiver. `a_value` is declared in
# `A[T]`, and through `B[K] extends A[K]` then `C extends B[int]` its `T` resolves to `int` — even
# though `C` reuses the name `T` for an unrelated parameter. Assigning it to a String must error,
# proving the substitution did not bind `a_value` to `C`'s `T`.
class A[T]:
	var a_value: T


class B[K] extends A[K]:
	pass


class C[T] extends B[int]:
	var c_value: T


func test() -> void:
	var c := C[String].new()
	var bad: String = c.a_value
	print(bad)
	print(c.c_value)
