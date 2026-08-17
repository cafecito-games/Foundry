# The gradual rule is unchanged at a trait destination: only evidence that contradicts the
# declaration rejects. A raw destination, a raw conformer, a `Self` argument at either finality, and a
# conformance forwarded through a type parameter all carry no evidence, so every store below stays
# legal and the file analyzes without a diagnostic.
trait Keeper[T]:
	func label() -> String:
		return "keeper"


class ForwardingKeeper[U]:
	uses Keeper[U]


class SelfKeeper:
	uses Keeper[Self]


final class FinalSelfKeeper:
	uses Keeper[Self]


class Passer[V]:
	func forward(value: ForwardingKeeper[V]) -> Keeper[int]:
		return value


func take_raw(value: Keeper) -> void:
	print(value.label())


func pass_through[W](value: ForwardingKeeper[W]) -> Keeper[int]:
	return value


func test() -> void:
	var raw_target: Keeper = ForwardingKeeper[String].new()
	take_raw(raw_target)

	var raw_conformer := ForwardingKeeper.new()
	var raw_source_slot: Keeper[int] = raw_conformer
	print(raw_source_slot.label())

	var self_slot: Keeper[SelfKeeper] = SelfKeeper.new()
	print(self_slot.label())

	var final_self_slot: Keeper[FinalSelfKeeper] = FinalSelfKeeper.new()
	print(final_self_slot.label())

	print(Passer[int].new().forward(ForwardingKeeper[int].new()).label())
	print(pass_through(ForwardingKeeper[int].new()).label())
