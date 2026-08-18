# A local that merely stores `self` is a separately named value, not the receiver expression, so it is
# no more proof of receiver identity than an alias of any other value: an open receiver named through
# an alias still rejects the calling frame's `self` in its `Self` position.
class Cell:
	func take(other: Self) -> void:
		pass

	func route() -> void:
		var alias := self
		alias.take(self)


func test() -> void:
	Cell.new().route()
