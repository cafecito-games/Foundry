# Regression for issue #1965: a third consumer of the same shared declaring file
# (`csw_shared_int_doubler_ext.notest.fs`) as `conformance_builtin_witness_shared_declaring_file_a`
# and `_b`, covering dispatch through a generic bound (`[T: CswSharedIntDoubler]`) alongside the
# direct and parameter call sites those two cover.
const _Conformance = preload("csw_shared_int_doubler_ext.notest.fs")


func via_bound[T: CswSharedIntDoubler](value: T) -> int:
	return value.csw_shared_doubled()


func test() -> void:
	var n: int = 5
	print(via_bound(n))
