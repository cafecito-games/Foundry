# The static rule now answers the way the run-time relation always did. Each row prints the store
# answer and the `is` answer side by side, for absent, exact, and contradicted evidence. The
# contradicted store itself aborts, so it lives in
# runtime/errors/retroactive_conformance_native_store_rejects_conflicting_evidence.fs.
trait RcrKeeper[T]:
	abstract func keep(item: T) -> T


trait RcrOpen[T]:
	abstract func size() -> int


class RcrTarget:
	pass


class RcrOpenTarget:
	pass


extend RcrTarget uses RcrKeeper[int]:
	func keep(item: int) -> int:
		return item


extend RcrOpenTarget uses RcrOpen:
	func size() -> int:
		return 0


func test() -> void:
	var exact: Variant = RcrTarget.new()
	var exact_store: RcrKeeper[int] = exact
	print("exact store: ", exact_store != null)
	print("exact is: ", exact is RcrKeeper[int])

	var absent: Variant = RcrOpenTarget.new()
	var absent_store: RcrOpen[String] = absent
	print("absent store: ", absent_store != null)
	print("absent is: ", absent is RcrOpen[String])

	var conflicting: Variant = RcrTarget.new()
	print("conflicting is: ", conflicting is RcrKeeper[String])
