# Annotation position and value position must spell the same generic argument the same way: a
# qualified represented class (`Outer.Factory`) and a use-site `?` on a nested argument both have to
# survive the value-position resolution that `Slot[...].new()` goes through.
class Outer:
	class Factory extends RefCounted:
		static func create() -> Outer.Factory:
			return Outer.Factory.new()


class Box[T] extends RefCounted:
	var value: T


class Slot[T]:
	var value: T


func test() -> void:
	var qualified: Slot[Type[Outer.Factory]] = Slot[Type[Outer.Factory]].new()
	qualified.value = Outer.Factory
	print(qualified.value.create() is Outer.Factory)

	# A nullable nested argument keeps its marker, so the two spellings agree and `null` is storable.
	var nullable: Slot[Box[Outer.Factory?]] = Slot[Box[Outer.Factory?]].new()
	nullable.value = Box[Outer.Factory?].new()
	nullable.value.value = null
	print(nullable.value.value == null)
	print("qualified and nullable arguments ok")
