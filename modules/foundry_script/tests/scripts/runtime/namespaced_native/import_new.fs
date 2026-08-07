import foundry.http.server

func test() -> void:
	var server := HTTPServer.new()
	server.port = 9000
	print(server.port)
	print(server.start())
	server.free()
