# Conforms an engine class to a subtrait of `JsonSerializable` from outside its definition. The
# objects this applies to carry no script, so trait-scoped lookup must find the original witness
# through the implied supertrait membership. Loading this file is what brings it into effect.
trait ResourceJson uses JsonSerializable:
	pass


extend Image uses ResourceJson:
	func to_json() -> JsonNode:
		return JsonNode.Str("image:" + get_class())

	static func from_json(_node: JsonNode) -> JsonResult[Self]:
		return JsonResult[Self].fail("not decodable", "$")
