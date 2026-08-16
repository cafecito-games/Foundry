type IntOrString = int | String

enum Level:
	LOW = 1
	HIGH = 2

trait Identified:
	abstract func id() -> int

class Tagged uses Identified:
	func id() -> int:
		return 7

class Plain:
	pass

var getter_reads := 0
var tracked: Variant = "cached":
	get:
		getter_reads += 1
		return tracked

var allow_first := false

func classify_union(value: IntOrString) -> String:
	match value:
		value is String:
			return "string:" + value
		value is int:
			return "int:" + str(value + 1)
		_:
			return "fallback"

func classify_object(value: Variant) -> String:
	match value:
		value is Resource:
			return "resource:" + value.resource_name
		_:
			return "fallback"

func classify_width(value: Variant) -> String:
	match value:
		value is int:
			return "int"
		value is long:
			return "long"
		_:
			return "fallback"

func classify_trait(value: Variant) -> String:
	match value:
		value is Identified:
			return "identified:" + str(value.id())
		_:
			return "fallback"

func classify_enum(value: Variant) -> String:
	match value:
		value is Level:
			return "level"
		_:
			return "fallback"

func classify_alternatives(value: Variant) -> String:
	match value:
		value is String, value is int:
			return "string or int"
		_:
			return "other"

func classify_guarded(value: Variant) -> String:
	match value:
		value is String when allow_first:
			return "guarded string"
		value is String:
			return "plain string"
		_:
			return "fallback"

func classify_tracked() -> String:
	match tracked:
		tracked is String:
			return "string"
		tracked is int:
			return "int"
		_:
			return "fallback"

func test() -> void:
	print(classify_union("hi"))
	print(classify_union(3))

	var resource := Resource.new()
	resource.resource_name = "named"
	print(classify_object(resource))
	print(classify_object(Plain.new()))
	print(classify_object(12))

	print(classify_width(5))
	print(classify_width(9223372036854775807L))

	print(classify_trait(Tagged.new()))
	print(classify_trait(Plain.new()))

	print(classify_enum(Level.HIGH))
	print(classify_enum(99))

	print(classify_alternatives("text"))
	print(classify_alternatives(9))
	print(classify_alternatives(1.5))

	print(classify_guarded("text"))
	allow_first = true
	print(classify_guarded("text"))

	print(classify_tracked())
	prints("getter reads:", getter_reads)
