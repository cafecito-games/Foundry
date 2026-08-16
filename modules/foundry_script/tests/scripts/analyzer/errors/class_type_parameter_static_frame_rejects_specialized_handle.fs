# A class type parameter is reified onto the instance, which is what lets an instance method check a
# downcast against the receiver's actual argument. A parameter sitting in the type argument of a
# specialized class handle is no different now that the construction reifies it: a static function has
# no receiver, so there is no check to license the downcast and the value would be laundered into the
# slot untested.
class BaseHolder:
	pass


class Holder[T] extends BaseHolder:
	var value: T


class Crate[U]:
	static func downcast_local(value: BaseHolder) -> void:
		var kept: Holder[U] = value
		print(kept)

	static func downcast_return(value: BaseHolder) -> Holder[U]:
		return value

	func instance_local(value: BaseHolder) -> Holder[U]:
		# The same downcast inside an instance method is checked against this receiver's argument.
		var kept: Holder[U] = value
		return kept


func test() -> void:
	Crate[int].downcast_local(Holder[int].new())
	print(Crate[int].downcast_return(Holder[int].new()) != null)
	print(Crate[int].new().instance_local(Holder[int].new()) != null)
