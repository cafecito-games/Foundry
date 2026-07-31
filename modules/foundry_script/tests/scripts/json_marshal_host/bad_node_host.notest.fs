# Loaded by the C++ marshal tests to prove that a `to_json()` returning something that is not a
# well-formed JsonNode is reported and encoded as `null` rather than trusted.
extends RefCounted
uses JsonSerializable

func to_json() -> JsonNode:
	# A tagged union erases to a read-only `[tag, payload...]` Array at runtime, so an untyped
	# value can reach the marshaller and stand in for bytecode disagreeing with the declaration.
	var bogus: Variant = ["not a tag"]
	return bogus

static func from_json(_node: JsonNode) -> JsonResult[Self]:
	return JsonResult[Self].fail("not decodable", "$")
