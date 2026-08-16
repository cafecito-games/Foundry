# A construction inside a generic trait's body names the trait's parameters, whose ordinals index what
# the implementer supplied. Forwarding the implementer's own parameter keeps the construction
# receiver-resolved; a concrete application substitutes the argument in and reifies with no receiver
# involved at all.
class Holder[T]:
	var value: T


trait Keeper[V]:
	func build() -> Holder[V]:
		return Holder[V].new()


class ForwardingKeeper[U]:
	uses Keeper[U]


class ConcreteKeeper:
	uses Keeper[int]


func test() -> void:
	var forwarded: Variant = ForwardingKeeper[String].new().build()
	print(forwarded is Holder[String])
	print(forwarded is Holder[int])

	var concrete: Variant = ConcreteKeeper.new().build()
	print(concrete is Holder[int])
	print(concrete is Holder[String])
	print("trait forwarded construction ok")
