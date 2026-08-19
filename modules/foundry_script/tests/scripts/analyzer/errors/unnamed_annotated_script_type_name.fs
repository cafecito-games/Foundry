extends RefCounted

const Helper = preload("./unnamed_type_name_helper.notest.fs")

func test():
	var helper: Helper = Helper.new()
	var value: int = helper
	print(value)
