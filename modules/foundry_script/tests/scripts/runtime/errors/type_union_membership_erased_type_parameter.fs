# An erased type parameter promises nothing about the value it carries, so a union destination it
# reaches has proved no membership. The slot verifies it instead, exactly as the non-union spelling
# `func take(v: int)` is verified by the same parameter check.
class Box:
	var label = "box"


func take(v: int | String) -> void:
	print("took ", v)


func launder[T](value: T) -> void:
	take(value)


func test() -> void:
	launder(Box.new())
