# Each trait identity in a conformance's implied closure records its own arguments, so a supertrait
# reached through `uses Sub[int]` proves `Super[int]` and contradicts `Super[String]`.
trait RcfSuper[T]:
	abstract func keep(item: T) -> T


trait RcfSub[T]:
	uses RcfSuper[T]


class RcfTarget:
	pass


extend RcfTarget uses RcfSub[int]:
	func keep(item: int) -> int:
		return item


func test() -> void:
	var conflicting: RcfSuper[String] = RcfTarget.new()
	print(conflicting)
