# An instance argument does not satisfy a dependent handle bound (`T: Type[U]`).
class Holder[U: Node, T: Type[U]]:
	var value: T


func test():
	print(Holder[Button, Button].new())
