import foundry.http.server

func make() -> foundry.http.server.HTTPServer:
	return HTTPServer.new()

func test() -> void:
	var server := make()
	print(server is HTTPServer)
	print(server is foundry.http.server.HTTPServer)

	var widened: Object = server
	var narrowed := widened as HTTPServer
	print(narrowed != null)

	var typed: HTTPServer = server
	typed.port = 9200
	print(typed.port)
	server.free()
