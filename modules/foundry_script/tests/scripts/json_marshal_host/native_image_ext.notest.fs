# Conforms an engine class to `JsonSerializable` from outside its definition. The objects this
# applies to carry no script, so the witness is reachable only through the conformance registry;
# loading this file is what brings it into effect. `Image` is deliberately narrow: a conformance is
# keyed by (target, trait), so declaring one on a broad base class here would collide with the
# `extend Resource uses JsonSerializable` the runtime fixtures declare.
extend Image uses JsonSerializable:
	func to_json() -> JsonNode:
		return JsonNode.Str("image:" + get_class())

	static func from_json(_node: JsonNode) -> JsonResult[Self]:
		return JsonResult[Self].fail("not decodable", "$")
