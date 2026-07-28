class_name LspTaggedUnions

## A message the actor can receive.
enum Message:
	## Stop processing.
	Quit
	## Move by a delta.
	Move(x: int, y: int)
	Write(text: String)

func handle(message: Message) -> void:
	if message is Message.Move(x, y):
		prints(x, y)
