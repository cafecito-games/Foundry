# The other cross-file direction: the derived class's conformance is the preloaded one, and the
# declaration on its script base analyzed here is what the rule has to reject. The pair is rejected
# whichever file is analyzed second.
const _Holder = preload("say_ancestor_holder.notest.fs")


extend SayMiddle uses SayKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	print("unreachable")
