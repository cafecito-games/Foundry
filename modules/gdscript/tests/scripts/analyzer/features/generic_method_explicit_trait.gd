# Explicit type-argument application `name[TypeArgs](...)` resolves generic methods flattened in
# from an applied trait, both on a `self` (identifier) receiver and on an instance receiver, and for
# multi-parameter generic methods bound positionally.
trait Echoer:
	func echo_value[T](value: T) -> T:
		return value

	func combine[A, B](a: A, b: B) -> String:
		return str(a) + ":" + str(b)


class Holder:
	uses Echoer

	func via_self() -> int:
		# Self-dispatch explicit application of a trait-provided generic method.
		return echo_value[int](5)


func test() -> void:
	var holder := Holder.new()
	print(holder.via_self())

	# Instance-receiver explicit application of a trait-provided generic method.
	var name: String = holder.echo_value[String]("hi")
	print(name)

	# Multi-parameter trait-provided generic method, applied explicitly.
	print(holder.combine[int, String](1, "x"))
	print("explicit trait generic ok")
