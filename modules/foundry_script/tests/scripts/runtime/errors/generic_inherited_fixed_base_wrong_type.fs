# A dynamic write to an inherited member fixed to `int` by an intermediate `extends B[int]`
# specialization is rejected against `int`, even though the leaf instance is `C[float]`.
class A[T]:
	var a_value: T


class B[K] extends A[K]:
	var b_value: K


class C[T] extends B[int]:
	var c_value: T


func test() -> void:
	var c := C[float].new()
	var dyn: Variant = c
	dyn.a_value = "not an int"
	print("unreached")
