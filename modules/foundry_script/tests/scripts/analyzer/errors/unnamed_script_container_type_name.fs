extends RefCounted

const Helper = preload("./unnamed_type_name_helper.notest.fs")

func test():
	var helpers: Array[Helper] = []
	var value: int = helpers
	print(value)
