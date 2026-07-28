# A `match` branch can name a tagged-union case and match its payload positionally. A bare identifier
# in a payload position binds the value, `_` skips it, and a literal or a nested pattern narrows the
# branch further. A payload-less case is matched as the plain value it is.
enum MatchMessage:
	Quit
	Move(x: int, y: int)
	Write(text: String)

enum MatchShape:
	Point
	Circle(radius: float)
	Rect(size: (int, int))

func handle_message(msg: MatchMessage) -> void:
	if msg is MatchMessage.Move(x, y):
		prints("preview move", x, y)
	match msg:
		MatchMessage.Quit:
			print("quit")
		MatchMessage.Move(x, y):
			prints("move", x, y)
		MatchMessage.Write(_):
			print("write")

func classify(msg: MatchMessage) -> String:
	match msg:
		MatchMessage.Move(0, 0):
			return "origin"
		MatchMessage.Move(0, y):
			return "vertical %d" % y
		MatchMessage.Move(x, y) when x == y:
			return "diagonal %d" % x
		MatchMessage.Move(x, y):
			return "move %d,%d" % [x, y]
		MatchMessage.Write(text):
			return "write " + text
		MatchMessage.Quit:
			return "quit"
	return "unreachable"

const EXPECTED_STEP: int = 5

func step_kind(msg: MatchMessage) -> String:
	# Parentheses turn a payload position back into an ordinary value pattern, so this compares
	# against the constant instead of binding a new name.
	match msg:
		MatchMessage.Move((EXPECTED_STEP), _):
			return "expected step"
		_:
			return "other"

func describe_shape(shape: MatchShape) -> String:
	match shape:
		MatchShape.Circle(radius):
			return "circle %.1f" % radius
		MatchShape.Rect((var width, var height)):
			return "rect %dx%d" % [width, height]
		MatchShape.Point:
			return "point"
	return "unreachable"

func test():
	handle_message(MatchMessage.Move(1, 2))
	handle_message(MatchMessage.Write("hi"))
	handle_message(MatchMessage.Quit)

	print(classify(MatchMessage.Move(0, 0)))
	print(classify(MatchMessage.Move(0, 5)))
	print(classify(MatchMessage.Move(3, 3)))
	print(classify(MatchMessage.Move(4, 7)))
	print(classify(MatchMessage.Write("done")))
	print(classify(MatchMessage.Quit))

	print(describe_shape(MatchShape.Circle(2.5)))
	print(describe_shape(MatchShape.Rect((3, 4))))
	print(describe_shape(MatchShape.Point))

	print(step_kind(MatchMessage.Move(EXPECTED_STEP, 1)))
	print(step_kind(MatchMessage.Move(1, 1)))

	# Several patterns can share a branch as long as none of them binds.
	for message in [MatchMessage.Quit, MatchMessage.Move(9, 9), MatchMessage.Write("x")]:
		match message:
			MatchMessage.Quit, MatchMessage.Write(_):
				print("no movement")
			_:
				print("movement")
