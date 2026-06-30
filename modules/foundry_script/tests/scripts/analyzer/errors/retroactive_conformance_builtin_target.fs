# Retroactive conformance supports Foundry Script class and native engine-class targets; a builtin
# (value) type target like `int` is still rejected.
extend int uses RtcUnsupportedTrait:
	func describe() -> String:
		return "int"
