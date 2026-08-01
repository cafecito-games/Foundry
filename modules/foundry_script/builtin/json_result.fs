# The outcome of a decode: either a value or the error explaining why there is none.
# This is a generic class rather than a tagged union because enums take no type parameters.
# Success is explicit state established by ok(...); payload nullness is not the discriminant.
class_name JsonResult[T] extends RefCounted

var value: T?
var error: JsonDecodeError?
var _succeeded: bool = false

static func ok(value: T) -> JsonResult[T]:
	var result := JsonResult[T].new()
	result.value = value
	result._succeeded = true
	return result

static func fail(message: String, path: String) -> JsonResult[T]:
	var result := JsonResult[T].new()
	result.error = JsonDecodeError.create(message, path)
	return result

# Re-roots a nested failure under `key`, so a field decoder can report the whole path
# from the document root rather than the path relative to the nested value.
static func nested(error: JsonDecodeError, key: String) -> JsonResult[T]:
	var child_path := error.path
	if child_path.begins_with("$"):
		child_path = child_path.substr(1)
	return JsonResult[T].fail(error.message, "$." + key + child_path)

func is_ok() -> bool:
	return _succeeded and error == null
