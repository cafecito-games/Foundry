# An explicit type argument is a type position, so a user-declared alias resolves there exactly
# as `Number` does, keeping the member's runtime typing (including numeric width for a
# single-member alias) and its bound-checking path for a multi-member union alias.
type Meters = float
type Wide = ulong
type Scalar = int | String


class Box[T]:
	var value: T


func identity[T](value: T) -> T:
	return value


func describe[T: Scalar](value: T) -> String:
	return str(value)


func test() -> void:
	# A single-member alias in explicit type-argument position resolves to its member and keeps
	# that member's runtime typing.
	var boxed := Box[Meters].new()
	boxed.value = 2.5
	print(typeof(boxed.value) == TYPE_FLOAT)

	# The member's numeric width is preserved, so a `ulong`-only alias accepts a `ulong` literal
	# with no width mismatch.
	print(identity[Wide](1UL))

	# A multi-member union alias in explicit type-argument position resolves and is checked
	# against the target's bound the same way `Number` is.
	print(describe[Scalar](1))
	print(describe[Scalar]("text"))

	# Explicit and inferred type arguments share one bound-checking path: both accept the alias.
	print(describe(2))
