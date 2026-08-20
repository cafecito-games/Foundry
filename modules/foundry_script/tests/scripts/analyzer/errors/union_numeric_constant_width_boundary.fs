# A constant whose width the two type names cannot describe is reported by its value and reported
# once. The union spelling gets the same single sentence the plain one gets: the alternative that owns
# the range owns the refusal, and the position adds nothing when its own report would print the same
# word twice.
func take_plain(v: int) -> void:
	print(v)


func take_union(v: int | String) -> void:
	print(v)


func test():
	take_plain(5000000000)
	take_union(5000000000)

	var plain: int = 5000000000
	var stored: int | String = 5000000000
	print(plain, stored)
