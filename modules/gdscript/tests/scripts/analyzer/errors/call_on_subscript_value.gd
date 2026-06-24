# Calling the result of an index expression is still an error. The parser now lets
# `name[...](...)` through so the analyzer can interpret use-site type arguments, but
# until generic-method application is resolved this remains a call on an expression.
func test():
	var a = [1]
	a[0]()
