# A type that can be written to and read back from JSON. `from_json` returns a result for
# `Self`, so a conforming type decodes into itself rather than into a sibling type.
trait_name JsonSerializable

abstract func to_json() -> JsonNode
abstract static func from_json(node: JsonNode) -> JsonResult[Self]
