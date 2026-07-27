enum Message:
	Quit
	Move(x: int, y: int)
	Write(text: String)

	func describe() -> String:
		return "message"

enum Small:
	Empty
	Single(value: int)
