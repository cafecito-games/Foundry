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
