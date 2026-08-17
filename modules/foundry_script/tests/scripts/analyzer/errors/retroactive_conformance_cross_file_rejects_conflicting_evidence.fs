# Evidence recorded by a conformance in another file is compared here too: visibility decides whether
# the conformance is seen at all, and a seen conformance's arguments are seen with it.
const _Conformance = preload("retroactive_conformance_cross_file_source.notest.fs")


func test() -> void:
	var conflicting: RcxKeeper[String] = RcxTarget.new()
	print(conflicting)
