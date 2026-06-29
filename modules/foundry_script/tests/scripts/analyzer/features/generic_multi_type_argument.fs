# Use-site type arguments accept more than one parameter (`Pair[int, String]`) and richer
# single-argument forms (`?` nullable markers, Callable signatures). This fixture exercises the
# parsing of those forms plus the multi-argument plumbing that reaches generic-class
# specialization and explicit generic-method application. Deeper semantic interpretation of the
# nullable and Callable forms is tracked by other epic #125 issues.

class Pair[K, V]:
	var first: K
	var second: V

	func set_first(value: K) -> void:
		first = value


class Box[T]:
	var value: T


func swap_pair[A, B](a: A, b: B) -> void:
	print(a, " ", b)


func identity[T](v: T) -> T:
	return v


func test() -> void:
	var p := Pair[int, String].new()
	p.first = 7
	p.second = "hi"
	print(p.first)
	print(p.second)

	# Explicit multi-argument generic-method application binds both parameters.
	swap_pair[int, String](1, "x")

	# A use-site `?` nullable marker parses for both generic-class specialization and explicit
	# generic-method application. The marker is captured separately, so the type argument binds to
	# the underlying type; nullable semantics under strict_null_checks are tracked separately.
	var b := Box[Node?].new()
	print(b.value)
	print(identity[Node?](null))

	print("multi type argument ok")
