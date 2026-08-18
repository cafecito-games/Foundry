# The ClassDB chain rule must not reach targets that have no ClassDB chain to share. A builtin value
# type is keyed by its own name and inherits from nothing, and a Foundry Script class is not an engine
# class at all, so both may bind the same trait to arguments an unrelated engine-class conformance
# contradicts.
trait RnccoKeeper[T]:
	abstract func make() -> T


class RnccoHolder:
	pass


extend Node uses RnccoKeeper[int]:
	func make() -> int:
		return 7


extend String uses RnccoKeeper[String]:
	func make() -> String:
		return self


extend RnccoHolder uses RnccoKeeper[bool]:
	func make() -> bool:
		return true


func test() -> void:
	var text: RnccoKeeper[String] = "seven"
	print(text.make())
	var holder: RnccoKeeper[bool] = RnccoHolder.new()
	print(holder.make())
