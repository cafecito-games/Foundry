extends Object

enum Message:
	Quit
	Move(x: int, y: int)

func test() -> void:
	var m = Message.➡(1, 2)
	pass
