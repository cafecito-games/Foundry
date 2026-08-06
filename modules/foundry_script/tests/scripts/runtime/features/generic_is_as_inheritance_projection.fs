# A derived value is tested against a base specialization by projecting its own arguments onto that
# base through the declared bindings. A non-generic leaf fixed to `Crate[int]` proves that base even
# though it reifies no arguments of its own; a generic leaf forwarding its parameter proves only the
# argument it was given; and a leaf that reorders its parameters is projected by declaration mapping
# rather than by vector position. Projection follows the whole chain, so a leaf two levels below a
# base proves that base's specialization as well as its intermediate's.
class Crate[T]:
	var item: T


class PairCrate[A, B]:
	var first: A
	var second: B


class IntCrate extends Crate[int]:
	var label := ""


class Derived[U] extends Crate[U]:
	var extra := 0


class Swapped[X, Y] extends PairCrate[Y, X]:
	var marker := 0


class Middle[M] extends Crate[M]:
	var middle_marker := 0


class Leaf[L] extends Middle[L]:
	var leaf_marker := 0


func test() -> void:
	var fixed_leaf: Variant = IntCrate.new()
	print("fixed leaf is Crate: ", fixed_leaf is Crate)
	print("fixed leaf is Crate[int]: ", fixed_leaf is Crate[int])
	print("fixed leaf is Crate[String]: ", fixed_leaf is Crate[String])
	print("fixed leaf as Crate[String] is null: ", (fixed_leaf as Crate[String]) == null)

	var forwarded_leaf: Variant = Derived[int].new()
	print("forwarded leaf is Crate[int]: ", forwarded_leaf is Crate[int])
	print("forwarded leaf is Crate[String]: ", forwarded_leaf is Crate[String])
	print("forwarded leaf is Derived[int]: ", forwarded_leaf is Derived[int])
	print("forwarded leaf is Derived[String]: ", forwarded_leaf is Derived[String])

	var raw_forwarded_leaf: Variant = Derived.new()
	print("raw forwarded leaf is Crate: ", raw_forwarded_leaf is Crate)
	print("raw forwarded leaf is Crate[int]: ", raw_forwarded_leaf is Crate[int])

	var deep_leaf: Variant = Leaf[int].new()
	print("deep leaf is Crate[int]: ", deep_leaf is Crate[int])
	print("deep leaf is Crate[String]: ", deep_leaf is Crate[String])
	print("deep leaf is Middle[int]: ", deep_leaf is Middle[int])
	print("deep leaf is Middle[String]: ", deep_leaf is Middle[String])

	var swapped: Variant = Swapped[int, String].new()
	print("swapped is PairCrate[String, int]: ", swapped is PairCrate[String, int])
	print("swapped is PairCrate[int, String]: ", swapped is PairCrate[int, String])
	print("swapped as PairCrate[String, int] keeps identity: ", (swapped as PairCrate[String, int]) == swapped)

	var pair: Variant = PairCrate[int, String].new()
	print("pair is PairCrate[int, String]: ", pair is PairCrate[int, String])
	print("pair is PairCrate[String, int]: ", pair is PairCrate[String, int])

	var fixed_leaf_handle: Variant = IntCrate
	print("fixed leaf handle is Type[Crate[int]]: ", fixed_leaf_handle is Type[Crate[int]])
	print("fixed leaf handle is Type[Crate[String]]: ", fixed_leaf_handle is Type[Crate[String]])

	print("is/as inheritance projection ok")
