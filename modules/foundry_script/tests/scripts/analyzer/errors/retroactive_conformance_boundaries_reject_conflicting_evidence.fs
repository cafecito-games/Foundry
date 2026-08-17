# The comparison lives in the type relation, not in one boundary: an argument, a return, and a member
# write all reject the same contradicted evidence.
trait RcdKeeper[T]:
	abstract func keep(item: T) -> T


class RcdTarget:
	pass


extend RcdTarget uses RcdKeeper[int]:
	func keep(item: int) -> int:
		return item


class RcdHolder:
	var slot: RcdKeeper[String] = null


func accept(_keeper: RcdKeeper[String]) -> void:
	pass


func produce() -> RcdKeeper[String]:
	return RcdTarget.new()


func test() -> void:
	accept(RcdTarget.new())
	var holder := RcdHolder.new()
	holder.slot = RcdTarget.new()
	print(produce())
