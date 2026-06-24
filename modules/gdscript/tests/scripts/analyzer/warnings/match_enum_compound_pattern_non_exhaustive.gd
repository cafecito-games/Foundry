enum State { A, B, C }

func test():
	var state := State.A
	match state:
		State.A:
			print("a")
		[State.B]:
			print("array")
	print("ok")
