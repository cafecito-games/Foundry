extends RefCounted
uses Bag

trait Bag:
	signal changed
	const CAPACITY := 8
	enum Kind { A, B }
	var kind: Kind = Kind.B

func test() -> void:
	print(CAPACITY)
	print(kind)
	print(Kind.A)
	print(changed.get_name())
