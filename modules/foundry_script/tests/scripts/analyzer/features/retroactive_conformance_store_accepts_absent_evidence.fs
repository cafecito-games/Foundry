# A conformance to a generic trait that supplies no arguments records none, and a conformance whose
# argument is the declaring generic's own parameter records nothing concrete either. Both are an
# absence of evidence, so every specialized store stays accepted.
trait RcjKeeper[T]:
	abstract func size() -> int


class RcjTarget:
	pass


class RcjGeneric[U]:
	pass


extend RcjTarget uses RcjKeeper:
	func size() -> int:
		return 0


extend RcjGeneric uses RcjKeeper:
	func size() -> int:
		return 1


func test() -> void:
	var specialized: RcjKeeper[String] = RcjTarget.new()
	var raw: RcjKeeper = RcjTarget.new()
	var generic: RcjKeeper[String] = RcjGeneric.new()
	print(specialized != null, raw != null, generic != null)
