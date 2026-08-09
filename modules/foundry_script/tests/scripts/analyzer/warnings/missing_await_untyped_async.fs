# An untyped async func (no return annotation) yields a Coroutine[Variant]: its result is unknown, so a
# root-discard still warns — unlike an explicitly "-> void" fire-and-forget launch.
async func immediate():
	pass

func test():
	immediate()
