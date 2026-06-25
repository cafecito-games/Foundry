# An inherited `T`-typed member fixed by an intermediate `extends Base[int]` specialization reifies
# and validates against that fixed argument, not the most-derived class's own argument. Here `a_value`
# and `b_value` are fixed to `int` by `extends B[int]`, while `c_value` stays open and reifies as the
# leaf's `float`. A non-generic subclass of a specialized base (`IntBox extends A[int]`) likewise
# validates its inherited member as `int` even though the instance carries no reified arguments.
class A[T]:
	var a_value: T


class B[K] extends A[K]:
	var b_value: K


class C[T] extends B[int]:
	var c_value: T


class IntBox extends A[int]:
	var label := ""


func test() -> void:
	var c := C[float].new()
	c.a_value = 1
	c.b_value = 2
	c.c_value = 3.5
	print(c.a_value, " ", c.b_value, " ", c.c_value)

	var box := IntBox.new()
	box.a_value = 9
	print(box.a_value)
	print("inherited fixed base ok")
