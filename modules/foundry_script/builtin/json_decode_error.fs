# Why a decode failed, and where in the document it failed. `path` is a JSONPath-like
# location such as `$.inventory.0.name`.
class_name JsonDecodeError extends RefCounted

var message: String
var path: String

static func create(message: String, path: String) -> JsonDecodeError:
	var error := JsonDecodeError.new()
	error.message = message
	error.path = path
	return error
