# The ancestor that fixed the binding is declared in another file, so the chain walk crosses a file
# boundary before it reads the binding the reified applied vector contradicts. `Array[Self]` reifies
# to `Array[FscxSub]`, which the ancestor's `Array[String]` contradicts at the element position.
const _Ancestor = preload("fsc_chain_ancestor.notest.fs")


final class FscxSub extends FscxBase:
	uses FscxKeeper[Array[Self]]


func test() -> void:
	print("unreachable")
