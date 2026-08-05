# Two specializations of the same global declaration must not share payload schemas through the
# dependency cache: each application re-derives its own field types from the open declaration.
import generic_union_fixture

func verify_int_string() -> void:
	var ok: GlobalResult[int, String] = GlobalResult[int, String].Ok(1)
	var err: GlobalResult[int, String] = GlobalResult[int, String].Err("bad")
	print(ok)
	print(err)

func verify_string_int() -> void:
	var ok: GlobalResult[String, int] = GlobalResult[String, int].Ok("good")
	var err: GlobalResult[String, int] = GlobalResult[String, int].Err(2)
	print(ok)
	print(err)

func test():
	verify_int_string()
	verify_string_int()
