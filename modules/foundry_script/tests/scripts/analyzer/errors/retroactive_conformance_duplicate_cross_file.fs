# The same `(target, trait)` pair is already registered in a preloaded file.
const _FirstConformance = preload("retroactive_conformance_duplicate_cross_file.notest.fs")


extend RtcDup uses RtcDupTrait:
	func ping() -> void:
		pass
