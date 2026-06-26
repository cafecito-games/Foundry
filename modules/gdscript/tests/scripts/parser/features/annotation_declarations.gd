annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation tags(...names: String) targets METHOD, CLASS
annotation skip(reason: String = "") targets METHOD, CLASS
annotation fixture targets VARIABLE
annotation suite(name: String = "") targets CLASS

func test():
	print("annotation declarations parsed")
