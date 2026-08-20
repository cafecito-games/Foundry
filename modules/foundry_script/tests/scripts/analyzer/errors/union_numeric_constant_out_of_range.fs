# A constant no alternative can hold is refused by the union exactly as the alternative refuses it
# standing alone, so the two spellings agree on rejection as well as on acceptance.
func take_plain(v: uint) -> void:
	print(v)


func take_union(v: uint | String) -> void:
	print(v)


func test():
	take_plain(-1)
	take_union(-1)
	take_plain(5000000000)
	take_union(5000000000)
