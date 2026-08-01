# Acceptance fixture for compiled-bytecode exports. Every builtin JSON type used here has to
# resolve from the private packaged bytecode when the runtime ships without a Foundry Script
# front-end. Prints BUILTIN_EXPORT_RUNTIME_OK exactly once, and only after every check passed.
extends Node

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

final class Player extends RefCounted:
	uses JsonSerializable

	var player_name: String = ""
	var weapon: Weapon = Weapon.new()

	func to_json() -> JsonNode:
		var entries: Dictionary[String, JsonNode] = {}
		entries["name"] = JsonNode.Str(player_name)
		entries["weapon"] = weapon.to_json()
		return JsonNode.Object(entries)

	static func from_json(node: JsonNode) -> JsonResult[Self]:
		match node:
			JsonNode.Object(var entries):
				if not entries.has("name"):
					return JsonResult[Self].fail("missing field", "$.name")
				var decoded := Player.new()
				match entries["name"]:
					JsonNode.Str(var name_value):
						decoded.player_name = name_value
					_:
						return JsonResult[Self].fail("expected a string", "$.name")
				if entries.has("weapon"):
					var weapon_result := Weapon.from_json(entries["weapon"])
					if not weapon_result.is_ok():
						return JsonResult[Self].nested(weapon_result.error, "weapon")
					decoded.weapon = weapon_result.value
				return JsonResult[Self].ok(decoded)
			_:
				return JsonResult[Self].fail("expected an object", "$")

var failures: Array[String] = []

func check(condition: bool, label: String) -> void:
	if not condition:
		failures.append(label)

func _ready() -> void:
	var player := Player.new()
	player.player_name = "Captain"
	player.weapon.damage = 7

	# JSON.stringify() reaches the script `to_json()` hook through the builtin trait.
	var text := JSON.stringify(player)
	check(text.contains("\"name\":\"Captain\""), "stringify encodes the name field")
	check(text.contains("\"damage\":7"), "stringify encodes the nested weapon")

	var parsed: JsonResult[JsonNode] = JSON.parse_to_node(text)
	check(parsed.is_ok(), "parse_to_node accepts the encoded document")

	var decoded := Player.from_json(parsed.value)
	check(decoded.is_ok(), "decode accepts the round-tripped document")
	check(decoded.value.player_name == "Captain", "decoded name survives the round trip")
	check(decoded.value.weapon.damage == 7, "decoded weapon survives the round trip")

	# A nested failure is re-rooted under its parent key, so JsonDecodeError carries the
	# whole path from the document root.
	var broken: JsonResult[JsonNode] = JSON.parse_to_node('{"name": "Captain", "weapon": {"damage": "lots"}}')
	var broken_decoded := Player.from_json(broken.value)
	check(not broken_decoded.is_ok(), "a malformed nested field fails to decode")
	check(broken_decoded.error.path == "$.weapon.damage", "the decode error names the nested path")
	check(broken_decoded.error.message == "expected an int", "the decode error carries its message")

	match JsonNode.of("plain"):
		JsonNode.Str(var plain_value):
			check(plain_value == "plain", "JsonNode.of() builds a string case")
		_:
			check(false, "JsonNode.of() builds a string case")

	if failures.is_empty():
		print("BUILTIN_EXPORT_RUNTIME_OK")
		get_tree().quit(0)
		return

	for failure: String in failures:
		printerr("BUILTIN_EXPORT_RUNTIME_FAILED: " + failure)
	get_tree().quit(1)
