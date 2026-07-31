# A class that does not conform to `JsonSerializable` keeps the pre-existing representation:
# `JSON.stringify()` writes its quoted `to_string()`, not a marshaled object.
extends RefCounted

var label: String = "plain"

func test() -> void:
	print(JSON.stringify(self).begins_with("\""))
	print(JSON.stringify([self]).begins_with("[\""))
