# A successful value or an error value.
#
# Case order is a stable runtime and serialization contract: Ok is tag 0 and Err is tag 1.
enum_name Result[T, E]:
	Ok(value: T)
	Err(error: E)
