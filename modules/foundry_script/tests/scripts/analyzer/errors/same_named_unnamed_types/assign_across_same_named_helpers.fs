extends RefCounted

const HelperA = preload("./a/helper.notest.fs")
const HelperB = preload("./b/helper.notest.fs")

func test():
	var value: HelperB = HelperA.new()
	print(value)
