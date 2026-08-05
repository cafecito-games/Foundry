# Nullable type arguments specialize the builtin Result union like user declarations do.
func test() -> void:
	var nullable: Result[String?, int] = Result[String?, int].Ok(null)
	match nullable:
		Result[String?, int].Ok(var value):
			Utils.check(value == null)
		Result[String?, int].Err(_):
			Utils.check(false)
	print("ok")
