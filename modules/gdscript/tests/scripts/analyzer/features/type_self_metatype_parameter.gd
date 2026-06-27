class Base:
	func accept_type(klass: Type[Self]) -> void:
		print(klass == Child)


class Child:
	extends Base


func test() -> void:
	var child := Child.new()
	child.accept_type(Child)
