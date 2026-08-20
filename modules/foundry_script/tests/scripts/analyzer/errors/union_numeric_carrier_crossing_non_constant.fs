# Only a value known exactly can be proven to fit a differently carried alternative, so a crossing a
# constant earns is still refused for a variable -- inside a union just as outside one.
func take_plain(v: uint) -> void:
	print(v)


func take_union(v: uint | String) -> void:
	print(v)


func test():
	var n: int = 5
	take_plain(n)
	take_union(n)

	var stored: uint | String = n
	print(stored)
