trait Greeter:
	func greet() -> String


trait Marker:
	func mark()


extend Node uses Marker:
	# why
	pass  # note


extend Node2D uses Greeter:
	pass  # lead

	func greet() -> String:
		return "hi"

	pass  # tail
