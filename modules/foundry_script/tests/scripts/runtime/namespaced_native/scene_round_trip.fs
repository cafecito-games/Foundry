import foundry.http.server

func test() -> void:
	var server := HTTPServer.new()
	server.name = "Server"
	server.port = 9000

	var scene := PackedScene.new()
	print(scene.pack(server) == OK)
	server.free()

	var restored := scene.instantiate()
	print(restored.get_class())
	print(restored.name)
	print(restored is HTTPServer)
	print((restored as HTTPServer).port)
	restored.free()
