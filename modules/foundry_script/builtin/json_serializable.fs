# A type that can be written to and read back from JSON. `from_json` returns a result for
# `Self`, so a conforming type decodes into itself rather than into a sibling type.
#
# `to_json` is a script-dispatched hook: the engine never reaches it through C++ virtual
# dispatch. It is registered in `FSScriptExtensibleNativeHooks` so implementing it is not
# reported as an accidental native-method override, and `FSJsonMarshal::call_to_json` is the
# only native code that invokes it.
trait_name JsonSerializable

abstract func to_json() -> JsonNode
abstract static func from_json(node: JsonNode) -> JsonResult[Self]
