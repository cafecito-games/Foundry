# A class type parameter is reified onto the instance, so a function-body slot declared with it --
# a local, a later store into that local, or the return -- is validated against the argument this
# receiver actually carries, without generating a separate method body per specialization. The check
# happens at the slot's own boundary, so it holds no matter what the caller consumes the result as.
# The slot itself stays erased: the check rejects values the receiver cannot hold, it does not retype
# the value a gradual consumer receives.
class Crate[T]:
	func keep(value) -> T:
		var kept: T = value
		return kept

	func replace(first, second) -> T:
		var kept: T = first
		kept = second
		return kept

	func collect(value) -> Array[T]:
		var kept: Array[T] = [value]
		return kept


# An inherited body still resolves against the leaf's specialization, not against the parameter the
# declaring class left open.
class IntCrate extends Crate[int]:
	pass


class Relay[U] extends Crate[U]:
	pass


trait Keeper[V]:
	func keep_via_trait(value) -> V:
		var kept: V = value
		return kept


class TraitCrate[W]:
	uses Keeper[W]


func test() -> void:
	var crate := Crate[int].new()
	print(crate.keep(5))
	print(crate.replace(1, 2))
	print(crate.collect(3))

	var inherited := IntCrate.new()
	print(inherited.keep(11))

	var relayed := Relay[String].new()
	print(relayed.keep("kept"))

	var via_trait := TraitCrate[int].new()
	print(via_trait.keep_via_trait(13))

	# A raw, un-parameterized receiver carries no reified argument, so the slot stays gradual.
	var raw := Crate.new()
	print(raw.keep("anything"))
	print("class parameter local and return ok")
