# A conforming value encoded by `JSON.stringify()` parses back through `JSON.parse_to_node()`
# and decodes into an equal instance: the two directions share one `JsonNode` representation.
#
# JSON has a single number type, so a parsed number with no fractional part arrives as
# `JsonNode.Int`. A decoder for a `float` field therefore accepts `Int` as well as `Float`.
final class_name JsonRoundTripPlayer extends RefCounted
uses JsonSerializable

var player_name: String = ""
var level: int = 0
var ratio: float = 0.0

func to_json() -> JsonNode:
	var entries: Dictionary[String, JsonNode] = {}
	entries["level"] = JsonNode.Int(level)
	entries["name"] = JsonNode.Str(player_name)
	entries["ratio"] = JsonNode.Float(ratio)
	return JsonNode.Object(entries)

static func from_json(node: JsonNode) -> JsonResult[Self]:
	match node:
		JsonNode.Object(var entries):
			if not entries.has("name"):
				return JsonResult[Self].fail("missing field", "$.name")
			if not entries.has("level"):
				return JsonResult[Self].fail("missing field", "$.level")
			if not entries.has("ratio"):
				return JsonResult[Self].fail("missing field", "$.ratio")
			var decoded := JsonRoundTripPlayer.new()
			match entries["name"]:
				JsonNode.Str(var name_value):
					decoded.player_name = name_value
				_:
					return JsonResult[Self].fail("expected a string", "$.name")
			match entries["level"]:
				JsonNode.Int(var level_value):
					decoded.level = level_value
				_:
					return JsonResult[Self].fail("expected an int", "$.level")
			match entries["ratio"]:
				JsonNode.Float(var ratio_value):
					decoded.ratio = ratio_value
				JsonNode.Int(var whole_ratio_value):
					decoded.ratio = float(whole_ratio_value)
				_:
					return JsonResult[Self].fail("expected a number", "$.ratio")
			return JsonResult[Self].ok(decoded)
		_:
			return JsonResult[Self].fail("expected an object", "$")

func test() -> void:
	player_name = "Captain"
	level = 3
	ratio = 0.5

	var text := JSON.stringify(self)
	print(text)

	var parsed := JSON.parse_to_node(text)
	print(parsed.is_ok())

	var decoded := JsonRoundTripPlayer.from_json(parsed.value)
	print(decoded.is_ok())
	print(decoded.value.player_name)
	print(decoded.value.level)
	print(decoded.value.ratio)
