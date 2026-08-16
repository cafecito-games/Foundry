# Forwarding the implementer's own parameter into a trait leaves a parameter in the flattened lambda's
# slot, so that lambda does need the receiver and does capture it. The check has to fire.
trait LambdaKeeper[V]:
	var keeper := func(v):
		var kept: V = v
		return kept


class ForwardingLambdaKeeper[W]:
	uses LambdaKeeper[W]


func test() -> void:
	print(ForwardingLambdaKeeper[int].new().keeper.call("not an int"))
