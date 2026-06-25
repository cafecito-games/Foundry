# A dynamic proxy satisfies `is` against the type it proxies, against traits that
# type uses, and against an abstract base in its chain; unrelated types fail.
trait Drawable:
	@abstract func draw_self() -> void

trait Sprite uses Drawable:
	@abstract func render() -> void

trait Unrelated:
	@abstract func z() -> void

@abstract class Base:
	@abstract func a() -> void

@abstract class Derived extends Base:
	@abstract func b() -> void

func test() -> void:
	var sprite: Object = create_proxy_dynamic(Sprite, func(_method: StringName, _args: Array) -> Variant:
		return null)
	print(sprite is Sprite)
	print(sprite is Drawable)
	print(sprite is Unrelated)

	var derived: Object = create_proxy_dynamic(Derived, func(_method: StringName, _args: Array) -> Variant:
		return null)
	print(derived is Derived)
	print(derived is Base)
	print(derived is Sprite)
