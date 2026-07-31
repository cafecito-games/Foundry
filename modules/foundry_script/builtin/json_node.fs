# A JSON value as a tagged union. The case order is a wire contract shared with the native
# encoder: Null=0, Bool=1, Int=2, Float=3, Str=4, Array=5, Object=6. Do not reorder.
#
# `Int` and `Float` are separate cases on purpose: the encoder formats integers without a
# fractional part, so folding them into one numeric case would render an `int` field as `1.0`.
enum_name JsonNode:
	Null
	Bool(value: bool)
	Int(value: int)
	Float(value: float)
	Str(value: String)
	Array(items: Array[JsonNode])
	Object(entries: Dictionary[String, JsonNode])

	static func array_of(items: Array[JsonNode]) -> JsonNode:
		return JsonNode.Array(items)

	static func object_of(entries: Dictionary[String, JsonNode]) -> JsonNode:
		return JsonNode.Object(entries)

	static func of(value: Variant) -> JsonNode:
		match typeof(value):
			TYPE_NIL:
				return JsonNode.Null
			TYPE_BOOL:
				return JsonNode.Bool(value)
			TYPE_INT:
				return JsonNode.Int(value)
			TYPE_FLOAT:
				return JsonNode.Float(value)
			TYPE_STRING:
				return JsonNode.Str(value)
			TYPE_ARRAY:
				var items: Array[JsonNode] = []
				for item: Variant in value:
					items.append(JsonNode.of(item))
				return JsonNode.Array(items)
			TYPE_DICTIONARY:
				var entries: Dictionary[String, JsonNode] = {}
				for key: Variant in value:
					entries[str(key)] = JsonNode.of(value[key])
				return JsonNode.Object(entries)
		push_error("JsonNode.of() cannot represent a value of type %d." % typeof(value))
		return JsonNode.Null
