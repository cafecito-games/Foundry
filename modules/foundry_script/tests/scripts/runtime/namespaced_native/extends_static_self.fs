import foundry.http.server

trait NamespacedNativeNamed:
	abstract static func named() -> String


extend HTTPServer uses NamespacedNativeNamed:
	static func named() -> String:
		var instance := Self.new()
		var resolved_name := instance.get_class()
		instance.free()
		return resolved_name


func test() -> void:
	print(HTTPServer.named())
	print(foundry.http.server.HTTPServer.named())
