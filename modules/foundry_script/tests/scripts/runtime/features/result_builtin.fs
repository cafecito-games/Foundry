# The shipped builtin Result[T, E] resolves without import, constructs specialized cases, matches and
# `is`-binds payloads, and nests inside typed containers like any other generic tagged union.
func test() -> void:
	var ok: Result[int, String] = Result[int, String].Ok(7)
	var nested: Array[Result[int, String]] = [ok]

	match ok:
		Result[int, String].Ok(var value):
			Utils.check(value == 7)
		Result[int, String].Err(_):
			Utils.check(false)

	if ok is Result[int, String].Err(_):
		Utils.check(false)

	var err: Result[int, String] = Result[int, String].Err("bad")
	if err is Result[int, String].Err(message):
		Utils.check(message == "bad")
	else:
		Utils.check(false)

	Utils.check(nested.size() == 1)
	Utils.check(nested[0] == ok)
	print("ok")
