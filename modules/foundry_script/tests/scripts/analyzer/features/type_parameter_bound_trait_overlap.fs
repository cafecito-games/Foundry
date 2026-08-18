# A bound only makes a destination impossible when the two can be shown never to hold one value.
# Single inheritance shows that for two class chains, but a class implements any number of traits, so a
# value bounded by one trait can still turn out to satisfy an unrelated one. Narrowing a `T: ZaFirst`
# value to `ZaSecond` therefore stays a runtime-checked assignment rather than a static rejection.
trait ZaFirst:
	func first() -> void:
		print("first")


trait ZaSecond:
	func second() -> void:
		print("second")


class Both uses ZaFirst, ZaSecond:
	pass


func narrow[T: ZaFirst](value: T) -> void:
	var second: ZaSecond = value
	second.second()


func test() -> void:
	narrow(Both.new())
