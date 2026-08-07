import foundry.http.server

extends HTTPServer

func test() -> void:
	set_port(9000)
	print(get_port())
	print(is_listening())
