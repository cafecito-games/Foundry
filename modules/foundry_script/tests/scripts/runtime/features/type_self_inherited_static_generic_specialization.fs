class Crate[T]:
	var value: T


trait Packing:
	abstract static func pack(value: Self) -> Crate[Self]


extend Resource uses Packing:
	static func pack(value: Self) -> Crate[Self]:
		var crate := Crate[Self].new()
		crate.value = value
		return crate


func test() -> void:
	var image := ImageTexture.new()
	var widened: Variant = ImageTexture.pack(image)
	print(widened.value.get_class())
	widened.value = Material.new()
