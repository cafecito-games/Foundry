class Plain:
	var value: long = 0

func take_long(v: long) -> long:
	return v

func test():
	var p := Plain.new()
	p.set("value", 5000000000UL)
	print(p.value)
	print(p.value is long)
	print(take_long(5000000000UL))
	var direct: long = 5000000000UL
	print(direct)
