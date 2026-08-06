# A signal flattened in through `uses Emitter[int]` declares an `int` parameter, so connecting a
# `String`-taking callable is rejected at analysis time.
trait Emitter[T]:
	signal changed(v: T)


class IntEmitter uses Emitter[int]:
	pass


func test() -> void:
	var emitter := IntEmitter.new()
	emitter.changed.connect(func(v: String) -> void:
		print(v))
