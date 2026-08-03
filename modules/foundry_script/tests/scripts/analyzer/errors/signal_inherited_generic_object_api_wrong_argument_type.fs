# The string-name `Object` signal APIs resolve through the same specialized lookup, both for a local
# (implicit `self`) receiver inside the subclass and for a typed external receiver.
class Base[T]:
	signal reported(value: T)


class Child extends Base[int]:
	func go() -> void:
		emit_signal("reported", "nope")


func test(child: Child) -> void:
	child.emit_signal(&"reported", "nope")
