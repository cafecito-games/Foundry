extends Object

enum Message:
	Quit
	Move(x: int, y: int)

func take(handler: Callable) -> void:
	pass

func test() -> void:
	take(Message.➡)
	pass
