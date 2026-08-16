# A trait body names the trait's own parameters, so its receiver-relative check has to resolve them
# through the arguments the implementer applied. Resolving them against the implementer's own
# parameter list instead would check against an unrelated argument here: `ShadowingTraitCrate`
# declares a `V` of its own at the same ordinal while applying the trait with `int`, so a String is
# rejected even though the implementer's `V` is String.
trait Keeper[V]:
	func keep_via_trait(value) -> V:
		var kept: V = value
		return kept


class ShadowingTraitCrate[V]:
	uses Keeper[int]


func test() -> void:
	print(ShadowingTraitCrate[String].new().keep_via_trait("not an int"))
