# Enum targets are not supported for retroactive conformance.
enum RtcEnumTarget {
	ONE,
	TWO,
}

extend RtcEnumTarget uses RtcEnumTrait:
	func describe() -> String:
		return "enum"
