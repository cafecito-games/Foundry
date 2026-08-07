extends foundry.http.server.HTTPServer

func test() -> void:
	set_port(9100)
	print(get_port())
