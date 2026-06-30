# Retroactive conformance v1 supports only Foundry Script class targets; a builtin type target is
# rejected.
extend int uses RtcUnsupportedTrait:
	func describe() -> String:
		return "int"
