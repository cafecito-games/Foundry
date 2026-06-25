# `T` appears only in the return type, so no argument constrains it and inference cannot solve
# it. The call must direct the user to apply the type argument explicitly.
func make[T]() -> T:
	return null


func test():
	var value := make()
	print(value)
