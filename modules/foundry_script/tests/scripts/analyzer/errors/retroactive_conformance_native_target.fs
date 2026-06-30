# Retroactive conformance v1 supports only Foundry Script class targets; a native engine class target
# is rejected.
extend Node uses RtcUnsupportedTrait:
	func describe() -> String:
		return "node"
