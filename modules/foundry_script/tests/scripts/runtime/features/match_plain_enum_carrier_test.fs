# A plain enum's integer carrier accepts every value an enum-typed slot can hold, undeclared ones
# included, so an unguarded test against the carrier closes the match that `value is Level` leaves open.
enum Level:
	LOW = 1
	HIGH = 2


func describe(level: Level) -> String:
	match level:
		level is long:
			return "carrier:" + str(level)


func record(level: Level) -> void:
	match level:
		level is long:
			print("recorded:" + str(level))


func test():
	print(describe(Level.LOW))
	print(describe(99 as Level))
	record(Level.HIGH)
