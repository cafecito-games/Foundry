func test() -> void:
	var server := foundry.http.server.HTTPServer.new()
	var typed: foundry.http.server.HTTPServer = server
	typed.port = 9000
	print(typed.port)
	print(typed.is_listening())
	server.free()
