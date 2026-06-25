class_name LspTraits

trait Drawable:
	func draw_self() -> void:
		pass

class Sprite:
	uses Drawable

func render(item: Drawable) -> void:
	item.draw_self()
