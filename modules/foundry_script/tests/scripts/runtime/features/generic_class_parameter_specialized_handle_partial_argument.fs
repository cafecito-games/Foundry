# A handle whose arguments mix a concrete type with a class type parameter reifies fully when the
# receiver supplies the parameter, and falls back to the unspecialized form as a whole when it does
# not -- never to a vector that keeps `int` for one slot while fabricating evidence for the other.
class Pair[A, B]:
	var first: A
	var second: B


class Wrapper[U]:
	func build() -> Pair[int, U]:
		return Pair[int, U].new()


func test() -> void:
	var specialized: Variant = Wrapper[String].new().build()
	print(specialized is Pair[int, String])
	print(specialized is Pair[int, float])
	specialized.first = 1
	specialized.second = "text"
	print(specialized.first, " ", specialized.second)

	var unspecialized: Variant = Wrapper.new().build()
	print(unspecialized is Pair)
	print(unspecialized is Pair[int, String])
	# Nothing resolved, so the whole construction is unspecialized and neither slot is constrained.
	unspecialized.first = "text"
	print(unspecialized.first)
	print("partial argument ok")
