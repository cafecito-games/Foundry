# Applying type arguments to a generic tagged union specializes its payload schema, so a case
# constructor checks each argument against the concrete field type. Substitution is structural: it
# reaches through typed collections, nullable slots, and a nested application of another union. Two
# applications of one declaration stay independent of each other.
enum Result[T, E]:
	Ok(value: T)
	Err(error: E)

enum Bundle[T]:
	Items(values: Array[T])
	Table(rows: Dictionary[String, T?])
	Wrapped(inner: Result[T, String])
	Empty

func test():
	var ok: Result[int, String] = Result[int, String].Ok(1)
	var err: Result[int, String] = Result[int, String].Err("bad")
	print(ok)
	print(err)

	# The mirrored application of the same declaration binds the parameters the other way round.
	var flipped: Result[String, int] = Result[String, int].Ok("one")
	print(flipped)

	# A collection literal in payload position is typed by the specialized field type, exactly as an
	# ordinary `Array[int]` parameter would type it.
	var items := Bundle[int].Items([1, 2, 3])
	print(items)

	var typed_values: Array[int] = [4, 5]
	print(Bundle[int].Items(typed_values))

	var table := Bundle[int].Table({"a": 1, "b": null})
	print(table)

	var wrapped := Bundle[int].Wrapped(Result[int, String].Ok(7))
	print(wrapped)

	# A payload-less case is a value of the specialized union.
	var empty: Bundle[int] = Bundle[int].Empty
	print(empty)
