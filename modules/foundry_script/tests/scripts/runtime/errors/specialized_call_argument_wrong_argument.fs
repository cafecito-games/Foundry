# The call boundary converts each argument through the same structural test the stores use, so a
# parameter declared as a specialized class rejects a wrongly specialized value instead of binding it.
# The correctly specialized call goes through first, proving the parameter is otherwise callable.
class Pair[A, B]:
	pass


func take(value: Pair[int, String]) -> void:
	print("took ", value != null)


func test() -> void:
	var callback: Callable = take
	callback.call(Pair[int, String].new())
	callback.call(Pair.new())
	callback.call(Pair[int, Node].new())
	print("unreachable")
