# A preloaded file's class `uses` binding and an engine-class conformance declared here may both apply
# one generic trait as long as they bind it to the same type arguments. The binding class keeps its own
# method for dispatch; a plain engine receiver reaches the conformance's witness instead.
const Holder = preload("sccum_chain_dep_uses.notest.fs")


extend RefCounted uses SccumKeeper[int]:
	func make() -> int:
		return 7


func test() -> void:
	var holder := Holder.new()
	var narrow: SccumKeeper[int] = holder
	var widened: RefCounted = holder
	var wide: SccumKeeper[int] = widened
	var plain: SccumKeeper[int] = RefCounted.new()
	print(narrow.make())
	print(wide.make())
	print(plain.make())
