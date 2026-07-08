class Outer:
	enum { READY }

	class Inner:
		signal READY()

func test() -> void:
	pass
