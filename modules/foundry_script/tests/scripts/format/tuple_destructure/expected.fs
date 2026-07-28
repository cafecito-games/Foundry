tuple Player(name: String, hp: int)


func test():
	var (x, y) = (1, 2)
	const (first, second) = ("ana", 3)
	var (_, middle, _) = (1, 2, 3)
	var (name, hp) = Player("ana", 4)
	print(x + y + middle + hp + second + first.length())
