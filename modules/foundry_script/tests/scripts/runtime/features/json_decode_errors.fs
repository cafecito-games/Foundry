# A decode reports why it failed and where: a shape mismatch and a missing field both carry the
# path of the offending member, and a nested failure is re-rooted under its parent's key.
final class_name JsonDecodeErrorPlayer extends RefCounted
uses JsonSerializable

final class Weapon extends RefCounted:
	uses JsonSerializable

	var damage: int = 0

	func to_json() -> JsonNode:
		var entries: Dictionary[String, JsonNode] = {}
		entries["damage"] = JsonNode.Int(damage)
		return JsonNode.Object(entries)

	static func from_json(node: JsonNode) -> JsonResult[Self]:
		match node:
			JsonNode.Object(var entries):
				if not entries.has("damage"):
					return JsonResult[Self].fail("missing field", "$.damage")
				match entries["damage"]:
					JsonNode.Int(var damage_value):
						var decoded := Weapon.new()
						decoded.damage = damage_value
						return JsonResult[Self].ok(decoded)
					_:
						return JsonResult[Self].fail("expected an int", "$.damage")
			_:
				return JsonResult[Self].fail("expected an object", "$")

var level: int = 0
var weapon: Weapon = Weapon.new()

func to_json() -> JsonNode:
	var entries: Dictionary[String, JsonNode] = {}
	entries["level"] = JsonNode.Int(level)
	entries["weapon"] = weapon.to_json()
	return JsonNode.Object(entries)

static func from_json(node: JsonNode) -> JsonResult[Self]:
	match node:
		JsonNode.Object(var entries):
			if not entries.has("level"):
				return JsonResult[Self].fail("missing field", "$.level")
			var decoded := JsonDecodeErrorPlayer.new()
			match entries["level"]:
				JsonNode.Int(var level_value):
					decoded.level = level_value
				_:
					return JsonResult[Self].fail("expected an int", "$.level")
			if entries.has("weapon"):
				var weapon_result := Weapon.from_json(entries["weapon"])
				if not weapon_result.is_ok():
					return JsonResult[Self].nested(weapon_result.error, "weapon")
				decoded.weapon = weapon_result.value
			return JsonResult[Self].ok(decoded)
		_:
			return JsonResult[Self].fail("expected an object", "$")

func test() -> void:
	var wrong_type: JsonResult[JsonNode] = JSON.parse_to_node('{"level": "three"}')
	var decoded := JsonDecodeErrorPlayer.from_json(wrong_type.value)
	print(decoded.is_ok())
	print(decoded.error.message)
	print(decoded.error.path)

	var missing: JsonResult[JsonNode] = JSON.parse_to_node('{}')
	print(JsonDecodeErrorPlayer.from_json(missing.value).error.path)

	var nested: JsonResult[JsonNode] = JSON.parse_to_node('{"level": 1, "weapon": {"damage": "lots"}}')
	print(JsonDecodeErrorPlayer.from_json(nested.value).error.path)

	var bad_text: JsonResult[JsonNode] = JSON.parse_to_node('{not json')
	print(bad_text.is_ok())
	print(bad_text.error.path)
