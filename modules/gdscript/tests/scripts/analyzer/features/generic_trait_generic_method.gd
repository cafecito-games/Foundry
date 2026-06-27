# A generic trait may also require generic methods. The trait's use-site binding (`T := int`) does not
# capture a method's own type parameter that shadows the trait parameter by name (`echo[T]`), and it
# does specialize a method type-parameter bound that references the trait parameter (`keep[U: T]`).
trait Box[T: RefCounted]:
	abstract func put(item: T) -> void
	abstract func echo[T](value: T) -> T
	abstract func keep[U: T](value: U) -> U


class RefBox uses Box[RefCounted]:
	func put(_item: RefCounted) -> void:
		pass

	func echo[V](value: V) -> V:
		return value

	func keep[W: RefCounted](value: W) -> W:
		return value


func test() -> void:
	var box := RefBox.new()
	var ref := RefCounted.new()
	box.put(ref)
	print(box.echo(5))
	print(box.keep(ref) == ref)
	print("generic trait generic method ok")
