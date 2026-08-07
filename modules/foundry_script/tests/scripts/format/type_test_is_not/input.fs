func run(value: Variant, subject: int) -> void:
	if value is not int:
		print("not an int")
	if value is not .None:
		print("not the empty case")
	if not (subject is int):
		print("still a prefix not")
	var flag = value is not int and subject is int
	print(flag)
