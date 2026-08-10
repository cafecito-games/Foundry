extends RefCounted

const _Conformance = preload("conformance.fs")


func probe() -> RtcvGadgetlike:
	var holder := RtcvB.new()
	print(holder.thing)
	return holder.thing
