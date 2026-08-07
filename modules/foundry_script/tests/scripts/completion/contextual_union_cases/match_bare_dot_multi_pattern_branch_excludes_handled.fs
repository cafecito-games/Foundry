extends Object

enum Step:
	Begin(index: int)
	Middle
	End

func test(step: Step) -> void:
	match step:
		.Middle, .End:
			pass
		.➡
