# A supertrait reached through a retroactive conformance answers from the argument substituted into
# it: `uses Sub[int]` on a `trait Sub[T]: uses Super[T]` proves `Super[int]`, not `Super[String]`.
trait Super[T]:
	abstract func fetch() -> T


trait Sub[T]:
	uses Super[T]

	abstract func store(item: T) -> void


class Holder:
	var kept: int = 0


extend Holder uses Sub[int]:
	func store(item: int) -> void:
		kept = item

	func fetch() -> int:
		return kept


func test() -> void:
	var holder: Variant = Holder.new()

	print("holder is Sub: ", holder is Sub)
	print("holder is Sub[int]: ", holder is Sub[int])
	print("holder is Sub[String]: ", holder is Sub[String])

	print("holder is Super: ", holder is Super)
	print("holder is Super[int]: ", holder is Super[int])
	print("holder is Super[String]: ", holder is Super[String])

	var exact_cast: Variant = holder as Super[int]
	print("exact supertrait cast keeps identity: ", exact_cast == holder)
	var mismatched_cast: Variant = holder as Super[String]
	print("mismatched supertrait cast is null: ", mismatched_cast == null)
