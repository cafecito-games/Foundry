class Inner:
	var value := 7

func make(x: int, cls := Inner, count: int = 0) -> int:
	return cls.new().value + x + count

func test():
	# `cls` defaults to a class metatype, which the compiler re-resolves to the live compiled
	# subclass rather than baking as a constant, so it cannot be inlined; pass it explicitly.
	print(make(1, count = 2))
