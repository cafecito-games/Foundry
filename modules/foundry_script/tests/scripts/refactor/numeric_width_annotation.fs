extends Node

func test() -> void:
	var plain := 5
	var unsigned := 5U
	var wide := 5L
	var unsigned_wide := 18446744073709551615UL
	print(plain, unsigned, wide, unsigned_wide)
