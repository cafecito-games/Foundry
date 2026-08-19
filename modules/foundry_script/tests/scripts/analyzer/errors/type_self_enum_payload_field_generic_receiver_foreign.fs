# A payload case constructed through a bounded type-parameter receiver stays receiver-relative:
# the receiver expression itself is admitted in a `Self` position, while an unrelated instance of
# the bound is rejected.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


func take[T: Receiver](r: T, other: Receiver) -> void:
	var ok := r.Message.Attach(1, r)
	var bad := r.Message.Attach(2, other)
	print(ok, bad)


func test() -> void:
	pass
