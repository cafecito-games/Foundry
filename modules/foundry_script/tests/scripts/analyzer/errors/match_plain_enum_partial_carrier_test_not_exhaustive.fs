# `long` is the carrier a plain enum travels in, so a narrower width is a range test that some values
# of the slot fail. Only the whole carrier closes the match.
enum Level:
	LOW = 1
	HIGH = 2


func describe(value: Level) -> String:
	match value:
		value is int:
			return "narrow"


func test():
	print(describe(Level.LOW))
