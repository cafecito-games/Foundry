# A locally declared signal flows through the non-explicit Signal path: its rich parameter signature
# is populated from the declaration without `has_explicit_method_signature`. A nested Callable
# parameter mismatch must still be detected, so a signal whose parameter is `Callable[[String], void]`
# cannot be assigned to a variable annotated `Signal[[Callable[[int], void]]]`, even though the outer
# Signal shapes are identical.
signal event(cb: Callable[[String], void])


func test() -> void:
	var typed_event: Signal[[Callable[[int], void]]] = event
	print(typed_event)
