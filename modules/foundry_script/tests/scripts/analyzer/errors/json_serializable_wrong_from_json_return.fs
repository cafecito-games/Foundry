class_name FixtureEnemy extends RefCounted
uses JsonSerializable

func to_json() -> JsonNode:
	return JsonNode.Null

static func from_json(node: JsonNode) -> JsonResult[JsonDecodeError]:
	return JsonResult[JsonDecodeError].fail("wrong", "$")

func test():
	print(FixtureEnemy.new())
