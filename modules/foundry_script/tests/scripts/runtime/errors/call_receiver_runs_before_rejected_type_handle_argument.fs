# The declared-parameter conversion loop can abort the frame; the receiver has already run by
# then, so its side effects are observable in the output before the error.
# https://github.com/cafecito-games/Foundry/issues/2255
class Acceptor:
	func accept[T](_klass: Type[T], _value: T) -> void:
		print("accepted")


func make_acceptor() -> Acceptor:
	print("receiver ran")
	return Acceptor.new()


func test():
	var klass: Variant = 1
	var resource := Resource.new()
	make_acceptor().accept(klass, resource)
	print("after call")
