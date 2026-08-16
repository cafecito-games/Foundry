# A lambda inside a trait's field initializer is compiled with the implementer's `@implicit_new()`,
# away from the trait's own methods, and still names the trait's parameters. It resolves through the
# arguments this implementer applied: `ShadowingLambdaKeeper` declares a `V` of its own at the same
# ordinal while applying the trait with `int`, so a String is rejected.
trait LambdaKeeper[V]:
	var keeper := func(v):
		var kept: V = v
		return kept


class ShadowingLambdaKeeper[V]:
	uses LambdaKeeper[int]


func test() -> void:
	print(ShadowingLambdaKeeper[String].new().keeper.call("not an int"))
