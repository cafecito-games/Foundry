extends RefCounted

const Helper = preload("./unnamed_type_name_helper.notest.fs")

func test():
	var value: int = Helper.new()
	print(value)
