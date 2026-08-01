# An engine class conformed to the builtin `JsonSerializable` trait from outside its definition is
# encoded by `JSON.stringify()` through the retroactive witness, even though the instance carries no
# script of its own. `from_json` returns `JsonResult[Self]`, so this fixture also covers a native
# conformance whose signature mentions a generic over `Self` surviving compiled-bytecode export.
extend Resource uses JsonSerializable:
	func to_json() -> JsonNode:
		return JsonNode.Str("resource:" + get_class())

	static func from_json(_node: JsonNode) -> JsonResult[Self]:
		return JsonResult[Self].fail("not decodable", "$")


func test() -> void:
	var resource := Resource.new()
	print(JSON.stringify(resource))
	print(JSON.stringify([resource]))
	print(JSON.stringify({"held": resource}))
