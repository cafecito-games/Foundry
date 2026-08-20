class Plain:
	var value: long = 0

func test():
	var p := Plain.new()
	var big: ulong = 5000000000
	p.set("value", big)
	print(p.value)
