extends RefCounted

const HelperA = preload("./a/helper.notest.fs")
const HelperB = preload("./b/helper.notest.fs")

func test():
	var wrong: HelperB.Point = HelperA.Point(1, 2)
	print(wrong)
