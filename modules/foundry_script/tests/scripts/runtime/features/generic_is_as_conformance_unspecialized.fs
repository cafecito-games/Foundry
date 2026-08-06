# A conformance to a generic trait that supplies no arguments records none. That is an absence of
# evidence rather than a wildcard, so the raw trait target succeeds and every specialized one fails.
trait Boxed[T]:
	abstract func size() -> int


class Crate:
	pass


extend Crate uses Boxed:
	func size() -> int:
		return 0


func test() -> void:
	var crate: Variant = Crate.new()

	print("crate is Boxed: ", crate is Boxed)
	print("crate is Boxed[int]: ", crate is Boxed[int])
	print("crate is Boxed[String]: ", crate is Boxed[String])
	print("crate is Boxed[Variant]: ", crate is Boxed[Variant])

	var raw_cast: Variant = crate as Boxed
	print("raw cast keeps identity: ", raw_cast == crate)
	var specialized_cast: Variant = crate as Boxed[int]
	print("specialized cast is null: ", specialized_cast == null)
