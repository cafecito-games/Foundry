# A class conforming to the builtin `JsonSerializable` trait is encoded by `JSON.stringify()`
# through its own `to_json()`, at the top level and at any nesting depth.
extends RefCounted
uses JsonSerializable

var player_name: String = "Captain"
var level: int = 3

func to_json() -> JsonNode:
	var entries: Dictionary[String, JsonNode] = {}
	entries["level"] = JsonNode.Int(level)
	entries["name"] = JsonNode.Str(player_name)
	return JsonNode.Object(entries)

static func from_json(_node: JsonNode) -> JsonResult[Self]:
	return JsonResult[Self].fail("not decodable", "$")

func test() -> void:
	print(JSON.stringify(self))
	print(JSON.stringify([self]))
	print(JSON.stringify({"hero": self}))
	print(JSON.stringify({"party": [self, self]}))
