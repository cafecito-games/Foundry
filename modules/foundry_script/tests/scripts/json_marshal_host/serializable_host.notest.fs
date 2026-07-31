# Loaded by the C++ marshal tests to prove that native code reaches a script implementation
# of the `to_json` hook through `FSJsonMarshal::call_to_json`.
extends RefCounted
uses JsonSerializable

var label: String = "from script"

func to_json() -> JsonNode:
	return JsonNode.Str(label)

static func from_json(_node: JsonNode) -> JsonResult[Self]:
	return JsonResult[Self].fail("not decodable", "$")
