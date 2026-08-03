# A generic tagged union's static functions are not reachable through a bare external reference:
# the bare form would hand the caller the declaration's own unbound parameters, so the declared
# bound would not hold for the argument.
enum Holder[T: Resource]:
	Value(value: T)

	static func identity(value: T) -> T:
		return value

func leak() -> Variant:
	return Holder.identity(123)
